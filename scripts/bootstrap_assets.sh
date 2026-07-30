#!/bin/bash
# Regenerate every Mojang-texture-derived C header in c/magma/assets from
# your own minecraft-1.11.2.jar (located via assets/mc_jar.py: $MC_JAR, the
# repo-local ForgeGradle cache that bootstrap_oracle.sh populates, ~/.gradle,
# or a Prism/official launcher install). These headers are never committed.
#
# Usage:
#   bash scripts/bootstrap_assets.sh
#   bash scripts/bootstrap_assets.sh --fetch-client
#
# --fetch-client is the native-mac/bootstrap path when no launcher or Gradle
# cache already contains 1.11.2.  It downloads the official client jar from
# Mojang, verifies the manifest SHA-1/size, and keeps it in the ignored local
# ForgeGradle cache.  You must own Minecraft; nothing Mojang-derived is shipped.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"

FETCH_CLIENT=0
case "${1:-}" in
    "") ;;
    --fetch-client) FETCH_CLIENT=1 ;;
    -h|--help)
        sed -n '2,14p' "$0" | sed 's/^#[[:space:]]*//'
        exit 0
        ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
esac

if ! uv run --no-project python -c \
        'import sys; sys.path.insert(0, sys.argv[1]); import mc_jar; print(mc_jar.find_jar())' \
        "$REPO/c/magma/assets" >/dev/null 2>&1; then
    if [ "$FETCH_CLIENT" -eq 1 ]; then
        echo "== fetch official Minecraft 1.11.2 client (local, SHA-1 verified) =="
        uv run --no-project python "$REPO/scripts/fetch_minecraft_client.py"
    else
        echo "ERROR: minecraft-1.11.2.jar not found." >&2
        echo "Run scripts/bootstrap_oracle.sh, install 1.11.2 in a launcher," >&2
        echo "set MC_JAR, or rerun with --fetch-client." >&2
        exit 2
    fi
fi

cd "$REPO/c/magma"

SCRIPTS=(build_atlas build_colormap build_gui_atlas build_hand_atlas
         build_inventory_ui_atlas
         build_hud_atlas build_item_atlas build_loading_bg build_mob_atlas
         build_portal build_sky_atlas build_underwater build_water_frames)
for s in "${SCRIPTS[@]}"; do
    echo "== assets/$s.py =="
    uv run --no-project --with pillow python "assets/$s.py"
done
echo "asset headers regenerated: ${#SCRIPTS[@]} generators"
