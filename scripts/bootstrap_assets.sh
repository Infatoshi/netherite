#!/bin/bash
# Regenerate every Mojang-texture-derived C header in c/craster/assets from
# your own minecraft-1.11.2.jar (located via assets/mc_jar.py: $MC_JAR, the
# repo-local ForgeGradle cache that bootstrap_oracle.sh populates, ~/.gradle,
# or a Prism/official launcher install). These headers are never committed.
#
# Usage: bash scripts/bootstrap_assets.sh
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO/c/craster"

SCRIPTS=(build_atlas build_colormap build_gui_atlas build_hand_atlas
         build_hud_atlas build_item_atlas build_loading_bg build_mob_atlas
         build_portal build_sky_atlas build_underwater build_water_frames)
for s in "${SCRIPTS[@]}"; do
    echo "== assets/$s.py =="
    uv run --no-project --with pillow python "assets/$s.py"
done
echo "asset headers regenerated: ${#SCRIPTS[@]} generators"
