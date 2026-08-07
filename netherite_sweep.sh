#!/usr/bin/env bash
# netherite_sweep.sh - one-command verification pyramid for netherite (gate 4 / ops).
#
#   bash netherite_sweep.sh --quick          # builds + unit batteries + blaze CPU gate + vec-env (<10 min)
#   bash netherite_sweep.sh --full           # everything incl. CUDA default/chain/mixed gates,
#                                            # t0 throughput pin check, canonical tape replay
#   bash netherite_sweep.sh --full --gpu 0   # device index for the blaze CUDA steps (default 0)
#
# Every step is independent: it runs an EXISTING gate (make target / script,
# never reimplemented here), gets its own timeout and log, and reports
# [PASS]/[FAIL]/[SKIP]. GPU steps preflight nvidia-smi and SKIP (not fail)
# when the device is busy (>50% util) - the box is shared. Steps whose input
# artifacts are missing (snapshots, tapes, prefixes) SKIP with a reason.
# Exit is nonzero iff any step FAILs.
#
# See docs/GATES.md for the product gates this sweep certifies.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAGMA="$ROOT/magma"
BLAZE="$ROOT/blaze"
CANON_TAPE="$ROOT/verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl"
SNAPS_DIR="$ROOT/blaze/rl/out/snaps"

MODE=quick
GPU_IDX=0
while [ $# -gt 0 ]; do
	case "$1" in
	--quick) MODE=quick ;;
	--full) MODE=full ;;
	--gpu)
		shift
		GPU_IDX="${1:?--gpu needs an index}"
		;;
	*)
		echo "usage: $0 [--quick|--full] [--gpu IDX]" >&2
		exit 2
		;;
	esac
	shift
done

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
LOGDIR="${NETHERITE_LOG_DIR:-/tmp/netherite_sweep_$STAMP}"
mkdir -p "$LOGDIR"

NAMES=()
STATUSES=()
DETAILS=()
SECS=()
NFAIL=0

note() { printf '%s\n' "$*"; }

record() { # name status detail secs
	NAMES+=("$1")
	STATUSES+=("$2")
	DETAILS+=("$3")
	SECS+=("$4")
	case "$2" in
	PASS) printf '[PASS] %-22s (%ss)\n' "$1" "$4" ;;
	SKIP) printf '[SKIP] %-22s %s\n' "$1" "$3" ;;
	FAIL)
		printf '[FAIL] %-22s %s (%ss, log: %s)\n' "$1" "$3" "$4" "$LOGDIR/$1.log"
		NFAIL=$((NFAIL + 1))
		;;
	esac
}

skip() { record "$1" SKIP "$2" 0; }

# run_step NAME TIMEOUT_S WORKDIR CMD...
run_step() {
	local name="$1" tmo="$2" dir="$3"
	shift 3
	local log="$LOGDIR/$name.log"
	local t0 t1 rc
	t0=$(date +%s)
	(cd "$dir" && timeout -k 15 "$tmo" "$@") >"$log" 2>&1
	rc=$?
	t1=$(date +%s)
	if [ "$rc" -eq 0 ]; then
		record "$name" PASS "" $((t1 - t0))
	elif [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
		record "$name" FAIL "timeout after ${tmo}s" $((t1 - t0))
	else
		record "$name" FAIL "rc=$rc: $(tail -1 "$log" | cut -c1-90)" $((t1 - t0))
	fi
}

# gpu_busy IDX -> 0 (busy/unknown reason echoed) or 1 (idle)
gpu_busy() {
	local idx="$1" util used
	if ! command -v nvidia-smi >/dev/null 2>&1; then
		echo "nvidia-smi not found"
		return 0
	fi
	util=$(nvidia-smi --id="$idx" --query-gpu=utilization.gpu \
		--format=csv,noheader,nounits 2>/dev/null | head -1 | tr -d ' ')
	if [ -z "$util" ]; then
		echo "GPU $idx not visible to nvidia-smi"
		return 0
	fi
	used=$(nvidia-smi --id="$idx" --query-compute-apps=used_memory \
		--format=csv,noheader,nounits 2>/dev/null | \
		awk '{sum += $1} END {print sum + 0}')
	if [ "$used" -gt 4096 ]; then
		echo "GPU $idx shared (${used}MiB held by compute processes)"
		return 0
	fi
	if [ "$util" -gt 50 ]; then
		echo "GPU $idx busy (${util}% util)"
		return 0
	fi
	return 1
}

gpu_sm() { # compute_cap -> sm string (e.g. 12.0 -> sm_120)
	nvidia-smi --id="$1" --query-gpu=compute_cap --format=csv,noheader 2>/dev/null |
		head -1 | tr -d ' .' | sed 's/^/sm_/'
}

gpu_mem_free_mib() { # free MiB on GPU $1 ("0" on query failure, safe for compare)
	local m
	m=$(nvidia-smi --id="$1" --query-gpu=memory.free \
		--format=csv,noheader,nounits 2>/dev/null | head -1 | tr -d ' ')
	case "$m" in '' | *[!0-9]*) m=0 ;; esac
	echo "$m"
}

note "netherite sweep - mode=$MODE gpu=$GPU_IDX  ($STAMP)"
note "logs: $LOGDIR"
note ""

# ---- builds ----------------------------------------------------------------
# stale-object trap: game/*.o must be cleaned after blaze core header edits.
rm -f "$MAGMA"/game/*.o
run_step build-magma 600 "$MAGMA" make magma_game -j"$(nproc)"
run_step build-blaze-cpu 300 "$MAGMA" make blaze_so blaze_verify

# ---- blaze core: CPU oracle battery (java-derived goldens, cpu path) -------
run_step blaze-oracle-smoke 300 "$BLAZE" \
	env MC_CPU_ONLY=1 uv run --no-project python oracle/runner.py smoke 12345 256
run_step blaze-cpu-trunk 600 "$BLAZE" \
	make verify-cpu-trunk PYTHON="uv run --no-project python"
if [ "$MODE" = full ]; then
	run_step blaze-cpu-physics 900 "$BLAZE" \
		make verify-cpu-physics PYTHON="uv run --no-project python"
	run_step blaze-cpu-tick 900 "$BLAZE" \
		make verify-cpu-tick PYTHON="uv run --no-project python"
fi

# ---- magma: unit goldens + standalone-game smoke --------------------------
run_step magma-verify-harsh 600 "$MAGMA" make verify-harsh
run_step magma-test-config 300 "$MAGMA" bash game/test_config.sh
run_step magma-block-registry 300 "$MAGMA" make test-block-registry
run_step magma-test-launch 300 "$MAGMA" make test-launch
run_step magma-parity-60 900 "$ROOT" bash scripts/test_parity_60.sh

# ---- blaze: batched env gates (CPU) -----------------------------------------
FIRST_SNAP=$(find "$SNAPS_DIR" -maxdepth 1 -name '*.bsnp' 2>/dev/null | sort | head -1)
if [ -z "$FIRST_SNAP" ]; then
	skip blaze-c-smoke "no .bsnp snapshots in blaze/rl/out/snaps (run make_snapshots.py)"
	skip blaze-cpu-gate "no .bsnp snapshots in blaze/rl/out/snaps (run make_snapshots.py)"
else
	run_step blaze-c-smoke 300 "$ROOT" blaze/env/blaze_verify "$FIRST_SNAP" 64 50 4
	if [ "$MODE" = full ]; then
		run_step blaze-cpu-gate 1500 "$ROOT" \
			uv run --no-project --with numpy python blaze/env/verify_cpu.py
	else
		run_step blaze-cpu-gate 600 "$ROOT" \
			uv run --no-project --with numpy python blaze/env/verify_cpu.py --seeds 14,16 --ticks 500
	fi
fi

# ---- rl: vec-env bit-exactness ----------------------------------------------
if [ -f "$ROOT/blaze/rl/out/coal_prefixes.json" ]; then
	run_step rl-vec-env-test 900 "$ROOT" \
		uv run --no-project --with numpy,torch python blaze/rl/test_vec_env.py
else
	skip rl-vec-env-test "blaze/rl/out/coal_prefixes.json missing (PREFIX=1 chain_probe.py)"
fi

# reward module bitwise parity vs the archived inline block (CPU-forced)
run_step rl-reward-parity 300 "$ROOT" \
	env CUDA_VISIBLE_DEVICES="" \
	uv run --no-project --with numpy,torch python blaze/env/test_reward_chain.py

# focused scenario + tape unit gates (no Java/GPU): pixel mild-shift, state
# assertions, scenario materialize/archive, missing-model completeness
run_step state-capsule-selftest 60 "$MAGMA" \
	uv run --no-project python trace/state_capsule.py selftest
run_step verify-unit-gates 600 "$ROOT" \
	uv run --no-project --with pytest --with numpy --with scipy --with pillow \
	--with pyyaml \
	pytest -q verify/trace/test_replay_tape.py \
	verify/scenarios/test_scenario.py

# ---- full-only: CUDA gates + tape replay + RL smoke -------------------------
if [ "$MODE" = full ]; then
	# blaze env CUDA + blaze core CUDA run on --gpu GPU_IDX (arch derived from compute cap)
	BUSY=$(gpu_busy "$GPU_IDX") || BUSY=""
	if [ -n "$BUSY" ]; then
		skip blaze-cuda-trunk "$BUSY"
		skip build-blaze-cuda "$BUSY"
		skip blaze-cuda-gate "$BUSY"
		skip blaze-cuda-chain "$BUSY"
		skip blaze-cuda-mixed "$BUSY"
		skip blaze-t0-bench-pin "$BUSY"
	else
		SM=$(gpu_sm "$GPU_IDX")
		run_step blaze-cuda-trunk 900 "$BLAZE" \
			env CUDA_VISIBLE_DEVICES="$GPU_IDX" make verify-trunk \
			PYTHON="uv run --no-project python" SM="$SM" MC_SM="$SM"
		run_step build-blaze-cuda 600 "$MAGMA" make blaze_cuda_so BLAZE_SM="$SM"
		if [ -z "$FIRST_SNAP" ]; then
			skip blaze-cuda-gate "no .bsnp snapshots in blaze/rl/out/snaps"
			skip blaze-cuda-chain "no .bsnp snapshots in blaze/rl/out/snaps"
			skip blaze-cuda-mixed "no *_t0.bsnp snapshots in blaze/rl/out/snaps"
			skip blaze-t0-bench-pin "no *_t0.bsnp snapshots in blaze/rl/out/snaps"
		else
			run_step blaze-cuda-gate 1500 "$ROOT" \
				env CUDA_VISIBLE_DEVICES="$GPU_IDX" \
				uv run --no-project --with numpy,torch python blaze/env/verify_cuda.py

			# chain gate: 2058-action chain, 64 CUDA lanes byte-exact vs CPU
			if [ ! -f "$ROOT/blaze/rl/out/chain_actions_s10.json" ] ||
				[ ! -f "$SNAPS_DIR/s10_t0.bsnp" ]; then
				skip blaze-cuda-chain "s10_t0.bsnp or chain_actions_s10.json missing"
			else
				run_step blaze-cuda-chain 1200 "$ROOT" \
					env CUDA_VISIBLE_DEVICES="$GPU_IDX" \
					uv run --no-project --with numpy,torch python \
					blaze/env/verify_cuda.py --chain
			fi

			# mixed big-N FULL-action gate on the t0 snapshots
			# (N=2048 region pool ~17GiB: preflight free VRAM, shared box)
			MEMFREE=$(gpu_mem_free_mib "$GPU_IDX")
			if [ ! -f "$SNAPS_DIR/s10_t0.bsnp" ]; then
				skip blaze-cuda-mixed "no *_t0.bsnp snapshots in blaze/rl/out/snaps"
			elif [ "$MEMFREE" -lt 18000 ]; then
				skip blaze-cuda-mixed \
					"GPU $GPU_IDX free ${MEMFREE}MiB < 18000MiB (N=2048 pool)"
			else
				run_step blaze-cuda-mixed 1800 "$ROOT" \
					env CUDA_VISIBLE_DEVICES="$GPU_IDX" \
					uv run --no-project --with numpy,torch python \
					blaze/env/verify_cuda.py --mixed --n 2048
			fi

			# gate-3 throughput pin: >=1.0M env-ticks/s full-feature t0 at
			# N=9216 (the docs/GATES.md measurement point; ~77GiB pool, so
			# GPU0-class only - SKIP below 80GiB free rather than OOM into a
			# neighbor session)
			MEMFREE=$(gpu_mem_free_mib "$GPU_IDX")
			if [ "$MEMFREE" -lt 80000 ]; then
				skip blaze-t0-bench-pin \
					"GPU $GPU_IDX free ${MEMFREE}MiB < 80000MiB (N=9216 pool)"
			else
				BENCH_LOG="$LOGDIR/blaze-t0-bench-pin.log"
				b0=$(date +%s)
				(cd "$ROOT" && env CUDA_VISIBLE_DEVICES="$GPU_IDX" \
					timeout -k 15 900 \
					uv run --no-project --with numpy,torch python \
					blaze/env/verify_cuda.py --bench --t0 --n 9216) \
					>"$BENCH_LOG" 2>&1
				brc=$?
				b1=$(date +%s)
				MVAL=$(sed -n 's/.*N=[0-9]*: \([0-9.]*\)M env-ticks.*/\1/p' \
					"$BENCH_LOG" | tail -1)
				if [ "$brc" -ne 0 ]; then
					record blaze-t0-bench-pin FAIL \
						"rc=$brc: $(tail -1 "$BENCH_LOG" | cut -c1-60)" \
						$((b1 - b0))
				elif [ -z "$MVAL" ]; then
					record blaze-t0-bench-pin FAIL "no throughput line in log" \
						$((b1 - b0))
				elif awk "BEGIN{exit !($MVAL >= 1.0)}"; then
					record blaze-t0-bench-pin PASS \
						"${MVAL}M env-ticks/s at N=9216 (pin >=1.0M)" \
						$((b1 - b0))
				else
					record blaze-t0-bench-pin FAIL \
						"${MVAL}M env-ticks/s < 1.0M pin at N=9216" $((b1 - b0))
				fi
			fi
		fi
	fi

	# canonical tape replay + raster parity are pinned to GPU1 by design
	# (magma_game_cuda is built sm_86; replay_tape.py defaults to GPU1)
	BUSY1=$(gpu_busy 1) || BUSY1=""
	# NVML utilization is a trailing sample. When the Blaze benchmark above ran
	# on GPU1, its process can be gone while one 99% sample remains and makes the
	# sweep skip its own raster gates. Give that sample a short chance to clear;
	# a genuinely shared workload remains busy and is still left untouched.
	if [ -n "$BUSY1" ] && [ "$GPU_IDX" = 1 ]; then
		for _attempt in 1 2 3 4 5; do
			sleep 2
			BUSY1=$(gpu_busy 1) || BUSY1=""
			[ -z "$BUSY1" ] && break
		done
	fi
	if [ -n "$BUSY1" ]; then
		skip build-magma-cuda "$BUSY1 (GPU1 pinned)"
		skip raster-parity "$BUSY1 (GPU1 pinned)"
		skip tape-replay-canonical "$BUSY1 (GPU1 pinned)"
	else
		run_step build-magma-cuda 600 "$MAGMA" make magma_game_cuda -j"$(nproc)"
		run_step raster-parity 600 "$MAGMA" make test-raster-parity
		if [ -f "$CANON_TAPE" ]; then
			run_step tape-replay-canonical 1200 "$ROOT/verify/trace" \
				uv run --no-project --with numpy,scipy,pillow,nbt python \
				replay_tape.py "$CANON_TAPE" --report
		else
			skip tape-replay-canonical "canonical tape missing: $CANON_TAPE"
		fi
	fi

	# RL training smoke: tiny ppo_coal run (2 episodes) just proves the loop turns
	if [ -f "$ROOT/blaze/rl/out/coal_prefixes.json" ]; then
		run_step rl-ppo-smoke 900 "$ROOT" \
			env N_EPISODES=2 EP_LEN=40 CURR_TICKS=10 \
			uv run --no-project --with numpy,torch,matplotlib python blaze/rl/ppo_coal.py
	else
		skip rl-ppo-smoke "blaze/rl/out/coal_prefixes.json missing"
	fi
fi

# ---- summary ----------------------------------------------------------------
note ""
note "==================== netherite sweep summary ($MODE) ===================="
printf '%-24s %-6s %-5s %s\n' STEP STATUS SECS DETAIL
NSKIP=0
for i in "${!NAMES[@]}"; do
	printf '%-24s %-6s %-5s %s\n' "${NAMES[$i]}" "${STATUSES[$i]}" "${SECS[$i]}" "${DETAILS[$i]}"
	case "${STATUSES[$i]}" in
	SKIP) NSKIP=$((NSKIP + 1)) ;;
	esac
done
note "=========================================================================="
if [ "$NFAIL" -gt 0 ]; then
	note "RESULT: $NFAIL step(s) FAILED (logs in $LOGDIR)"
	exit 1
fi
# SKIPs (GPU busy, missing artifacts) are not failures, but the summary must
# not call a partially-run pyramid "green" - that hid skipped CUDA gates.
if [ "$NSKIP" -gt 0 ]; then
	note "RESULT: PASS with $NSKIP SKIP(s) (not fully green; see SKIPs above)"
	exit 0
fi
note "RESULT: green (all steps PASS, no SKIPs)"
