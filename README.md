# Netherite

Python-controlled Minecraft RL environment. MC 1.20.1 + Fabric + Lithium/Sodium. Zero manual GUI interaction -- boot straight into a world with every setting configurable from Python.

## Prerequisites

- **Java 17+** (21 recommended). Check with `java -version`.
- **Python 3.11+** via [uv](https://docs.astral.sh/uv/).
- **Minecraft ownership is required.** Netherite uses Fabric Loom which downloads MC assets from Mojang's servers during build. This is permitted under Mojang's EULA for development. We do not redistribute Minecraft.

## Quick Start

```bash
# Clone
git clone <repo-url> && cd netherite

# Build (downloads MC 1.20.1 + Fabric + dependencies on first run)
JAVA_HOME=/path/to/jdk21 ./gradlew build

# Launch MC with the mod (opens a window, auto-creates world)
JAVA_HOME=/path/to/jdk21 ./gradlew runClient

# Optional: add Sodium + Lithium for performance
mkdir -p run/mods
# Download from https://modrinth.com/mod/sodium (1.20.1 Fabric)
# Download from https://modrinth.com/mod/lithium (1.20.1 Fabric)
# Drop jars into run/mods/
```

## Python Environment

```bash
cd netherite
uv venv && uv pip install numpy gymnasium pillow

# With MC running, read a frame:
uv run python -c "
import sys; sys.path.insert(0, 'env')
from config import NetheriteConfig
from netherite_env import NetheriteEnv
env = NetheriteEnv()
obs, _ = env.reset()
print(f'Frame: {obs[\"pov\"].shape}, pos: {obs[\"position\"]}')
env.close()
"
```

## Configuration

Every setting is controlled via `-Dnetherite.*` JVM flags, which map to `NetheriteConfig` fields in Python.

```python
from config import NetheriteConfig

cfg = NetheriteConfig(
    seed=42,
    render_distance=4,
    simulation_distance=4,
    graphics="fast",
    particles="minimal",
    clouds="off",
    smooth_lighting=False,
    do_mob_spawning=False,
    do_daylight_cycle=False,
    rl=True,  # auto-dismiss menus, disable pause
)

# Launch with these settings:
# ./gradlew runClient <cfg.to_gradle_args()>
```

### Key settings

| Setting | Default | Notes |
|---------|---------|-------|
| `seed` | 12345 | World seed |
| `render_distance` | 8 | Chunks. Lower = faster. |
| `simulation_distance` | 5 | Chunks. Lower = faster. |
| `graphics` | fast | fast/fancy/fabulous |
| `particles` | minimal | all/decreased/minimal |
| `clouds` | off | off/fast/fancy |
| `max_fps` | 60 | FPS cap |
| `rl` | false | True = auto-dismiss menus, suppress toasts |
| `game_mode` | survival | survival/creative/adventure/spectator |
| `do_mob_spawning` | false | Game rule |
| `do_daylight_cycle` | false | Game rule |

See `env/config.py` for the full list.

## RL Mode

Pass `-Dnetherite.rl=true` to enable training mode:
- Auto-dismisses pause menu and other screens
- Suppresses toast notifications and tutorials
- Disables pause on lost focus

Without it, MC behaves normally (ESC works, GUI is interactive).

```bash
./gradlew runClient -Dnetherite.rl=true -Dnetherite.seed=42
```

## Headless (Linux / Training)

On a Linux server without a display, use Xvfb:

```bash
Xvfb :99 -screen 0 854x480x24 &
export DISPLAY=:99

./gradlew runClient \
  -Dnetherite.rl=true \
  -Dnetherite.render_distance=4 \
  -Dnetherite.seed=42
```

Resolution is controlled by the Xvfb screen size.

## Multi-Instance

Each instance needs a unique ID and its own shmem buffers:

```bash
# Instance 0
./gradlew runClient -Dnetherite.instance_id=0 -Dnetherite.seed=100 &

# Instance 1
./gradlew runClient -Dnetherite.instance_id=1 -Dnetherite.seed=200 &
```

Or use the Python launcher:

```python
from config import NetheriteConfig
from launcher import Launcher

launcher = Launcher("/path/to/netherite")
configs = [NetheriteConfig(instance_id=i, seed=i*100, rl=True) for i in range(4)]
launcher.launch(configs)
launcher.wait_all_ready()
```

## Architecture

```
Python (netherite_env.py)
    |
    | shmem: /tmp/netherite_obs_{id}_{A,B}    (RGBA pixels, PBO double-buffered)
    | shmem: /tmp/netherite_state_{id}         (pos/health/inventory/entities)
    | shmem: /tmp/netherite_action_{id}        (movement/camera/interact)
    |
MC 1.20.1 (Java, Fabric)
    ├── Sodium         -- batched indirect rendering (optional)
    ├── Lithium        -- optimized game logic (optional)
    └── netherite mod  -- PBO readback, action injection, state export
```

## Shmem Protocol

All little-endian. Ready flag at offset 12 written last.

- **Obs** (`netherite_obs_{id}_{A,B}`): 8MB, 16B header + RGBA pixels
- **State** (`netherite_state_{id}`): 64KB, player pos/health/food/inventory/entities
- **Action** (`netherite_action_{id}`): 4KB, movement keys + camera delta

See `SPEC.md` for byte-level layout.

## Machines

- **macOS** (dev): Window opens, Sodium may not load (LWJGL compat). Lithium works.
- **Linux** (training): Xvfb + Sodium + Lithium. Sodium requires GPU with OpenGL 4.6.
