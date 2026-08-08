#!/bin/bash
# Regenerate every Mojang-texture-derived C header in magma/assets from
# your own minecraft-1.11.2.jar (located via assets/mc_jar.py: $MC_JAR, the
# repo-local ForgeGradle cache that bootstrap_oracle.sh populates, ~/.gradle,
# or a Prism/official launcher install). These headers are never committed.
#
# Usage: bash scripts/bootstrap_assets.sh
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO/magma"

SCRIPTS=(build_atlas build_colormap build_gui_atlas build_hand_atlas
         build_inventory_ui_atlas
         build_hud_atlas build_item_atlas build_loading_bg build_mob_atlas
         build_portal build_sky_atlas build_underwater build_water_frames)
for s in "${SCRIPTS[@]}"; do
    echo "== assets/$s.py =="
    uv run --no-project --with pillow python "assets/$s.py"
done
echo "== assets/build_end_city_templates.py =="
uv run --no-project --with nbtlib python assets/build_end_city_templates.py
echo "== assets/build_village_templates.py =="
VILLAGE_OUT="$REPO/.tmp/village-templates"
mkdir -p "$VILLAGE_OUT"
(cd "$REPO/java/Minecraft" && ./gradlew -q villageTemplateGolden) \
    > "$VILLAGE_OUT/templates.txt" 2> "$VILLAGE_OUT/gradle.log" || {
        tail -80 "$VILLAGE_OUT/gradle.log" >&2
        exit 1
    }
uv run --no-project python assets/build_village_templates.py \
    "$VILLAGE_OUT/templates.txt" assets/village_templates.h
echo "== assets/build_sound_manifest.py =="
uv run --no-project python assets/build_sound_manifest.py
echo "asset headers regenerated: ${#SCRIPTS[@]} texture generators plus structures and sound manifest"
