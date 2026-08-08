"""Locate the vanilla minecraft-1.11.2.jar for the asset build scripts.

Search order:
  1. $MC_JAR (explicit override)
  2. the repo's ForgeGradle cache (java/Minecraft/run/gradle - the layout
     bootstrap_oracle.sh / setupDecompWorkspace produces)
  3. ~/.gradle/caches (default ForgeGradle cache location)
  4. common launcher installs (Prism/MultiMC/official) of version 1.11.2

The jar is Mojang content and is never committed; every generated *_atlas.h /
*_gen.h header derives from it at build time (scripts/bootstrap_assets.sh).
"""
import glob
import os

_REL = "minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar"
_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.abspath(os.path.join(_HERE, "..", ".."))


def find_jar():
    cands = []
    if os.environ.get("MC_JAR"):
        cands.append(os.environ["MC_JAR"])
    cands.append(os.path.join(_REPO, "java", "Minecraft", "run", "gradle",
                              "caches", _REL))
    cands.append(os.path.expanduser("~/.gradle/caches/" + _REL))
    for pat in (
            "~/.local/share/PrismLauncher/libraries/com/mojang/minecraft/"
            "1.11.2/minecraft-1.11.2-client.jar",
            "~/Library/Application Support/PrismLauncher/libraries/com/"
            "mojang/minecraft/1.11.2/minecraft-1.11.2-client.jar",
            "~/.minecraft/versions/1.11.2/1.11.2.jar",
            "~/scoop/persist/prismlauncher/libraries/com/mojang/minecraft/"
            "1.11.2/minecraft-1.11.2-client.jar"):
        cands.extend(glob.glob(os.path.expanduser(pat)))
    for c in cands:
        if c and os.path.isfile(c):
            return c
    raise SystemExit(
        "minecraft-1.11.2.jar not found. Either run scripts/bootstrap_oracle.sh"
        " first (it populates the gradle cache), install MC 1.11.2 via a"
        " launcher, or set MC_JAR=/path/to/minecraft-1.11.2.jar")
