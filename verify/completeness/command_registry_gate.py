#!/usr/bin/env python3
"""Fail closed on the vanilla integrated-server command registry."""

from __future__ import annotations

import json
import pathlib
import re
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    manifest = json.loads(
        (HERE / "command_registry_manifest.json").read_text(encoding="utf-8")
    )
    jars = sorted((ROOT / "java/Minecraft/.gradle/minecraft").glob(
        "forgeSrc-1.11.2-*-sources.jar"
    ))
    require(len(jars) == 1, "expected one bootstrapped 1.11.2 source jar")
    with zipfile.ZipFile(jars[0]) as archive:
        source = archive.read(
            "net/minecraft/command/ServerCommandManager.java"
        ).decode("utf-8")
    common, tail = source.split("if (serverIn.isDedicatedServer())", 1)
    integrated = set(re.findall(r"new (Command[A-Za-z]+)\(\)", common))
    else_body = tail.split("else", 1)[1]
    integrated.update(re.findall(
        r"new (Command[A-Za-z]+)\(\)", else_body.split("CommandBase", 1)[0]
    ))
    supported = set(manifest["supported"])
    host_control = set(manifest["host_control"])
    opened = set(manifest["open"])
    require(manifest["schema"] == "netherite.command_registry.v1"
            and manifest["java_version"] == "1.11.2",
            "invalid command-registry manifest identity")
    require(len(integrated) == manifest["integrated_server_count"] == 47,
            "integrated command registry count changed")
    require(not supported.intersection(opened)
            and not supported.intersection(host_control)
            and not opened.intersection(host_control),
            "command registry classifications overlap")
    require(supported.union(host_control).union(opened) == integrated,
            "command registry has an omitted or invented classification")
    require(host_control == {
        "CommandDebug", "CommandPublishLocalServer",
    }, "host-control command set changed")
    require(manifest["host_control_contract"] == {
        "CommandDebug": (
            "permission-3 wall-clock profiler control and external "
            "debug/profile-results file output"),
        "CommandPublishLocalServer": (
            "permission-4 integrated-server LAN listener publication and "
            "host port allocation"),
    }, "host-control command contract changed")
    with zipfile.ZipFile(jars[0]) as archive:
        debug_source = archive.read(
            "net/minecraft/command/CommandDebug.java").decode("utf-8")
        publish_source = archive.read(
            "net/minecraft/command/server/CommandPublishLocalServer.java"
        ).decode("utf-8")
    require("return 3;" in debug_source
            and "server.enableProfiling();" in debug_source
            and 'server.getFile("debug")' in debug_source
            and "new FileWriter(file1)" in debug_source,
            "debug command no longer matches its host-control boundary")
    require("server.shareToLAN(GameType.SURVIVAL, false)" in publish_source
            and "commands.publish.started" in publish_source
            and "commands.publish.failed" in publish_source,
            "publish command no longer matches its host-control boundary")
    require(supported == {
        "CommandAchievement", "CommandBlockData", "CommandBroadcast",
        "CommandClearInventory", "CommandClone",
        "CommandCompare", "CommandDefaultGameMode", "CommandDifficulty",
        "CommandEffect", "CommandEmote", "CommandEnchant", "CommandEntityData",
        "CommandExecuteAt",
        "CommandFill", "CommandGameRule", "CommandGameMode", "CommandGive",
        "CommandHelp", "CommandKill", "CommandLocate",
        "CommandMessage",
        "CommandMessageRaw", "CommandParticle", "CommandPlaySound",
        "CommandReplaceItem", "CommandScoreboard", "CommandSetBlock",
        "CommandSetDefaultSpawnpoint",
        "CommandSetSpawnpoint",
        "CommandShowSeed",
        "CommandSpreadPlayers",
        "CommandStats", "CommandStopSound",
        "CommandSummon",
        "CommandTestFor", "CommandTestForBlock", "CommandTP",
        "CommandTeleport", "CommandTime",
        "CommandTitle", "CommandToggleDownfall", "CommandTrigger",
        "CommandWeather", "CommandXP",
        "CommandWorldBorder",
    }, "supported command set changed without its direct oracle gate")
    print(f"PASS command registry: {len(supported)}/{len(integrated)} exact, "
          f"{len(host_control)} host-control, {len(opened)} open")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError, zipfile.BadZipFile) \
            as error:
        print(f"FAIL command registry: {error}")
        raise SystemExit(1)
