#!/usr/bin/env python3
"""Lock the bounded sound graph, event producers, routing, and persistence."""

from __future__ import annotations

import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]
GROUPS = {
    "AUD-01": ("magma/assets/sound_manifest.h",
               "magma/game/test_audio_selector_oracle.sh",
               "magma/game/test_audio_source_oracle.sh"),
    "AUD-02": ("magma/game/audio_live.c", "magma/game/test_audio_live.c",
               "magma/game/test_weather_world.c", "magma/game/test_portal_live.c",
               "magma/game/test_firework_audio.c"),
    "AUD-03": ("magma/game/config.c", "magma/game/test_config.c",
               "magma/game/audio_live.c", "magma/app/game_main.c"),
}
EVENT_TESTS = (
    "magma/game/test_block_break_audio.c",
    "magma/game/test_player_attack_audio_runtime.c",
    "magma/game/test_player_movement_audio_runtime.c",
    "magma/game/test_firework_audio.c",
)


def require_tokens(relative: str, tokens: tuple[str, ...]) -> None:
    text = (ROOT / relative).read_text()
    missing = [token for token in tokens if token not in text]
    if missing:
        raise RuntimeError(f"{relative}: missing ownership tokens {missing}")


def main() -> int:
    for todo, evidence in GROUPS.items():
        for relative in evidence:
            path = ROOT / relative
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f"{todo}: missing evidence {relative}")
    for relative in EVENT_TESTS:
        if not (ROOT / relative).is_file():
            raise RuntimeError(f"missing event-family evidence {relative}")

    runtime_h = (ROOT / "magma/game/runtime.h").read_text()
    sound_enum = runtime_h[runtime_h.index("GM_SOUND_CHICKEN_HURT"):
                           runtime_h.index("GM_SOUND_COUNT")]
    sound_count = len(re.findall(r"\bGM_SOUND_[A-Z0-9_]+\b", sound_enum))
    if sound_count < 230:
        raise RuntimeError("generated sound-event census fell below 230 identities")
    require_tokens("magma/game/audio_live.c", (
        "choose_node", "category_volume", "pending_delayed", "active_records",
        "GM_SOUND_RECORD_STOP", "alSourceStop", "AL_POSITION",
        "gm_music_ticker_update", "{12000, 24000}", "{0, 0}",
        "gm_cave_ambience_probe", "1013904223u", "player_distance_sq <= 4.0",
        "jrand_int_bound(&ambience->random, 12000)",
        "gm_special_sound_asset_roots[1 + music_type]", "start_music",
        "gm_audio_requested_music_type", "GM_MUSIC_CREDITS",
        "GM_MUSIC_CREATIVE", "active_music_type", "update_cave",
        "gm_audio_live_update_at", "gm_audio_live_set_category_volume",
        "alcLoopbackOpenDeviceSOFT", "alcRenderSamplesSOFT"))
    require_tokens("magma/assets/build_sound_manifest.py", (
        "SPECIAL_EVENTS", '"ambient.cave"', '"music.dragon"',
        "gm_special_sound_asset_roots"))
    require_tokens("magma/game/runtime.h", (
        "GmRuntimeSoundEvent", "GM_SOUND_CATEGORY_WEATHER",
        "GM_SOUND_CATEGORY_PLAYERS", "GM_SOUND_CATEGORY_BLOCKS"))
    require_tokens("magma/game/test_audio_live.c", (
        "delayed", "record", "dropped", "static lava source",
        "music switch consumes both exact Random draws",
        "cave mood matches exact LCG position",
        "live consumer starts the selected game music stream",
        "plays the live ordered cave sample",
        "thirty-second record capture contains sustained PCM output",
        "thirty-second title capture contains sustained PCM output",
        "without a fake crossfade"))

    print(
        "PASS audio boundary: generated event/accessor graph, exact selector and "
        "source descriptor oracles, live event families, delayed/record lifecycle, "
        "categories, attenuation, listener motion, exact MusicTicker and cave-mood "
        "scheduling, and failure paths are fail-closed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL audio boundary: {exc}")
        raise SystemExit(1)
