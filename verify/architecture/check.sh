#!/usr/bin/env bash
# Run under a machine lease. Validation deliberately excludes throughput claims.
set -euo pipefail
cd "$(dirname "$0")/../.."
base=out/verify/architecture
out=${1:?usage: check.sh NEW_OUTPUT_DIRECTORY}
[[ ! -e $out ]] || { echo "output already exists" >&2; exit 2; }
mkdir -p "$out"
for scene in no_liquid placement fluid_spread mobs_det death boundary; do
 fixture=$base/fixtures/$scene-64.bsnp
 [[ $scene != death && $scene != boundary ]] || fixture=$base/fixtures/$scene.bsnp
 mobs=0; [[ $scene != mobs_det ]] || mobs=1
 args=(--fixture "$fixture" --n 2 --steps 12 --warmup 2 --repeat 4 --reset-every 3 --mobs "$mobs" --policy 0 --threads 2)
 "$base/measure" --mode cpu "${args[@]}" --record "$out/$scene.bin" > "$out/$scene-cpu.log" 2>&1
 for variant in hybrid original fine graph; do
  case $variant in
   hybrid) backend=(--mode hybrid --delta 1);;
   original) backend=(--mode cuda --lib "$base/baseline_gpu/blaze_cuda_measure.so");;
   fine|graph)
    graph=0; [[ $variant != graph ]] || graph=1
    backend=(--mode cuda --lib out/blaze/env/blaze_cuda.so --split 2 --grain 1 --cubin "$base/cubin/blaze_cuda.1.sm_86.cubin" --fine "$base/fine" --graph "$graph");;
  esac
  "$base/measure" "${backend[@]}" "${args[@]}" --reference "$out/$scene.bin" > "$out/$scene-$variant.log" 2>&1
  # Tick counts and reset counts are measured separately from the parity record.
  awk '/^ACCOUNTING /{for(i=2;i<=NF;i++){split($i,a,"=");if(a[1]=="actual_ticks"||a[1]=="reset_lanes"||a[1]=="reset_batches")print a[1],a[2]}}' "$out/$scene-cpu.log" > "$out/counts-cpu"
  awk '/^ACCOUNTING /{for(i=2;i<=NF;i++){split($i,a,"=");if(a[1]=="actual_ticks"||a[1]=="reset_lanes"||a[1]=="reset_batches")print a[1],a[2]}}' "$out/$scene-$variant.log" > "$out/counts-other"
  [[ $(wc -l < "$out/counts-cpu") == 3 ]] && cmp "$out/counts-cpu" "$out/counts-other"
  printf 'PASS scene=%s variant=%s\n' "$scene" "$variant"
 done
done
rm "$out/counts-cpu" "$out/counts-other"
printf 'PASS all trajectory and tick/reset accounting comparisons\n'
