#!/usr/bin/env bash
# The device module of S6 on the device (cuda/tick/tick.mk): the engine's device
# bitcode and cuda/tick/dev.c linked, cut to what the kernels reach
# (nw-roots), optimized, stubbed and given its symbol table (nw-stub), then
# split into parts, each instrumented (nw-dev, cuda/tick/inl.c inlined),
# compiled to PTX and to relocatable code by itself (ptxas -c through the
# memory budget: one ptxas on the whole module took 8 GB), and device-linked.
#   bash cuda/tick/build.sh OUTDIR ARCH PTXAS_O INLINE PARTS JOBS CALLBACKS
set -euo pipefail
cd "$(dirname "$0")/../.."
O=$1 ARCH=$2 PO=$3 INLINE=$4 N=$5 J=$6 CB=$7
L=${LLVM_BIN:-/usr/lib/llvm-21/bin}
CUDA_BIN=${CUDA_BIN:-/usr/local/cuda/bin}
P=$O/nwpass.so
MEM="bash tests/memslot.sh --key"
OPT="-vectorize-loops=false -vectorize-slp=false -inline-threshold=$INLINE"
API=chunkloop,tick_scatter,tick_gather,tick_clear,nwdev_in_slow,nwdev_out_slow,nwdev_wb_slow,nwdev_unsupported
API=$API,nwdev_tabs,nwdev_curs,nwdev_bases,nwdev_sizes,nwdev_envs,nwdev_bad_call,nwdev_memcpy,nwdev_memmove,nwdev_memset
"$L/llvm-link" -o "$O/s6.link.bc" "$O"/dev/*.bc "$O/dev.bc"
$MEM s6-opt "$O" "$L/opt" -load-pass-plugin="$P" -passes='nw-roots,internalize,globaldce' -nw-callbacks="$CB" \
  -internalize-public-api-list="$API" -o "$O/s6.roots.bc" "$O/s6.link.bc" 2> "$O/nw-roots.log" || { cat "$O/nw-roots.log"; exit 1; }
# the functions the device keeps, one FILE$NAME a line: the mirror rule's
# kept list for TICK (cuda/mirror.tsv, cuda/mirror.c)
"$L/llvm-nm" --defined-only "$O/s6.roots.bc" | awk '$2 ~ /^[Tt]$/ {print $3}' > "$O/nw-roots.kept"
$MEM s6-opt "$O" "$L/opt" $OPT -passes='default<O2>' -o "$O/s6.o2.bc" "$O/s6.roots.bc"
$MEM s6-opt "$O" "$L/opt" -load-pass-plugin="$P" -passes='nw-stub,globaldce' -o "$O/s6.stub.bc" "$O/s6.o2.bc" \
  2> "$O/nw-stub.log" || { cat "$O/nw-stub.log"; exit 1; }
rm -rf "$O/parts"
mkdir -p "$O/parts"
"$L/llvm-split" -j "$N" -o "$O/parts/p" "$O/s6.stub.bc"
part() {
  local k=$1 b=$O/parts/p$1
  "$L/llvm-link" -o "$b.l.bc" "$b" "$O/inl.bc"
  "$L/opt" -load-pass-plugin="$P" $OPT -passes='nw-dev,default<O1>' -o "$b.f.bc" "$b.l.bc" 2> "$b.dev.log"
  "$L/llc" -O2 -mcpu="$ARCH" -nvptx-fma-level=0 -o "$b.ptx" "$b.f.bc"
  $MEM s6-ptxas "$O" "$CUDA_BIN/ptxas" -arch="$ARCH" -c -O"$PO" --fmad=false -o "$b.o" "$b.ptx"
}
export -f part
export O L P OPT ARCH PO MEM CUDA_BIN
seq 0 $((N - 1)) | xargs -P "$J" -I{} bash -c 'part {}'
"$CUDA_BIN/nvlink" -arch="$ARCH" -o "$O/tick.cubin" "$O"/parts/p*.o
echo "s6: $(tail -1 "$O/nw-roots.log"); $(tail -1 "$O/nw-stub.log"); $(cat "$O"/parts/*.dev.log | awk '{l += $2; s += $5; w += $8; x += $15} END {print "nw-dev: " l " pointer loads, " s " pointer stores, " w " writes instrumented, " x " mem calls made wide"}'); PTX $(cat "$O"/parts/p*.ptx | wc -l) lines in $N parts"
