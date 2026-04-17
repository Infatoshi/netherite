"""End-to-end PPO training PoC for the treechop task.

This is intentionally minimal:
    * Single MC instance (B=1).
    * Inline PPO in ~250 lines. No stable-baselines3.
    * Tiny CNN on 80x45 greyscale POV observations.
    * Factored policy: 9 Bernoulli keys + 2D gaussian camera delta.

The point of this script is not to reach human-level treechop -- it is to
verify the full pipeline end-to-end: Java TaskReward fires -> shmem reward
block -> Python step -> PPO update -> new action distribution -> Java side.

Usage:
    uv run env/train_treechop.py --iterations 40 --horizon 128

Artifacts:
    recordings/train_treechop_<timestamp>/metrics.jsonl
    recordings/train_treechop_<timestamp>/policy.pt
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.distributions import Bernoulli, Independent, Normal

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from config import NetheriteConfig  # noqa: E402
from launcher import Launcher  # noqa: E402
from netherite_env import NetheriteEnv  # noqa: E402

ROOT = HERE.parent

KEY_NAMES = [
    "forward",
    "back",
    "left",
    "right",
    "jump",
    "sneak",
    "sprint",
    "attack",
    "use",
]


def preprocess(pov: np.ndarray, out_w: int = 80, out_h: int = 45) -> np.ndarray:
    """RGB uint8 HxWx3 -> greyscale float32 HxW in [0, 1]. Nearest-neighbour."""
    h, w = pov.shape[:2]
    if (h, w) != (out_h, out_w):
        y_idx = np.linspace(0, h - 1, out_h).astype(np.int32)
        x_idx = np.linspace(0, w - 1, out_w).astype(np.int32)
        pov = pov[y_idx][:, x_idx, :]
    grey = pov.astype(np.float32).mean(axis=-1) / 255.0
    return grey


class Policy(nn.Module):
    """Tiny CNN + MLP head. ~40k params. Outputs key logits and camera gaussian."""

    def __init__(self, in_h: int = 45, in_w: int = 80):
        super().__init__()
        self.backbone = nn.Sequential(
            nn.Conv2d(1, 16, kernel_size=5, stride=2, padding=2),  # 45x80 -> 23x40
            nn.ReLU(),
            nn.Conv2d(16, 32, kernel_size=3, stride=2, padding=1),  # -> 12x20
            nn.ReLU(),
            nn.Conv2d(32, 32, kernel_size=3, stride=2, padding=1),  # -> 6x10
            nn.ReLU(),
            nn.Flatten(),
        )
        feat_dim = 32 * 6 * 10
        self.trunk = nn.Sequential(nn.Linear(feat_dim, 128), nn.ReLU())
        self.key_head = nn.Linear(128, 9)
        self.camera_mean = nn.Linear(128, 2)
        self.camera_logstd = nn.Parameter(torch.zeros(2))
        self.value_head = nn.Linear(128, 1)

        # Modest bias toward attacking so exploration has a chance of hitting a log.
        with torch.no_grad():
            self.key_head.bias.fill_(-1.0)
            self.key_head.bias[7] = 0.5  # attack
            self.key_head.bias[0] = -0.2  # forward

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, ...]:
        # x: (B, 1, H, W)
        h = self.trunk(self.backbone(x))
        key_logits = self.key_head(h)
        cam_mean = torch.tanh(self.camera_mean(h)) * 30.0  # +-30 deg max per step
        cam_std = torch.exp(self.camera_logstd.clamp(-4.0, 1.0)).expand_as(cam_mean)
        value = self.value_head(h).squeeze(-1)
        return key_logits, cam_mean, cam_std, value

    @staticmethod
    def distribution(key_logits, cam_mean, cam_std):
        key_dist = Independent(Bernoulli(logits=key_logits), 1)
        cam_dist = Independent(Normal(cam_mean, cam_std), 1)
        return key_dist, cam_dist


def build_action(keys: np.ndarray, camera: np.ndarray) -> dict:
    action = {name: int(keys[i]) for i, name in enumerate(KEY_NAMES)}
    action["camera"] = np.clip(camera.astype(np.float32), -120.0, 120.0)
    return action


@dataclass
class RolloutBuffer:
    obs: list = None
    keys: list = None
    cam: list = None
    logp: list = None
    reward: list = None
    done: list = None
    value: list = None

    def __post_init__(self):
        self.obs = []
        self.keys = []
        self.cam = []
        self.logp = []
        self.reward = []
        self.done = []
        self.value = []

    def add(self, obs, keys, cam, logp, reward, done, value):
        self.obs.append(obs)
        self.keys.append(keys)
        self.cam.append(cam)
        self.logp.append(logp)
        self.reward.append(reward)
        self.done.append(done)
        self.value.append(value)

    def as_tensors(self, device):
        return {
            "obs": torch.as_tensor(np.stack(self.obs), device=device).unsqueeze(1),
            "keys": torch.as_tensor(np.stack(self.keys), dtype=torch.float32, device=device),
            "cam": torch.as_tensor(np.stack(self.cam), dtype=torch.float32, device=device),
            "logp": torch.as_tensor(np.asarray(self.logp), dtype=torch.float32, device=device),
            "reward": np.asarray(self.reward, dtype=np.float32),
            "done": np.asarray(self.done, dtype=np.float32),
            "value": np.asarray(self.value, dtype=np.float32),
        }


def compute_gae(rewards, values, dones, last_value, gamma=0.99, lam=0.95):
    T = len(rewards)
    adv = np.zeros(T, dtype=np.float32)
    lastgae = 0.0
    next_value = last_value
    for t in reversed(range(T)):
        non_terminal = 1.0 - dones[t]
        delta = rewards[t] + gamma * next_value * non_terminal - values[t]
        lastgae = delta + gamma * lam * non_terminal * lastgae
        adv[t] = lastgae
        next_value = values[t]
    returns = adv + values
    return adv, returns


def ppo_update(policy, optim, batch, clip=0.2, vf_coef=0.5, ent_coef=0.005, epochs=4, minibatch=64):
    obs = batch["obs"]
    keys = batch["keys"]
    cam = batch["cam"]
    old_logp = batch["logp"]
    adv_t = batch["adv"]
    ret_t = batch["ret"]

    N = obs.shape[0]
    idx = np.arange(N)
    metrics = {"pol_loss": 0.0, "val_loss": 0.0, "entropy": 0.0, "approx_kl": 0.0}
    steps = 0

    for _ in range(epochs):
        np.random.shuffle(idx)
        for start in range(0, N, minibatch):
            mb = idx[start:start + minibatch]
            mb_t = torch.as_tensor(mb, device=obs.device)
            b_obs = obs[mb_t]
            b_keys = keys[mb_t]
            b_cam = cam[mb_t]
            b_old_logp = old_logp[mb_t]
            b_adv = adv_t[mb_t]
            b_ret = ret_t[mb_t]

            key_logits, cam_mean, cam_std, value = policy(b_obs)
            key_dist, cam_dist = Policy.distribution(key_logits, cam_mean, cam_std)
            new_logp = key_dist.log_prob(b_keys) + cam_dist.log_prob(b_cam)
            ratio = torch.exp(new_logp - b_old_logp)
            surr1 = ratio * b_adv
            surr2 = torch.clamp(ratio, 1.0 - clip, 1.0 + clip) * b_adv
            pol_loss = -torch.min(surr1, surr2).mean()
            val_loss = 0.5 * (value - b_ret).pow(2).mean()
            entropy = (key_dist.entropy() + cam_dist.entropy()).mean()
            loss = pol_loss + vf_coef * val_loss - ent_coef * entropy

            optim.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(policy.parameters(), 0.5)
            optim.step()

            with torch.no_grad():
                approx_kl = (b_old_logp - new_logp).mean().item()
            metrics["pol_loss"] += pol_loss.item()
            metrics["val_loss"] += val_loss.item()
            metrics["entropy"] += entropy.item()
            metrics["approx_kl"] += approx_kl
            steps += 1

    return {k: v / max(1, steps) for k, v in metrics.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iterations", type=int, default=20)
    ap.add_argument("--horizon", type=int, default=128)
    ap.add_argument("--seed", type=int, default=12345)
    ap.add_argument("--width", type=int, default=160)
    ap.add_argument("--height", type=int, default=90)
    ap.add_argument("--render-distance", type=int, default=4)
    ap.add_argument("--simulation-distance", type=int, default=5)
    ap.add_argument("--max-fps", type=int, default=9999)
    ap.add_argument("--max-episode-steps", type=int, default=1000)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--device", default="cpu")
    ap.add_argument("--java-home", default=None)
    ap.add_argument("--ready-timeout", type=float, default=180.0)
    ap.add_argument("--step-timeout", type=float, default=30.0)
    ap.add_argument("--out-dir", default=None)
    args = ap.parse_args()

    java_home = args.java_home
    if java_home is None:
        if sys.platform == "darwin":
            java_home = "/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"
        else:
            java_home = "/usr/lib/jvm/java-21-openjdk-amd64"
    if not Path(java_home).exists():
        fallback = os.environ.get("JAVA_HOME")
        if fallback and Path(fallback).exists():
            java_home = fallback
        else:
            raise RuntimeError(f"JAVA_HOME not found at {java_home}")

    stamp = time.strftime("%Y%m%d_%H%M%S")
    out_dir = Path(args.out_dir) if args.out_dir else (ROOT / "recordings" / f"train_treechop_{stamp}")
    out_dir.mkdir(parents=True, exist_ok=True)
    metrics_path = out_dir / "metrics.jsonl"
    policy_path = out_dir / "policy.pt"
    print(f"train_treechop: writing artefacts to {out_dir}", flush=True)

    cfg = NetheriteConfig(
        instance_id=0,
        seed=args.seed,
        width=args.width,
        height=args.height,
        render_distance=args.render_distance,
        simulation_distance=args.simulation_distance,
        max_fps=args.max_fps,
        uncapped=True,
        rl=True,
        headless=False,
        task="treechop",
        max_episode_steps=args.max_episode_steps,
        do_mob_spawning=False,
        do_daylight_cycle=True,
        use_semaphore=True,
        step_ticks=1,
        java_home=java_home,
    )

    launcher = Launcher(ROOT)
    launcher.cleanup_shmem()
    launcher.cleanup_instance_run_dirs([cfg.instance_id])
    launcher.launch([cfg])
    try:
        if not launcher.wait_all_ready(timeout=args.ready_timeout):
            raise RuntimeError("MC instance failed to come up")

        env = NetheriteEnv(config=cfg, timeout=args.step_timeout)
        obs, info = env.reset()
        print("train_treechop: env reset ok", flush=True)

        # Wait for start latch release
        try:
            env.release_start_latch()
        except Exception as e:
            print(f"train_treechop: release_start_latch warning: {e}", flush=True)

        # Warmup steps to flush any startup-tick weirdness
        noop = {name: 0 for name in KEY_NAMES}
        noop["camera"] = np.zeros(2, dtype=np.float32)
        for _ in range(10):
            obs, _, _, _, _ = env.step(noop)

        device = torch.device(args.device)
        policy = Policy().to(device)
        optim = torch.optim.Adam(policy.parameters(), lr=args.lr)

        pov = preprocess(obs["pov"])
        ep_reward = 0.0
        ep_len = 0
        ep_logs = 0

        total_steps = 0
        iter_times = []
        for it in range(args.iterations):
            t_iter = time.monotonic()
            rollout = RolloutBuffer()
            ep_rewards, ep_lens, ep_log_counts, ep_terminations = [], [], [], []
            cur_ep_reward = ep_reward
            cur_ep_len = ep_len
            cur_ep_logs = ep_logs
            last_step_reward = 0.0

            for t in range(args.horizon):
                obs_t = torch.as_tensor(pov, device=device).unsqueeze(0).unsqueeze(0)
                with torch.no_grad():
                    key_logits, cam_mean, cam_std, value = policy(obs_t)
                    key_dist, cam_dist = Policy.distribution(key_logits, cam_mean, cam_std)
                    keys = key_dist.sample()
                    cam = cam_dist.sample()
                    logp = (key_dist.log_prob(keys) + cam_dist.log_prob(cam)).squeeze(0).item()
                    value = value.squeeze(0).item()

                keys_np = keys.squeeze(0).cpu().numpy()
                cam_np = cam.squeeze(0).cpu().numpy()
                action = build_action(keys_np, cam_np)
                obs2, reward, terminated, truncated, info = env.step(action)
                done = bool(terminated) or bool(truncated)
                total_steps += 1

                rollout.add(pov, keys_np, cam_np, logp, float(reward), float(done), value)

                cur_ep_reward += float(reward)
                cur_ep_len += 1
                cur_ep_logs = int(info.get("logs_broken", cur_ep_logs))
                last_step_reward = float(reward)

                if done:
                    ep_rewards.append(cur_ep_reward)
                    ep_lens.append(cur_ep_len)
                    ep_log_counts.append(cur_ep_logs)
                    ep_terminations.append("died" if terminated else "truncated")
                    # Reset accumulators for the next episode.
                    cur_ep_reward = 0.0
                    cur_ep_len = 0
                    cur_ep_logs = 0

                pov = preprocess(obs2["pov"])

            # Stash partial-episode state for the next iteration.
            ep_reward = cur_ep_reward
            ep_len = cur_ep_len
            ep_logs = cur_ep_logs

            # Bootstrap from the final obs.
            with torch.no_grad():
                obs_t = torch.as_tensor(pov, device=device).unsqueeze(0).unsqueeze(0)
                _, _, _, last_value = policy(obs_t)
                last_value = last_value.squeeze(0).item()

            tensors = rollout.as_tensors(device)
            rewards = tensors["reward"]
            values = tensors["value"]
            dones = tensors["done"]
            adv, ret = compute_gae(rewards, values, dones, last_value)
            adv = (adv - adv.mean()) / (adv.std() + 1e-8)
            tensors["adv"] = torch.as_tensor(adv, device=device)
            tensors["ret"] = torch.as_tensor(ret, device=device)

            metrics = ppo_update(policy, optim, tensors)

            dt = time.monotonic() - t_iter
            iter_times.append(dt)
            sps = args.horizon / max(dt, 1e-6)
            mean_r = float(np.mean(ep_rewards)) if ep_rewards else 0.0
            mean_len = float(np.mean(ep_lens)) if ep_lens else 0.0
            mean_logs = float(np.mean(ep_log_counts)) if ep_log_counts else 0.0

            row = {
                "iter": it,
                "total_steps": total_steps,
                "dt_s": dt,
                "sps": sps,
                "episodes": len(ep_rewards),
                "mean_ep_reward": mean_r,
                "mean_ep_len": mean_len,
                "mean_logs_broken": mean_logs,
                "last_step_reward": last_step_reward,
                **metrics,
            }
            with metrics_path.open("a") as f:
                f.write(json.dumps(row) + "\n")

            print(
                f"iter {it:>3} sps={sps:>6.1f} eps={len(ep_rewards):>2} "
                f"mean_R={mean_r:>+6.3f} mean_logs={mean_logs:>4.1f} "
                f"entropy={metrics['entropy']:+.3f} kl={metrics['approx_kl']:+.4f} "
                f"pol_loss={metrics['pol_loss']:+.4f} val_loss={metrics['val_loss']:+.4f}",
                flush=True,
            )

        torch.save({"policy": policy.state_dict()}, policy_path)
        print(f"train_treechop: saved policy to {policy_path}", flush=True)
        env.close()
    finally:
        launcher.stop_all()


if __name__ == "__main__":
    main()
