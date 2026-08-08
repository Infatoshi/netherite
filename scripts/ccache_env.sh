#!/usr/bin/env bash
# ccache_env.sh - opt-in shared ccache for agent worktrees (Job 6 / flywheel).
#
# Usage (from any worktree root, or any cwd):
#   source scripts/ccache_env.sh
#   make -C magma clean && make -C magma game -j"$(nproc)"
#
# Or one-shot without polluting the parent shell:
#   bash -c 'source scripts/ccache_env.sh && make -C magma game -j"$(nproc)"'
#
# What this does:
#   - Points ccache at the shared content-addressed cache:
#       $HOME/dev/nw/.ccache
#   - Exports CC="ccache <underlying-cc>" so every recipe that already uses
#     $(CC) goes through the cache. magma/Makefile already honors $(CC)
#     (see `CC ?= gcc` and every link/compile recipe); no Makefile change
#     is required, and builds without sourcing this file are unchanged.
#   - Leaves CCACHE_BASEDIR empty by default. ccache's base_dir rewrites
#     absolute -I paths (magma uses -I$(abspath ../blaze)/core) into paths
#     relative to CWD before invoking the real compiler, which changes
#     embedded __FILE__ strings and breaks byte-identity vs a plain gcc
#     build. Empty base_dir keeps clean non-ccache and clean warm-ccache
#     magma_game binaries sha256-identical. Cross-worktree hit rate for the
#     few TUs that pass absolute worktree paths is weaker; same-worktree
#     rebuilds still hit. Opt in to rewriting only if you accept a different
#     binary hash:
#       export CCACHE_BASEDIR=$HOME/dev/nw   # or this worktree root
#       source scripts/ccache_env.sh
#   - Sets CCACHE_NOHASHDIR=1 so the current working directory is not part of
#     the cache key (helps when the same sources are built from the same
#     relative layout under different absolute roots, without path rewriting).
#
# Acceptance notes:
#   - Clean build without ccache and clean warm-ccache build must produce
#     byte-identical magma_game (sha256).
#   - Touching a .c source or a generated header must change the rebuilt hash
#     (no stale-cache false hit).
#   - Do NOT copy a shared candidate binary between agents; each worktree
#     keeps its own magma/magma_game.
#
# Stats: `CCACHE_DIR=$HOME/dev/nw/.ccache ccache -s`
#
# Not sourced by the frozen scripts/delegate_gate.sh; opt-in only.

# Allow being sourced from any cwd; resolve this file's location.
_CCACHE_ENV_SRC="${BASH_SOURCE[0]:-$0}"
_CCACHE_ENV_ROOT="$(cd "$(dirname "$_CCACHE_ENV_SRC")/.." && pwd)"

export CCACHE_DIR="${CCACHE_DIR:-$HOME/dev/nw/.ccache}"
# Default empty: see header. Honour a caller-provided CCACHE_BASEDIR.
if [ -z "${CCACHE_BASEDIR+x}" ]; then
    # Unset means "not provided": leave empty (no rewrite).
    export CCACHE_BASEDIR=""
fi
# Do not hash CWD into the key; pairs with empty base_dir for same-layout
# worktrees without rewriting absolute -I paths into different embed strings.
export CCACHE_NOHASHDIR="${CCACHE_NOHASHDIR:-1}"

mkdir -p "$CCACHE_DIR"

# Pick the underlying compiler once. Prefer an already-set non-ccache CC,
# else gcc, else cc.
_CCACHE_UNDERLYING="${CCACHE_REAL_CC:-}"
if [ -z "$_CCACHE_UNDERLYING" ]; then
    if [ -n "${CC:-}" ] && [[ "$CC" != ccache* ]]; then
        _CCACHE_UNDERLYING="$CC"
    elif command -v gcc >/dev/null 2>&1; then
        _CCACHE_UNDERLYING=gcc
    else
        _CCACHE_UNDERLYING=cc
    fi
fi

if ! command -v ccache >/dev/null 2>&1; then
    echo "[ccache_env] WARNING: ccache not on PATH; leaving CC=$_CCACHE_UNDERLYING" >&2
    export CC="$_CCACHE_UNDERLYING"
else
    export CC="ccache $_CCACHE_UNDERLYING"
    # make expands $(CC) unquoted in magma recipes; "ccache gcc" is two
    # words and still works as the command + first arg.
    echo "[ccache_env] CC=$CC" >&2
    echo "[ccache_env] CCACHE_DIR=$CCACHE_DIR" >&2
    echo "[ccache_env] CCACHE_BASEDIR=${CCACHE_BASEDIR:-<empty>}" >&2
    echo "[ccache_env] CCACHE_NOHASHDIR=$CCACHE_NOHASHDIR" >&2
fi

unset _CCACHE_ENV_SRC _CCACHE_ENV_ROOT _CCACHE_UNDERLYING
