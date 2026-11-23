#!/usr/bin/env bash
# The git tree hash of directory DIR's files (oracle-src-check.sh compares two trees with it),
# through a scratch repository beside it; needs git, not this repository.
set -euo pipefail
D=$(cd "${1:?usage: treehash.sh DIR}" && pwd)
G=$D.treehash.git
rm -rf "$G"
git init -q --bare "$G"
GIT_DIR=$G GIT_WORK_TREE=$D git add -A .
GIT_DIR=$G git write-tree
rm -rf "$G"
