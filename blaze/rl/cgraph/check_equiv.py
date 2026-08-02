#!/usr/bin/env python3
"""FP32 Torch versus C++ cgraph forward and one-update equivalence gate."""

import argparse
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import torch


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "blaze" / "env"))
import ppo_chain_cu as P  # noqa: E402


def inputs(batch, seed, device):
    generator = torch.Generator(device=device).manual_seed(seed)
    obs = torch.randint(
        0,
        2,
        (batch, P.NCH, P.CAM_H, P.CAM_W),
        dtype=torch.uint8,
        device=device,
        generator=generator,
    )
    for stack in range(P.STACK):
        channel = stack * P.NPLANES + 7
        obs[:, channel] = torch.randint(
            0,
            256,
            obs[:, channel].shape,
            dtype=torch.uint8,
            device=device,
            generator=generator,
        )
    planes = obs.float()
    for stack in range(P.STACK):
        planes[:, stack * P.NPLANES + 7] /= 255.0
    scalars = torch.randn(
        (batch, P.NSCAL), device=device, generator=generator
    )
    row = torch.arange(batch, device=device)
    actions = torch.stack(
        [
            (row * (2 * head + 1) + seed) % width
            for head, width in enumerate(P.HEADS)
        ],
        dim=1,
    ).long()
    advantages = torch.sin(row.float() * 0.017 + seed)
    advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)
    return planes, scalars, actions, advantages


def run_step(state, planes, scalars, actions, advantages, old_logprob, returns):
    model = P.ChainPolicy().to(planes.device)
    model.load_state_dict(state, strict=True)
    optimizer = torch.optim.Adam(model.parameters(), lr=3e-4)
    optimizer.zero_grad()
    logits, values = model(planes, scalars)
    logprob, entropy = P.fused_logp_entropy(logits, actions)
    ratio = torch.exp(logprob - old_logprob)
    policy_loss = -torch.min(
        ratio * advantages,
        torch.clamp(ratio, 1.0 - P.CLIP, 1.0 + P.CLIP) * advantages,
    ).mean()
    value_loss = 0.5 * (returns - values).square().mean()
    entropy_loss = -P.ENT * entropy.mean()
    loss = policy_loss + value_loss + entropy_loss
    loss.backward()
    gradients = torch.cat(
        [parameter.grad.detach().flatten() for parameter in model.parameters()]
    )
    grad_norm = gradients.norm()
    torch.nn.utils.clip_grad_norm_(model.parameters(), P.GRAD_CLIP)
    optimizer.step()
    initial = torch.cat(
        [state[name].detach().flatten() for name, _ in model.named_parameters()]
    )
    updated = torch.cat(
        [parameter.detach().flatten() for parameter in model.parameters()]
    )
    return {
        "logits": torch.cat([value.detach().flatten() for value in logits]),
        "values": values.detach(),
        "logprob": logprob.detach(),
        "entropy": entropy.detach(),
        "policy_loss": policy_loss.detach().reshape(1),
        "value_loss": value_loss.detach().reshape(1),
        "entropy_loss": entropy_loss.detach().reshape(1),
        "loss": loss.detach().reshape(1),
        "gradients": gradients,
        "grad_norm": grad_norm.detach().reshape(1),
        "update_delta": updated - initial,
        "greedy_actions": torch.stack(
            [value.detach().argmax(1) for value in logits], dim=1
        ),
    }


def write_tensor(stream, name, tensor):
    tensor = tensor.detach().cpu().contiguous()
    dtype = {torch.float32: 1, torch.int64: 2}[tensor.dtype]
    encoded = name.encode()
    raw = tensor.numpy().tobytes()
    stream.write(struct.pack("<I", len(encoded)))
    stream.write(encoded)
    stream.write(struct.pack("<BI", dtype, tensor.ndim))
    stream.write(struct.pack("<" + "q" * tensor.ndim, *tensor.shape))
    stream.write(struct.pack("<Q", len(raw)))
    stream.write(raw)


def make_fixture(path, batch, seed):
    device = torch.device("cuda:0")
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)
    model = P.ChainPolicy().to(device)
    state = {name: value.detach().clone() for name, value in model.state_dict().items()}
    planes, scalars, actions, advantages = inputs(batch, seed + 1000, device)
    with torch.no_grad():
        logits, values = model(planes, scalars)
        base_logprob, _ = P.fused_logp_entropy(logits, actions)
        row = torch.arange(batch, device=device).float()
        old_logprob = base_logprob + 0.3 * torch.sin(row * 0.013)
        returns = values + 0.5 * torch.cos(row * 0.011)
    reference = run_step(
        state, planes, scalars, actions, advantages, old_logprob, returns
    )
    control = run_step(
        state, planes, scalars, actions, advantages, old_logprob, returns
    )
    gradient_floor = (
        (reference["gradients"] - control["gradients"]).norm()
        / reference["gradients"].norm()
    ).item()
    gradient_budget = max(1e-5, 3.0 * gradient_floor)

    tensors = {f"state/{name}": value.float() for name, value in state.items()}
    tensors.update(
        {
            "input/planes": planes,
            "input/scalars": scalars,
            "input/actions": actions,
            "input/advantages": advantages,
            "input/old_logprob": old_logprob,
            "input/returns": returns,
            "input/gradient_budget": torch.tensor([gradient_budget]),
            "expected/greedy_actions": reference["greedy_actions"],
        }
    )
    for name, value in reference.items():
        if name != "greedy_actions":
            tensors[f"expected/fp32/{name}"] = value.float()
    with path.open("wb") as stream:
        stream.write(b"NBORCL1\0")
        stream.write(struct.pack("<I", len(tensors)))
        for name, tensor in tensors.items():
            write_tensor(stream, name, tensor)
    return gradient_floor, gradient_budget


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--batch", type=int, default=6144)
    parser.add_argument("--seed", type=int, default=1234)
    parser.add_argument("--binary", type=Path, default=HERE / "build" / "cgraph_train")
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()

    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cuda.matmul.allow_tf32 = False
    scratch = Path(os.environ["TMPDIR"])
    descriptor, raw_path = tempfile.mkstemp(prefix="cgraph-equiv-", suffix=".fixture", dir=scratch)
    os.close(descriptor)
    fixture = Path(raw_path)
    try:
        floor, budget = make_fixture(fixture, args.batch, args.seed)
        env = dict(os.environ)
        env["CGRAPH_EQUIV_FIXTURE"] = str(fixture)
        process = subprocess.run(
            [str(args.binary)], cwd=ROOT, env=env, text=True, capture_output=True
        )
        print(process.stdout, end="")
        print(process.stderr, end="", file=sys.stderr)
        metrics = {
            name: float(value)
            for name, value in re.findall(
                r"ORACLE component=(\S+) max_abs=([0-9.eE+-]+)",
                process.stdout,
            )
        }
        greedy_match = re.search(r"ORACLE greedy_agreement=([0-9.eE+-]+)", process.stdout)
        gradient_match = re.search(r"ORACLE gradient_rel_l2=([0-9.eE+-]+)", process.stdout)
        receipt = {
            "schema": "netherite.cgraph-equivalence.v1",
            "batch": args.batch,
            "seed": args.seed,
            "torch": torch.__version__,
            "cuda": torch.version.cuda,
            "cudnn": torch.backends.cudnn.version(),
            "gpu": torch.cuda.get_device_name(0),
            "eager_gradient_rel_l2_floor": floor,
            "gradient_rel_l2_budget": budget,
            "components_max_abs": metrics,
            "greedy_agreement": float(greedy_match.group(1)) if greedy_match else None,
            "gradient_rel_l2": float(gradient_match.group(1)) if gradient_match else None,
            "passed": process.returncode == 0,
        }
        rendered = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
        if args.receipt:
            args.receipt.parent.mkdir(parents=True, exist_ok=True)
            args.receipt.write_text(rendered)
        print(rendered, end="")
        if process.returncode != 0:
            raise SystemExit(process.returncode)
    finally:
        fixture.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
