# netherite

Home: Anvil-primary - `anvil:~/dev/netherite`. Build/run here; Mac is control
plane / Moonlight only.

**Agent entry is `AGENTS.md`.** Read that first. This file exists so Claude Code
auto-load has a project file; it does not duplicate the full map.

| Topic | File |
|-------|------|
| Commands, gotchas, doc map | `AGENTS.md` |
| First-clone bootstrap | `docs/BOOTSTRAP.md` |
| Play / VNC / qrl / sweep | `docs/RUNBOOK.md` |
| Product gates | `docs/GATES.md` |
| What's cut / pinned / open | `docs/SCOPE.md` |
| History | `docs/DEVLOG.md` |
| Fidelity | `magma/VERIFY.md` |
| Why a pixel differs | `pxdiff.py` (AGENTS.md "Pixel investigation") |
| Isolated worktree for a delegate | `scripts/agent_worktree.sh NAME` |

Fanning out agents: give each one `scripts/agent_worktree.sh` (a bare `git
worktree add` cannot build or replay - tapes and generated headers are
gitignored), and read AGENTS.md "Pixel investigation" for the two ways a
parallel measurement lies.

Anvil is headless: demos scp to Mac; human play via Moonlight or mcwindow;
agent stack is Xvfb `:1` (`java/start_vnc_client.sh`). One qrl port 25575 owner.
