# py/ - pybind11 gym.Env smoke (Wave 14)

Minimal build for `py_gym_env_smoke` schema validation. Requires pybind11 for the same Python
UV uses (`uv pip install pybind11`).

```bash
uv pip install pybind11
PYBIND11_DIR=$(uv run --no-project python -c "import pybind11; print(pybind11.get_cmake_dir())")
PY=$(uv run --no-project python -c "import sys; print(sys.executable)")
cd py
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -Dpybind11_DIR="$PYBIND11_DIR" -DPython3_EXECUTABLE="$PY"
cmake --build build
```

Produces `build/mcsim_gym*.so`. The oracle picks it up automatically:

```bash
uv run --no-project python oracle/py_gym_env_smoke.py
uv run --no-project python oracle/runner.py py_gym_env_smoke
```

Python usage:

```python
import mcsim_gym

env = mcsim_gym.McSimEnv()
obs = env.reset(12345)
obs, reward, done, info = env.step({"forward": 1, "jump": 1})
```

Obs dict keys align with qrl mod subset: `x/y/z`, `yaw/pitch`, `vx/vy/vz`,
`on_ground`, `look{type,bx,by,bz}`, `tick`, `combined_hash`.

If pybind11 is unavailable, the CPU oracle still PASSes on determinism +
CPU==CUDA via `oracle/py_gym_env_smoke.py` (skips schema bind check).
