#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export TMPDIR=${TMPDIR:-"$ROOT/.tmp"}
mkdir -p "$TMPDIR"
cd "$ROOT/magma"
make game/test_scattered_live game/test_igloo_runtime \
    game/test_swamp_witch_runtime game/test_witch_self_potion_live \
    game/test_witch_ranged_live game/test_witch_loot_live \
    game/test_witch_terminal_xp_live \
    game/test_witch_equipped_death_live \
    game/test_witch_drowning_death_live \
    game/test_witch_anvil_death_live \
    game/test_witch_burning_death_live >/dev/null
./game/test_scattered_live
./game/test_igloo_runtime
./game/test_swamp_witch_runtime
./game/test_witch_self_potion_live
./game/test_witch_ranged_live
./game/test_witch_loot_live
./game/test_witch_terminal_xp_live
./game/test_witch_equipped_death_live
./game/test_witch_drowning_death_live
./game/test_witch_anvil_death_live
./game/test_witch_burning_death_live
