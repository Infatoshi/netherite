"""Deterministic FP32 versus BF16 PPO numeric oracle.

This is intentionally outside the training hot path. It measures BF16 error
at the production PPO minibatch shape and emits component-wise receipts used
as the acceptance ceiling for native implementations.
"""

import argparse
import json
import os
import struct
import sys
import tempfile
from contextlib import nullcontext

import torch

HERE = os.path.dirname(os.path.abspath(__file__))
ENV = os.path.abspath(os.path.join(HERE, "..", "..", "env"))
sys.path.insert(0, ENV)

from ppo_chain_cu import (
    CLIP,
    ENT,
    GRAD_CLIP,
    HEADS,
    NCH,
    NPLANES,
    NSCAL,
    STACK,
    ChainPolicy,
)


def make_inputs(batch, seed, device):
    gen = torch.Generator(device=device).manual_seed(seed)
    obs = torch.randint(
        0, 2, (batch, NCH, 36, 64), dtype=torch.uint8,
        device=device, generator=gen)
    for stack_i in range(STACK):
        depth_i = stack_i * NPLANES + 7
        obs[:, depth_i] = torch.randint(
            0, 256, obs[:, depth_i].shape, dtype=torch.uint8,
            device=device, generator=gen)
    planes = obs.float()
    for stack_i in range(STACK):
        planes[:, stack_i * NPLANES + 7] /= 255.0
    scal = torch.randn(
        (batch, NSCAL), dtype=torch.float32, device=device, generator=gen)
    row = torch.arange(batch, device=device)
    actions = torch.stack(
        [(row * (2 * head_i + 1) + seed) % size
         for head_i, size in enumerate(HEADS)], dim=1).long()
    adv = torch.sin(row.float() * 0.017 + seed).float()
    adv = (adv - adv.mean()) / (adv.std() + 1e-8)
    return planes, scal, actions, adv


def logprob_entropy(logits, actions):
    dists = [torch.distributions.Categorical(logits=logit)
             for logit in logits]
    logprob = sum(
        dist.log_prob(actions[:, head_i])
        for head_i, dist in enumerate(dists))
    entropy = sum(dist.entropy() for dist in dists)
    return logprob, entropy


def clone_state(state):
    return {name: value.detach().clone() for name, value in state.items()}


def run_step(state, planes, scal, actions, adv, old_logprob, returns, bf16):
    net = ChainPolicy().cuda()
    net.load_state_dict(state)
    opt = torch.optim.Adam(net.parameters(), lr=3e-4)
    opt.zero_grad()
    amp = torch.amp.autocast("cuda", dtype=torch.bfloat16) \
        if bf16 else nullcontext()
    with amp:
        logits, values = net(planes, scal)
        logprob, entropy = logprob_entropy(logits, actions)
        ratio = torch.exp(logprob.float() - old_logprob)
        policy_loss = -torch.min(
            ratio * adv,
            torch.clamp(ratio, 1.0 - CLIP, 1.0 + CLIP) * adv).mean()
        value_loss = 0.5 * ((returns - values.float()) ** 2).mean()
        entropy_loss = -ENT * entropy.float().mean()
        loss = policy_loss + value_loss + entropy_loss
    loss.backward()
    gradients = torch.cat([
        parameter.grad.detach().float().flatten()
        for parameter in net.parameters()
    ])
    grad_norm = torch.nn.utils.clip_grad_norm_(net.parameters(), GRAD_CLIP)
    opt.step()
    updated = torch.cat([
        parameter.detach().float().flatten() for parameter in net.parameters()
    ])
    initial = torch.cat([
        state[name].detach().float().flatten()
        for name, _ in net.named_parameters()
    ])
    return {
        "logits": torch.cat([value.detach().float().flatten()
                             for value in logits]),
        "values": values.detach().float(),
        "logprob": logprob.detach().float(),
        "entropy": entropy.detach().float(),
        "policy_loss": policy_loss.detach().float().reshape(1),
        "value_loss": value_loss.detach().float().reshape(1),
        "entropy_loss": entropy_loss.detach().float().reshape(1),
        "loss": loss.detach().float().reshape(1),
        "gradients": gradients,
        "grad_norm": grad_norm.detach().float().reshape(1),
        "update_delta": updated - initial,
        "net": net,
    }


def error_stats(reference, candidate):
    reference = reference.double()
    candidate = candidate.double()
    delta = (candidate - reference).abs()
    denom = reference.abs().clamp_min(1e-12)
    return {
        "max_abs": delta.max().item(),
        "mean_abs": delta.mean().item(),
        "rmse": torch.sqrt(torch.mean(delta.square())).item(),
        "max_rel": (delta / denom).max().item(),
        "reference_abs_max": reference.abs().max().item(),
    }


def checkpoint_roundtrip(net):
    scratch = os.environ.get("TMPDIR", os.path.join(HERE, ".tmp"))
    os.makedirs(scratch, exist_ok=True)
    fd, path = tempfile.mkstemp(prefix="bf16-oracle-", suffix=".pt", dir=scratch)
    os.close(fd)
    try:
        state = {name: value.detach().cpu()
                 for name, value in net.state_dict().items()}
        torch.save(state, path)
        loaded = ChainPolicy()
        loaded.load_state_dict(torch.load(path, weights_only=True))
        return all(torch.equal(state[name], loaded.state_dict()[name])
                   for name in state)
    finally:
        os.unlink(path)


def write_tensor(handle, name, tensor):
    tensor = tensor.detach().cpu().contiguous()
    if tensor.dtype == torch.float32:
        dtype = 1
    elif tensor.dtype == torch.int64:
        dtype = 2
    else:
        raise TypeError(f"unsupported fixture dtype {tensor.dtype}")
    encoded = name.encode("utf-8")
    raw = tensor.numpy().tobytes(order="C")
    handle.write(struct.pack("<I", len(encoded)))
    handle.write(encoded)
    handle.write(struct.pack("<BI", dtype, tensor.ndim))
    handle.write(struct.pack(f"<{tensor.ndim}q", *tensor.shape))
    handle.write(struct.pack("<Q", len(raw)))
    handle.write(raw)


def write_native_fixture(path, batch, seed):
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)
    device = torch.device("cuda")
    initial = ChainPolicy().to(device)
    state = clone_state(initial.state_dict())
    planes, scal, actions, adv = make_inputs(batch, seed + 1000, device)
    with torch.no_grad():
        logits, values = initial(planes, scal)
        base_logprob, _ = logprob_entropy(logits, actions)
        row = torch.arange(batch, device=device).float()
        old_logprob = base_logprob.float() + 0.3 * torch.sin(row * 0.013)
        returns = values.float() + 0.5 * torch.cos(row * 0.011)
    result_bf16 = run_step(
        state, planes, scal, actions, adv, old_logprob, returns, True)
    result_fp32 = run_step(
        state, planes, scal, actions, adv, old_logprob, returns, False)
    sampling_model = ChainPolicy().to(device)
    sampling_model.load_state_dict(state)
    with torch.no_grad(), torch.amp.autocast("cuda", dtype=torch.bfloat16):
        sampling_logits, _ = sampling_model(planes, scal)
    sampling_seed = seed + 2000
    torch.manual_seed(sampling_seed)
    sample_actions = torch.stack([
        torch.distributions.Categorical(logits=logit).sample()
        for logit in sampling_logits], dim=1)
    with torch.no_grad():
        sampling_logits_fp32, _ = sampling_model(planes, scal)
    torch.manual_seed(sampling_seed)
    sample_actions_fp32 = torch.stack([
        torch.distributions.Categorical(logits=logit).sample()
        for logit in sampling_logits_fp32], dim=1)
    tensors = {}
    tensors.update({f"state/{name}": value.float() for name, value in state.items()})
    tensors.update({
        "input/planes": planes,
        "input/scalars": scal,
        "input/actions": actions,
        "input/advantages": adv,
        "input/old_logprob": old_logprob,
        "input/returns": returns,
        "input/sampling_seed": torch.tensor([sampling_seed], dtype=torch.int64),
        "expected/sample_actions_bf16": sample_actions,
        "expected/sample_actions_fp32": sample_actions_fp32,
    })
    for name in (
            "logits", "values", "logprob", "entropy", "policy_loss",
            "value_loss", "entropy_loss", "loss", "gradients", "grad_norm",
            "update_delta"):
        tensors[f"expected/{name}"] = result_bf16[name].float()
        tensors[f"expected/fp32/{name}"] = result_fp32[name].float()
    with open(path, "wb") as handle:
        handle.write(b"NBORCL1\0")
        handle.write(struct.pack("<I", len(tensors)))
        for name, tensor in tensors.items():
            write_tensor(handle, name, tensor)
    return {
        "path": os.path.abspath(path),
        "batch": batch,
        "seed": seed,
        "tensor_count": len(tensors),
        "bytes": os.path.getsize(path),
    }


def one_case(batch, seed):
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)
    device = torch.device("cuda")
    initial = ChainPolicy().to(device)
    state = clone_state(initial.state_dict())
    planes, scal, actions, adv = make_inputs(batch, seed + 1000, device)
    with torch.no_grad():
        logits, values = initial(planes, scal)
        base_logprob, _ = logprob_entropy(logits, actions)
        row = torch.arange(batch, device=device).float()
        old_logprob = base_logprob.float() + 0.3 * torch.sin(row * 0.013)
        returns = values.float() + 0.5 * torch.cos(row * 0.011)
    fp32 = run_step(
        state, planes, scal, actions, adv, old_logprob, returns, False)
    bf16 = run_step(
        state, planes, scal, actions, adv, old_logprob, returns, True)
    names = [
        "logits", "values", "logprob", "entropy", "policy_loss",
        "value_loss", "entropy_loss", "loss", "gradients", "grad_norm",
        "update_delta",
    ]
    errors = {name: error_stats(fp32[name], bf16[name]) for name in names}

    # Sampling uses the same Categorical implementation as training. Compare
    # aggregate distributions with independently reset RNG streams because
    # BF16 probability rounding can legitimately move boundary samples.
    with torch.no_grad():
        net_fp32 = ChainPolicy().to(device)
        net_fp32.load_state_dict(state)
        logits_fp32, _ = net_fp32(planes, scal)
        net_bf16 = ChainPolicy().to(device)
        net_bf16.load_state_dict(state)
        with torch.amp.autocast("cuda", dtype=torch.bfloat16):
            logits_bf16, _ = net_bf16(planes, scal)
        torch.manual_seed(seed + 2000)
        sample_fp32 = torch.stack([
            torch.distributions.Categorical(logits=logit).sample()
            for logit in logits_fp32], dim=1)
        torch.manual_seed(seed + 2000)
        sample_bf16 = torch.stack([
            torch.distributions.Categorical(logits=logit).sample()
            for logit in logits_bf16], dim=1)
    mismatches = (sample_fp32 != sample_bf16)
    return {
        "seed": seed,
        "errors": errors,
        "sampling": {
            "mismatch_count": mismatches.sum().item(),
            "total": mismatches.numel(),
            "mismatch_rate": mismatches.float().mean().item(),
        },
        "checkpoint_roundtrip_exact": checkpoint_roundtrip(bf16["net"]),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--batch", type=int, default=8192)
    parser.add_argument("--seeds", default="7,19,41")
    parser.add_argument("--output")
    parser.add_argument("--fixture-output")
    parser.add_argument("--fixture-batch", type=int, default=128)
    args = parser.parse_args()
    seeds = [int(value) for value in args.seeds.split(",")]
    cases = [one_case(args.batch, seed) for seed in seeds]
    component_names = cases[0]["errors"]
    ceilings = {
        name: {
            key: max(case["errors"][name][key] for case in cases)
            for key in ("max_abs", "mean_abs", "rmse", "max_rel")
        }
        for name in component_names
    }
    receipt = {
        "schema": "netherite.ppo-bf16-oracle.v1",
        "batch": args.batch,
        "seeds": seeds,
        "torch": torch.__version__,
        "cuda": torch.version.cuda,
        "cudnn": torch.backends.cudnn.version(),
        "gpu": torch.cuda.get_device_name(),
        "matmul_fp32_precision": torch.backends.cuda.matmul.fp32_precision,
        "cudnn_fp32_precision": torch.backends.cudnn.conv.fp32_precision,
        "cases": cases,
        "bf16_error_ceilings": ceilings,
        "tolerance_rule": "candidate error must not exceed the maximum measured BF16 baseline error over all fixed seeds",
        "all_checkpoint_roundtrips_exact": all(
            case["checkpoint_roundtrip_exact"] for case in cases),
    }
    if args.fixture_output:
        receipt["native_fixture"] = write_native_fixture(
            args.fixture_output, args.fixture_batch, seeds[0])
    rendered = json.dumps(receipt, indent=2, sort_keys=True)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as handle:
            handle.write(rendered + "\n")
    print(rendered)


if __name__ == "__main__":
    main()
