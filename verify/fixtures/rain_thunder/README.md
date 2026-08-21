# rain_thunder oracle tape

Missing evidence for OPEN_DIVERGENCES "The lightmap ignores rain and thunder".
Not a C renderer fix. Re-derive (anvil, exclusive `/tmp/qrl_25575.lock`):

```bash
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
export UV_CACHE_DIR=$HOME/.cache/uv TMPDIR=$HOME/dev/nw/.tmp
bash verify/scenarios/run_scenario.sh verify/scenarios/rain_thunder.yaml
```

`run_scenario.sh` then replays `--cpu --report`; that step needs `magma_game`
and is not the oracle-evidence gate. The tape is archived before replay.
Check the header and a mid-tape row before grinding:

```bash
python3 -c '
import json,sys
p=sys.argv[1]
ls=open(p).read().splitlines()
h=json.loads(ls[0]); r=json.loads(ls[len(ls)//2])
print("header", h.get("rain_strength"), h.get("thunder_strength"))
print("mid t", r.get("t"), "rain", r.get("rain"), "thunder", r.get("thunder"))
' verify/tapes/scenario_rain_thunder_<UTC>.jsonl
```

Recorded 2026-08-21 on anvil, git `aa43667`, scenario `rain_thunder.yaml`
(`/weather thunder 1000000`, `doWeatherCycle false`, 160-tick strength ramp):

| path | sha256 | bytes |
|------|--------|-------|
| `verify/tapes/scenario_rain_thunder_20260821T093435Z.jsonl` | `37a7d62ee9cca0daa418fcc859101b1e94cd2c4538b6cdf2a0ac29951a30d972` | 90528 |
| `verify/tapes/scenario_rain_thunder_20260821T093435Z.meta.json` | `444b27bfab9738d134bc39bc177bdcc5ebe829635f527e70bfc43719792688ed` | 2314 |
| `verify/tapes/scenario_rain_thunder_20260821T093435Z_world/` (anvil) | `bf6bb57d4accf15cf07357a4a4ab759295397d5c8dd39097e6e5391e50fc8673` (sorted file sha256s) | 6448975 |
| `verify/tapes/scenario_rain_thunder_20260821T093435Z_frames/` (anvil) | goldens, 21 pngs, 2.5 MiB | 2557192 |

Header `rain_strength=1.0` `thunder_strength=1.0`. All 209 tick rows have
`rain=1.0` `thunder=1.0`. World snapshot is the recstart Anvil save
(level.dat + region/). Frames are gitignored like other tape goldens.
The jsonl is force-added despite `verify/tapes/*`; world+frames stay on
anvil at the paths above (6.4 MiB region files, not a git fixture).
