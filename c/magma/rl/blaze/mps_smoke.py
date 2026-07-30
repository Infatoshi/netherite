#!/usr/bin/env python3
"""One real MPS policy update over the hybrid Metal Blaze backend."""
import os
import tempfile

import numpy as np
import torch
from torch import nn

from blaze import VecBlaze
from metal_test_utils import write_synthetic_snapshot


class TinyPolicy(nn.Module):
    def __init__(self):
        super().__init__()
        self.vision = nn.Sequential(
            nn.Conv2d(3, 12, 5, stride=2, padding=2), nn.SiLU(),
            nn.Conv2d(12, 16, 3, stride=2, padding=1), nn.SiLU(),
            nn.AdaptiveAvgPool2d((3, 4)), nn.Flatten())
        self.trunk = nn.Sequential(nn.Linear(16 * 3 * 4 + 6, 64), nn.Tanh())
        self.heads = nn.ModuleList([nn.Linear(64, k) for k in (3, 3, 2, 2, 2)])
        self.value = nn.Linear(64, 1)

    def forward(self, cam, depth, edge, scal):
        x = torch.stack((cam.float() / 32.0, depth.float() / 255.0,
                         edge.float()), dim=1)
        h = self.trunk(torch.cat((self.vision(x), scal), dim=1))
        return [head(h) for head in self.heads], self.value(h).squeeze(1)


def main():
    if os.environ.get("PYTORCH_MPS_FAST_MATH", "0") not in ("", "0"):
        raise RuntimeError("PYTORCH_MPS_FAST_MATH must be disabled for this smoke")
    if not torch.backends.mps.is_available():
        raise RuntimeError("PyTorch MPS is unavailable")
    n = int(os.environ.get("N_ENVS", "32"))
    chunks = int(os.environ.get("T_CHUNK", "2"))
    minibatch = int(os.environ.get("MB", "64"))
    if n <= 0 or chunks <= 0 or minibatch <= 0:
        raise ValueError("N_ENVS, T_CHUNK, and MB must be positive")
    torch.manual_seed(20260730)
    device = torch.device("mps")

    with tempfile.TemporaryDirectory(prefix="blaze-mps-smoke-") as td:
        snapshot = os.environ.get("BLAZE_SNAPSHOT")
        if snapshot:
            snapshot = os.path.abspath(snapshot)
            if not os.path.isfile(snapshot):
                raise FileNotFoundError(f"BLAZE_SNAPSHOT not found: {snapshot}")
        else:
            snapshot = write_synthetic_snapshot(
                os.path.join(td, "synthetic.bsnp"))
        env = VecBlaze(n, backend="metal", output_device="mps")
        try:
            env.load_snapshots([snapshot])
            env.assign(np.zeros(n, dtype=np.int32)); env.reset()
            env.step(np.zeros((n, 5), dtype=np.int64), repeat=1)
            model = TinyPolicy().to(device)
            optimizer = torch.optim.Adam(model.parameters(), lr=3e-4)
            mb = min(minibatch, n)
            before = torch.cat([p.detach().flatten().cpu()
                                for p in model.parameters()])

            optimizer.zero_grad(set_to_none=True)
            total_loss = torch.zeros((), device=device)
            for _ in range(chunks):
                logits, value = model(env.cam, env.depth, env.edge, env.scal)
                dists = [torch.distributions.Categorical(logits=x)
                         for x in logits]
                action = torch.stack([d.sample() for d in dists], dim=1)
                logp = sum(d.log_prob(action[:, i])
                           for i, d in enumerate(dists))
                entropy = sum(d.entropy() for d in dists)
                env.step(action, repeat=1)
                advantage = env.rew - value.detach()
                for start in range(0, n, mb):
                    stop = min(start + mb, n)
                    weight = (stop - start) / (n * chunks)
                    policy_loss = -(logp[start:stop] *
                                    advantage[start:stop]).mean()
                    value_loss = 0.5 * (value[start:stop] -
                                        env.rew[start:stop]).square().mean()
                    total_loss = total_loss + weight * (
                        policy_loss + value_loss -
                        1e-3 * entropy[start:stop].mean())
            total_loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            torch.mps.synchronize()

            after = torch.cat([p.detach().flatten().cpu()
                               for p in model.parameters()])
            if not torch.isfinite(total_loss).item():
                raise AssertionError(f"non-finite loss: {total_loss.item()}")
            if torch.equal(before, after):
                raise AssertionError("Adam update did not change parameters")

            checkpoint = os.path.join(td, "smoke.pt")
            torch.save({"model": model.state_dict(),
                        "optimizer": optimizer.state_dict()}, checkpoint)
            restored = TinyPolicy().to(device)
            restored.load_state_dict(torch.load(checkpoint, map_location=device,
                                                 weights_only=True)["model"])
            restored.eval()
            with torch.no_grad():
                eval_logits, eval_value = restored(
                    env.cam, env.depth, env.edge, env.scal)
            if not torch.isfinite(eval_value).all().item() or not all(
                    torch.isfinite(x).all().item() for x in eval_logits):
                raise AssertionError("non-finite checkpoint evaluation")

            mask = torch.zeros(n, dtype=torch.uint8, device=device)
            mask[0] = 1
            env.reset(mask)
            stats = env.transfer_stats
            if stats["observation_bytes"] <= 0 or stats["steps"] < chunks + 1:
                raise AssertionError(f"missing transfer accounting: {stats}")
            info = env.backend_info()
            print(f"MPS smoke PASS: snapshot={os.path.basename(snapshot)}, "
                  f"n={n}, chunks={chunks}, MB={min(minibatch, n)}, "
                  f"loss={total_loss.item():.6f}, "
                  f"action_copy={stats['action_seconds'] * 1e3:.3f} ms, "
                  f"obs_copy={stats['observation_seconds'] * 1e3:.3f} ms, "
                  f"camera={info['last_camera_ms']:.3f} ms")
        finally:
            env.close()


if __name__ == "__main__":
    main()
