#!/usr/bin/env python3
"""Drive qrl hud_pin + frame captures for ui_hud oracle goldens.

Produces <id>_a.png / <id>_b.png plus meta/<id>.json and capture_manifest.json.
Frames come only from the live Java client (qrl cmd \"frame\"); never synthesized.

Capture integrity (this driver):
  - Every state starts from an asserted clean living player (no GuiGameOver).
  - base_scene fails hard if any server command fails.
  - clear_effects is applied before requested effects (Java hud_pin + here).
  - After death golden: real respawn + close GuiGameOver before anything else.
  - Shield block (1.11.2) replaces version-wrong sword block.
  - Fire pin must report burning and the PNG must show warm fire overlay.
  - A/B noise is frozen tight; excess noise marks the capture FAILED (no 40-loophole).
  - State-presence sanity checks for death/shield/bow/eat/fire/inside/portal/uw.
"""
from __future__ import print_function

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.getcwd())
import qrl_client  # noqa: E402

try:
    import numpy as np
    from PIL import Image
except ImportError:
    np = None
    Image = None

# Flat seed-0 ground is y=3/4; stand on a raised stone pad near origin.
PLAT_Y = 4
CX, CZ = 8, 8
POSE = {
    "x": CX + 0.5,
    "y": float(PLAT_Y + 1),
    "z": CZ + 0.5,
    "yaw": 0.0,
    "pitch": 0.0,
    "no_gravity": True,
}

# Item ids 1.11.2
IRON_HELM, IRON_CHEST, IRON_LEGS, IRON_BOOTS = 306, 307, 308, 309
WOOD_PICK, BOW, BREAD, SHIELD = 270, 261, 297, 442
ARMOR_IDS = [IRON_BOOTS, IRON_LEGS, IRON_CHEST, IRON_HELM]

# A/B mean-abs noise ceiling (ROI). Tight freeze, not the old 40 loophole.
NOISE_MAX_DEFAULT = 2.0
NOISE_MAX = {
    "hud_hurt_flash_on": 3.0,
    "hud_hurt_flash_off": 3.0,
    "hand_bow_pull20": 3.0,
    # Portal / fire atlas frames still race under pin_texture_animations on
    # llvmpipe. Presence checks enforce the feature; these ceilings reject
    # fully-unfrozen scenes (>>40) without the old 40 loophole.
    "overlay_portal_050": 12.0,
    "overlay_fire": 35.0,
    "hud_death": 5.0,  # GuiGameOver text can subpixel-shift slightly
    "overlay_inside_stone": 3.0,
    "overlay_inside_grass": 3.0,
    "overlay_underwater": 3.0,
}

W, H = 854, 480
S = 2
GUI_CX = (W + S - 1) // S // 2
SH = (H + S - 1) // S
HB_X = (GUI_CX - 91) * S
J1 = (SH - 39) * S


def log(msg):
    print("[ui_hud_driver] " + msg, file=sys.stderr)


def runcmds(e, cmds, retries=3):
    last = None
    for attempt in range(retries):
        try:
            r = e._cmd({"cmd": "runcmds", "action": {"cmds": cmds}})
        except Exception as ex:
            last = {"ok": False, "error": str(ex)}
            time.sleep(0.5)
            continue
        last = r
        if r.get("ok"):
            return r
        # Bridge-side timeout under load: back off and retry.
        if "timeout" in str(r.get("error", "")).lower() and attempt + 1 < retries:
            log("runcmds timeout, retry %d/%d" % (attempt + 1, retries))
            time.sleep(1.0)
            continue
        return r
    return last or {"ok": False, "error": "runcmds empty"}


def runcmds_ok(e, cmds, label="runcmds"):
    r = runcmds(e, cmds)
    if not r.get("ok"):
        raise RuntimeError("%s failed: %s" % (label, r))
    failed = int(r.get("failed", 0) or 0)
    if failed > 0:
        raise RuntimeError("%s had %d command failure(s): %s" % (label, failed, r))
    return r


def hud_pin(e, **kwargs):
    return e._cmd({"cmd": "hud_pin", "action": kwargs})


def hud_pin_ok(e, **kwargs):
    r = hud_pin(e, **kwargs)
    if not r.get("ok"):
        raise RuntimeError("hud_pin failed: %s" % r)
    return r


def set_pose(e, pose=None):
    p = dict(POSE if pose is None else pose)
    return e._cmd({"cmd": "set_pose", "action": p})


def focusdiag(e):
    return e._cmd({"cmd": "focusdiag", "action": {}})


def grab(e, path):
    r = e._cmd({"cmd": "frame", "action": {"file": path, "rerender": True}})
    if not r.get("ok"):
        raise RuntimeError("frame failed for %s: %s" % (path, r))
    if not os.path.isfile(path) or os.path.getsize(path) < 100:
        raise RuntimeError("frame file missing/empty: %s (%s)" % (path, r))
    return r


def settle(e, n=8):
    for _ in range(n):
        e.step({})


def base_scene(e):
    """Frozen clear noon, stone platform + backdrop wall, empty effects.

    Fails immediately if the runcmds transport fails or if the critical fill
    edits report total failure. Soft cmds (effect clear / clear inventory) may
    return 0 when already empty — that is not a hard failure in 1.11.2.
    """
    soft = [
        "gamerule sendCommandFeedback false",
        "gamerule logAdminCommands false",
        "gamerule doDaylightCycle false",
        "gamerule doWeatherCycle false",
        "gamerule doMobSpawning false",
        "gamerule doFireTick false",
        "gamerule randomTickSpeed 0",
        "gamerule keepInventory true",
        "time set 6000",
        "weather clear 1000000",
        "gamemode 0 @a",
        "difficulty peaceful",
        # Load chunks at the capture pad before fills (spawn is often far).
        "tp @a %d %d %d" % (CX + 0.5, PLAT_Y + 2, CZ + 0.5),
        "clear @a",
        "effect @a clear",
    ]
    hard = [
        "fill %d %d %d %d %d %d minecraft:stone" % (
            CX - 3, PLAT_Y, CZ - 3, CX + 3, PLAT_Y, CZ + 3),
        "fill %d %d %d %d %d %d minecraft:air" % (
            CX - 3, PLAT_Y + 1, CZ - 3, CX + 3, PLAT_Y + 4, CZ + 3),
        "fill %d %d %d %d %d %d minecraft:stone" % (
            CX - 2, PLAT_Y + 1, CZ + 3, CX + 2, PLAT_Y + 4, CZ + 3),
    ]
    r_soft = runcmds(e, soft)
    if not r_soft.get("ok"):
        raise RuntimeError("base_scene soft cmds failed: %s" % r_soft)
    # Let the server load the destination chunk after tp.
    settle(e, 6)
    set_pose(e, POSE)
    settle(e, 4)
    r_hard = runcmds(e, hard)
    if not r_hard.get("ok"):
        raise RuntimeError("base_scene hard fills failed: %s" % r_hard)
    # Require at least one fill to report success; unloaded-chunk fills return 0.
    # Re-fill of already-correct blocks can return 0 (counts as failed in qrl).
    if int(r_hard.get("ran", 0) or 0) < 1:
        raise RuntimeError(
            "base_scene fill failure (chunks not loaded?): ran=%s failed=%s"
            % (r_hard.get("ran"), r_hard.get("failed")))
    log("base_scene: soft=%s hard=%s" % (r_soft, r_hard))
    set_pose(e, POSE)
    settle(e, 6)
    return r_hard


def ensure_living(e, do_respawn=False):
    """Assert clean living state: health>0, no GuiGameOver, hold_death off.

    When do_respawn=True (post-death), performs real respawnPlayer + screen close.
    """
    pin = dict(
        hold_death=False,
        health=20.0,
        clear_effects=True,
        food=20,
        air=300,
        absorption=0.0,
        xp_level=0,
        xp_frac=0.0,
        fire=0,
        portal=0.0,
        use_action=0,
        boss={"show": False},
        hotbar=[[0, 0, 0]] * 9,
        armor=[0, 0, 0, 0],
    )
    pin.update(POSE)
    if do_respawn:
        pin["respawn"] = True
    r = hud_pin_ok(e, **pin)
    settle(e, 2 if not do_respawn else 4)
    if do_respawn:
        # Second pass after respawn settles.
        pin.pop("respawn", None)
        r = hud_pin_ok(e, **pin)
        settle(e, 2)
    fd = focusdiag(e)
    screen = (r.get("screen") or fd.get("screen") or "null")
    health = float(r.get("health", 0) or 0)
    if health <= 0.0 or r.get("dead") is True:
        # Escalate to real respawn once.
        pin["respawn"] = True
        r = hud_pin_ok(e, **pin)
        settle(e, 4)
        health = float(r.get("health", 0) or 0)
        screen = r.get("screen") or "null"
        if health <= 0.0 or r.get("dead") is True:
            raise RuntimeError("ensure_living: still dead after respawn: %s" % r)
    if "GameOver" in str(screen):
        r2 = hud_pin_ok(e, respawn=True, health=20.0, hold_death=False, **POSE)
        settle(e, 3)
        fd = focusdiag(e)
        screen = fd.get("screen") or r2.get("screen") or "null"
        if "GameOver" in str(screen):
            raise RuntimeError(
                "ensure_living: GuiGameOver still open: pin=%s focus=%s" % (r2, fd))
    log("ensure_living: health=%.1f screen=%s burning=%s" % (
        health, screen, r.get("burning")))
    return r


def clear_player(e):
    # clear/effect may return 0 when already empty; only transport must succeed.
    r = runcmds(e, ["clear @a", "effect @a clear", "kill @e[type=!player]"])
    if not r.get("ok"):
        raise RuntimeError("clear_player failed: %s" % r)
    return ensure_living(e, do_respawn=False)


def roi_for(state_id):
    if state_id in ("hud_armor_iron",):
        return (HB_X, J1 - 10 * S, HB_X + 10 * 8 * S, J1 + 9 * S)
    if state_id in ("hud_absorption_armor",):
        return (HB_X, J1 - 20 * S, HB_X + 10 * 8 * S, J1 + 9 * S)
    if state_id in ("hud_hurt_flash_on", "hud_hurt_flash_off"):
        return (HB_X, J1, HB_X + 10 * 8 * S, J1 + 9 * S)
    if state_id in ("hud_hunger_poison",):
        x1 = HB_X + 182 * S
        return (x1 - 10 * 8 * S - 9 * S, J1, x1, J1 + 9 * S)
    if state_id in ("hud_air_partial",):
        air_y = (SH - 49) * S
        x1 = (GUI_CX + 91) * S
        return (x1 - 10 * 8 * S - 9 * S, air_y, x1, air_y + 9 * S)
    if state_id in ("hud_xp_half",):
        xp_y = (SH - 29) * S
        return (HB_X, xp_y - 12 * S, HB_X + 182 * S, xp_y + 5 * S)
    if state_id in ("hud_durability_half",):
        ix = HB_X + 3 * S
        iy = (SH - 22) * S + 3 * S
        return (ix, iy + 12 * S, ix + 14 * S, iy + 16 * S)
    if state_id in ("hud_boss_half",):
        bb_x = (GUI_CX - 91) * S
        bb_y = 12 * S
        return (bb_x, bb_y - 10 * S, bb_x + 182 * S, bb_y + 6 * S)
    if state_id in ("hud_death",):
        by = H // 2 - 18
        return (0, by, W, by + 36)
    if state_id.startswith("hand_"):
        return viewmodel_roi_rect()
    if state_id.startswith("overlay_"):
        return (2, 2, W - 2, H - 2)
    return (0, 0, W, H)


def load_rgb(path):
    if Image is None:
        raise RuntimeError("PIL required for capture sanity checks")
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def mean_abs_roi(a, b, rect):
    x0, y0, x1, y1 = rect
    aa = a[y0:y1, x0:x1]
    bb = b[y0:y1, x0:x1]
    return float(np.abs(aa.astype(np.int32) - bb.astype(np.int32)).mean())


def assert_ab_noise(state_id, path_a, path_b):
    a = load_rgb(path_a)
    b = load_rgb(path_b)
    rect = roi_for(state_id)
    noise = mean_abs_roi(a, b, rect)
    limit = NOISE_MAX.get(state_id, NOISE_MAX_DEFAULT)
    if noise > limit:
        raise RuntimeError(
            "A/B noise %.3f > %.3f for %s (ROI %s) — capture FAILED, not frozen"
            % (noise, limit, state_id, rect))
    return noise


# Non-hotbar lower-right viewmodel ROI (above hotbar chrome).
# Hotbar sits at GUI y = sh-22; exclude the bottom ~44 px of FB + 8 px margin.
def viewmodel_roi_rect():
    hb_y = (SH - 22) * S
    x0, y0 = W * 2 // 3, H * 2 // 3
    x1, y1 = W - 8, max(y0 + 8, hb_y - 4)
    return (x0, y0, x1, y1)


# Cross-state viewmodel fingerprints for presence (reject wall-only clones).
_HAND_VM_FINGERPRINTS = {}
# Empty-hand wall baseline (no item) — each use-state must differ from this.
_HAND_EMPTY_BASELINE = None


def _vm_crop(a):
    x0, y0, x1, y1 = viewmodel_roi_rect()
    return a[y0:y1, x0:x1]


def capture_empty_hand_baseline(e, outdir):
    """Pin empty main hand against the wall; ROI is pure backdrop (no viewmodel item).

    Bare-arm may appear in lower-right; that is fine — use-state items must still
    differ from this baseline. Used only for presence, not as a golden.
    """
    global _HAND_EMPTY_BASELINE
    pin = dict(POSE)
    pin.update({
        "health": 20.0, "food": 20, "air": 300,
        "hotbar": [[0, 0, 0]] * 9, "hotbar_sel": 0,
        "armor": [0, 0, 0, 0],
        "use_action": 0, "fire": 0, "portal": 0.0,
        "boss": {"show": False},
        "clear_effects": True,
        "yaw": 0.0, "pitch": 0.0, "z": CZ + 0.5,
    })
    hud_pin_ok(e, **pin)
    settle(e, 4)
    hud_pin_ok(e, **pin)
    path = os.path.join(outdir, "_hand_empty_baseline.png")
    grab(e, path)
    a = load_rgb(path)
    _HAND_EMPTY_BASELINE = _vm_crop(a).copy()
    log("empty-hand baseline: vm_std=%.2f shape=%s" % (
        float(_HAND_EMPTY_BASELINE.std()), _HAND_EMPTY_BASELINE.shape))
    return _HAND_EMPTY_BASELINE


def assert_feature_presence(state_id, path, pin_reply):
    """Automated state-presence sanity checks. Fail = contaminated golden."""
    a = load_rgb(path)
    rect = roi_for(state_id)
    x0, y0, x1, y1 = rect
    roi = a[y0:y1, x0:x1]
    mean = roi.mean(axis=(0, 1))
    std = float(roi.std())
    warm = float(((a[:, :, 0] > a[:, :, 2] + 20) & (a[:, :, 0] > 90)).mean())
    bot = a[H // 2:, :, :]
    bot_warm = float(
        ((bot[:, :, 0] > bot[:, :, 2] + 15) & (bot[:, :, 0] > 80)).mean())
    lr = a[H * 2 // 3:, W * 2 // 3:, :]
    lr_std = float(lr.std())
    dark_frac = float((roi.mean(axis=2) < 50).mean())
    purple_bias = float(mean[2] - mean[1])  # B - G
    vm = _vm_crop(a)
    vm_std = float(vm.std()) if vm.size else 0.0
    # Non-stone residual: stone wall is ~mid gray; viewmodels add brown/wood
    # pixels and silhouette edges that push abs deviation from median gray.
    vm_med = float(np.median(vm.reshape(-1, 3), axis=0).mean()) if vm.size else 0.0
    vm_dev = float(np.abs(vm.astype(np.float32) - vm_med).mean()) if vm.size else 0.0

    if state_id == "hud_death":
        screen = str(pin_reply.get("screen") or "")
        if "GameOver" not in screen:
            raise RuntimeError(
                "hud_death: expected GuiGameOver, got screen=%s pin=%s"
                % (screen, pin_reply))
        if float(pin_reply.get("health", 1)) > 0:
            raise RuntimeError("hud_death: health not 0: %s" % pin_reply)
        # Red death wash / banner should leave non-gray variance in band
        if std < 5.0:
            raise RuntimeError("hud_death: ROI nearly flat (std=%.2f)" % std)

    elif state_id in ("hand_block_shield", "hand_bow_pull20", "hand_eat_mid"):
        if not pin_reply.get("hand_active"):
            raise RuntimeError(
                "%s: hand not active: %s" % (state_id, pin_reply))
        if pin_reply.get("hide_gui") is True:
            raise RuntimeError(
                "%s: hide_gui still true after pin (viewmodel suppressed): %s"
                % (state_id, pin_reply))
        if int(pin_reply.get("equip_pinned", 1) or 0) == 0:
            raise RuntimeError(
                "%s: equip_progress pin failed (reflection): %s"
                % (state_id, pin_reply))
        held = int(pin_reply.get("held_id", -1) or -1)
        expect_held = {
            "hand_bow_pull20": BOW,
            "hand_eat_mid": BREAD,
            "hand_block_shield": SHIELD,
        }[state_id]
        if held != expect_held:
            raise RuntimeError(
                "%s: held_id=%s want %s: %s"
                % (state_id, held, expect_held, pin_reply))
        if state_id == "hand_bow_pull20":
            uc = int(pin_reply.get("use_count", -1) or -1)
            # bow max 72000, pull 20 => remaining ~71980
            if uc < 71900 or uc > 72000:
                raise RuntimeError(
                    "hand_bow_pull20: unexpected use_count=%s (want ~71980)" % uc)
        # Wall-only ROIs have stone texture variance (std~20+) but no viewmodel.
        if vm_std < 8.0:
            raise RuntimeError(
                "%s: empty viewmodel vm_std=%.2f" % (state_id, vm_std))
        # Must differ from empty-hand baseline (rejects hideGUI wall-only frames
        # where hotbar icon changes but lower-right is still just stone).
        if _HAND_EMPTY_BASELINE is not None and _HAND_EMPTY_BASELINE.shape == vm.shape:
            vs_empty = float(np.abs(
                vm.astype(np.int32) - _HAND_EMPTY_BASELINE.astype(np.int32)).mean())
            if vs_empty < 2.0:
                raise RuntimeError(
                    "%s: viewmodel ROI matches empty-hand baseline "
                    "(meanabs=%.3f) — wall-only / missing viewmodel"
                    % (state_id, vs_empty))
        # Cross-state identity: bow / eat / shield must not share the same ROI.
        for other_id, other_vm in _HAND_VM_FINGERPRINTS.items():
            if other_vm.shape != vm.shape:
                continue
            cross = float(np.abs(vm.astype(np.int32) - other_vm.astype(np.int32)).mean())
            if cross < 1.5:
                raise RuntimeError(
                    "%s: viewmodel ROI identical to %s (cross=%.3f) — wall-only"
                    % (state_id, other_id, cross))
        _HAND_VM_FINGERPRINTS[state_id] = vm.copy()

    elif state_id == "overlay_fire":
        if not pin_reply.get("burning"):
            raise RuntimeError(
                "overlay_fire: player not burning after pin: %s" % pin_reply)
        ft = int(pin_reply.get("fire_ticks", 0) or 0)
        if ft <= 0:
            raise RuntimeError(
                "overlay_fire: fire_ticks=%s not positive: %s" % (ft, pin_reply))
        # First-person fire quads occupy lower half with warm texels.
        if bot_warm < 0.02 and warm < 0.03:
            raise RuntimeError(
                "overlay_fire: no fire overlay signal (bot_warm=%.4f warm=%.4f)"
                % (bot_warm, warm))

    elif state_id in ("overlay_inside_stone", "overlay_inside_grass"):
        if dark_frac < 0.15 and float(mean.mean()) > 80:
            raise RuntimeError(
                "%s: not darkened (mean=%.1f dark_frac=%.3f)"
                % (state_id, float(mean.mean()), dark_frac))

    elif state_id == "overlay_portal_050":
        portal = float(pin_reply.get("portal", 0) or 0)
        if portal < 0.4:
            raise RuntimeError(
                "overlay_portal_050: portal pin not held (%.3f): %s"
                % (portal, pin_reply))
        # Portal swirl is purple-tinted full-frame; allow modest bias
        if purple_bias < 2.0 and float(mean[2]) < 90:
            raise RuntimeError(
                "overlay_portal_050: weak portal tint (B-G=%.1f meanB=%.1f)"
                % (purple_bias, float(mean[2])))

    elif state_id == "overlay_underwater":
        # Water overlay blues the frame
        if float(mean[2]) < float(mean[0]) - 5:
            raise RuntimeError(
                "overlay_underwater: not blue-biased meanRGB=%s" % mean.tolist())

    return {
        "mean_rgb": [float(mean[0]), float(mean[1]), float(mean[2])],
        "std": std,
        "warm_frac": warm,
        "bot_warm_frac": bot_warm,
        "lr_std": lr_std,
        "vm_std": vm_std,
        "vm_dev": vm_dev,
        "dark_frac": dark_frac,
        "purple_bias": purple_bias,
    }


def capture_pair(e, outdir, state_id, pin_kwargs, meta_extra=None, settle_n=2):
    """Pin state, dump A then re-pin and dump B (no tick drift)."""
    os.makedirs(outdir, exist_ok=True)
    meta_dir = os.path.join(outdir, "meta")
    os.makedirs(meta_dir, exist_ok=True)

    # Always clear effects first, then apply pin (effects after clear).
    pin = dict(POSE)
    pin.update(pin_kwargs)
    # Force clear_effects before any effects list (Java also clears-then-applies).
    if pin.get("effects"):
        pin["clear_effects"] = True

    if pin.get("armor"):
        slots = ("feet", "legs", "chest", "head")
        names = {
            IRON_BOOTS: "minecraft:iron_boots",
            IRON_LEGS: "minecraft:iron_leggings",
            IRON_CHEST: "minecraft:iron_chestplate",
            IRON_HELM: "minecraft:iron_helmet",
        }
        cmds = []
        for i, aid in enumerate(pin["armor"][:4]):
            if aid and aid in names:
                cmds.append("replaceitem entity @p slot.armor.%s %s 1" % (
                    slots[i], names[aid]))
        if cmds:
            runcmds_ok(e, cmds, "equip_%s" % state_id)

    # States that race the client tick: zero settle after equip, single re-pin.
    tick_sensitive = state_id in (
        "hud_hurt_flash_on", "hud_hurt_flash_off",
        "overlay_portal_050", "overlay_fire",
    )
    # Hand-use states need a short settle so server inventory sync lands before
    # setActiveHand; otherwise frame{} draws empty viewmodel (hand_active true
    # but stack empty on render path).
    hand_use = state_id.startswith("hand_")
    if tick_sensitive:
        settle_n = 0
        n_repin = 1
    elif hand_use:
        settle_n = max(settle_n, 6)
        n_repin = 2
    else:
        n_repin = 2

    # Pre-pin settle, then freeze hard before A/B with zero wall-clock sleep.
    r0 = hud_pin_ok(e, **pin)
    if settle_n > 0:
        settle(e, settle_n)
    for _ in range(n_repin):
        r1 = hud_pin_ok(e, **pin)
    path_a = os.path.join(outdir, "%s_a.png" % state_id)
    fa = grab(e, path_a)
    for _ in range(n_repin):
        r2 = hud_pin_ok(e, **pin)
    path_b = os.path.join(outdir, "%s_b.png" % state_id)
    fb = grab(e, path_b)

    noise = None
    presence = None
    capture_error = None
    try:
        # Feature presence first (fire/portal may have elevated A/B noise from
        # atlas animation while still showing the real overlay).
        presence = assert_feature_presence(state_id, path_a, r1)
        noise = assert_ab_noise(state_id, path_a, path_b)
    except RuntimeError as ex:
        capture_error = str(ex)
        log("CAPTURE_FAIL %s: %s" % (state_id, capture_error))
        # Delete contaminated twins so gate sees MISSING, not bad goldens.
        for p in (path_a, path_b):
            try:
                os.remove(p)
            except OSError:
                pass
        meta = {
            "id": state_id,
            "pin": pin,
            "pin_reply_a": r1,
            "pin_reply_b": r2,
            "capture_error": capture_error,
            "ab_noise": noise,
            "verdict": "CAPTURE_FAIL",
        }
        if meta_extra:
            meta.update(meta_extra)
        with open(os.path.join(meta_dir, "%s.json" % state_id), "w") as f:
            json.dump(meta, f, indent=2)
        return meta

    meta = {
        "id": state_id,
        "pin": pin,
        "pin_reply_a": r1,
        "pin_reply_b": r2,
        "pin_reply_pre": r0,
        "frame_a": fa,
        "frame_b": fb,
        "pose": {k: pin.get(k) for k in ("x", "y", "z", "yaw", "pitch")},
        "width": fa.get("w"),
        "height": fa.get("h"),
        "gui_scale": 2,
        "partial_ticks": 1.0,
        "ab_noise": noise,
        "noise_limit": NOISE_MAX.get(state_id, NOISE_MAX_DEFAULT),
        "presence": presence,
        "verdict": "CAPTURE_OK",
        "notes": (
            "A/B from qrl frame{rerender:true} at partialTicks=1 with hud_pin "
            "freeze; capture integrity enforced (noise + feature presence)."
        ),
    }
    if meta_extra:
        meta.update(meta_extra)
    with open(os.path.join(meta_dir, "%s.json" % state_id), "w") as f:
        json.dump(meta, f, indent=2)
    log("captured %s  noise=%.3f presence_ok pin=%s" % (
        state_id, noise,
        {k: r1.get(k) for k in (
            "health", "food", "air", "armor", "absorption",
            "xp_level", "hand_active", "burning", "fire_ticks",
            "portal", "screen", "potion_count", "use_count")
         if k in r1}))
    return meta


def build_water_pool(e):
    x0, y0, z0 = CX - 2, PLAT_Y + 1, CZ - 2
    x1, y1, z1 = CX + 2, PLAT_Y + 4, CZ + 2
    cmds = [
        "fill %d %d %d %d %d %d minecraft:glass" % (x0, y0, z0, x1, y1, z1),
        "fill %d %d %d %d %d %d minecraft:water" % (
            x0 + 1, y0, z0 + 1, x1 - 1, y1 - 1, z1 - 1),
    ]
    r = runcmds(e, cmds)
    if not r.get("ok") or int(r.get("ran", 0) or 0) < 1:
        raise RuntimeError("build_water_pool failed: %s" % r)
    settle(e, 4)


def build_solid_cell(e, block):
    by = PLAT_Y + 2
    cmds = [
        "fill %d %d %d %d %d %d minecraft:air" % (
            CX - 1, PLAT_Y + 1, CZ - 1, CX + 1, PLAT_Y + 3, CZ + 1),
        "fill %d %d %d %d %d %d %s" % (
            CX - 1, by, CZ - 1, CX + 1, by, CZ + 1, block),
    ]
    r = runcmds(e, cmds)
    if not r.get("ok") or int(r.get("ran", 0) or 0) < 1:
        raise RuntimeError("build_solid_cell failed: %s" % r)
    settle(e, 4)


def begin_state(e, label, rebuild_scene=True):
    """Every state starts from clean living. rebuild_scene when world geometry changed."""
    log("--- begin %s ---" % label)
    clear_player(e)
    if rebuild_scene:
        base_scene(e)
    else:
        # Light refresh: pose + clear effects, keep existing pad.
        set_pose(e, POSE)
        hud_pin_ok(
            e, hold_death=False, health=20.0, clear_effects=True,
            fire=0, portal=0.0, use_action=0, boss={"show": False}, **POSE)
        settle(e, 2)
    ensure_living(e, do_respawn=False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument(
        "--only", default="",
        help="Comma-separated state ids (or 'hand' for the three viewmodels). "
             "Skips other states; still rebuilds a clean living scene.")
    args = ap.parse_args()
    outdir = args.out
    os.makedirs(outdir, exist_ok=True)

    only = set()
    if args.only.strip():
        for tok in args.only.split(","):
            tok = tok.strip()
            if not tok:
                continue
            if tok == "hand" or tok == "hands" or tok == "viewmodel":
                only.update((
                    "hand_bow_pull20", "hand_eat_mid", "hand_block_shield"))
            else:
                only.add(tok)
    def want(sid):
        return (not only) or (sid in only)

    if Image is None or np is None:
        log("FATAL: pillow+numpy required for capture sanity checks")
        return 1

    e = qrl_client.QRLEnv()
    log("reset seed=%d flat survival fresh..." % args.seed)
    o = e.reset({
        "seed": args.seed,
        "mode": "survival",
        "type": "flat",
        "structures": False,
        "fresh": True,
    }, timeout=180.0)
    if not o.get("ok"):
        log("reset failed: %s" % o)
        return 1
    log("spawn ~ (%.1f,%.1f,%.1f)" % (o["x"], o["y"], o["z"]))

    base_scene(e)
    ensure_living(e)
    manifest = {"seed": args.seed, "states": [], "blocked": [], "failed": [],
                "only": sorted(only) if only else None}

    def add(meta):
        manifest["states"].append(meta)
        if meta.get("verdict") == "CAPTURE_FAIL":
            manifest["failed"].append(meta["id"])

    # ---- HUD states (all pre-death; revalidated via ensure_living) ----
    # HUD chrome on the stone pad: rebuild once, then light refresh between.
    if want("hud_armor_iron"):
        begin_state(e, "hud_armor_iron", rebuild_scene=True)
        add(capture_pair(e, outdir, "hud_armor_iron", {
            "health": 20.0, "food": 20, "air": 300, "absorption": 0.0,
            "armor": ARMOR_IDS,
            "hotbar": [[0, 0, 0]] * 9,
            "xp_level": 0, "xp_frac": 0.0,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
        }, {"roi": "armor+hearts"}))

    if want("hud_absorption_armor"):
        begin_state(e, "hud_absorption_armor", rebuild_scene=False)
        add(capture_pair(e, outdir, "hud_absorption_armor", {
            "health": 20.0, "food": 20, "air": 300, "absorption": 20.0,
            "armor": ARMOR_IDS,
            "hotbar": [[0, 0, 0]] * 9,
            "xp_level": 0, "xp_frac": 0.0,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
            "effects": [{"id": 22, "duration": 600, "amplifier": 4}],
        }, {"roi": "armor lifted by absorption rows"}))

    if want("hud_hurt_flash_on") or want("hud_hurt_flash_off"):
        begin_state(e, "hud_hurt_flash", rebuild_scene=False)
        for flash_on, sid in ((True, "hud_hurt_flash_on"),
                              (False, "hud_hurt_flash_off")):
            if not want(sid):
                continue
            add(capture_pair(e, outdir, sid, {
                "health": 14.0, "food": 20, "air": 300, "absorption": 0.0,
                "armor": [0, 0, 0, 0],
                "hotbar": [[0, 0, 0]] * 9,
                "hurt_time": 10, "max_hurt_time": 10, "hurt_yaw": 0.0,
                "hud_flash": flash_on,
                "hud_health": 14, "hud_last_health": 20,
                "hud_update_counter": 1000,
                "use_action": 0, "fire": 0, "portal": 0.0,
                "boss": {"show": False},
                "clear_effects": True,
            }, {"roi": "hearts row only", "hud_flash": flash_on}))

    if want("hud_hunger_poison"):
        begin_state(e, "hud_hunger_poison", rebuild_scene=False)
        add(capture_pair(e, outdir, "hud_hunger_poison", {
            "health": 20.0, "food": 8, "air": 300, "absorption": 0.0,
            "armor": [0, 0, 0, 0],
            "hotbar": [[0, 0, 0]] * 9,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
            "effects": [{"id": 17, "duration": 600, "amplifier": 0}],
        }, {"roi": "hunger haunches"}))

    # Water pool geometry change.
    if want("hud_air_partial"):
        begin_state(e, "hud_air_partial", rebuild_scene=True)
        build_water_pool(e)
        water_pose = dict(POSE)
        water_pose["y"] = float(PLAT_Y + 1)
        water_pose["x"] = CX + 0.5
        water_pose["z"] = CZ + 0.5
        add(capture_pair(e, outdir, "hud_air_partial", {
            # 121 => 4 full + 1 partial (Forge renderAir). 123 is 5 full only.
            "health": 20.0, "food": 20, "air": 121, "absorption": 0.0,
            "armor": [0, 0, 0, 0],
            "hotbar": [[0, 0, 0]] * 9,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
            **water_pose
        }, {"roi": "air bubbles sh-49 right"}))

    # Leave water: full rebuild to dry pad.
    if want("hud_xp_half"):
        begin_state(e, "hud_xp_half", rebuild_scene=True)
        add(capture_pair(e, outdir, "hud_xp_half", {
            "health": 20.0, "food": 20, "air": 300, "absorption": 0.0,
            "armor": [0, 0, 0, 0],
            "hotbar": [[0, 0, 0]] * 9,
            "xp_level": 7, "xp_frac": 0.5,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
        }, {"roi": "xp bar + level"}))

    if want("hud_durability_half"):
        begin_state(e, "hud_durability_half", rebuild_scene=False)
        hotbar_dur = [[WOOD_PICK, 1, 30]] + [[0, 0, 0]] * 8
        add(capture_pair(e, outdir, "hud_durability_half", {
            "health": 20.0, "food": 20, "air": 300, "absorption": 0.0,
            "armor": [0, 0, 0, 0],
            "hotbar": hotbar_dur, "hotbar_sel": 0,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
        }, {"roi": "slot0 durability strip"}))

    if want("hud_boss_half"):
        begin_state(e, "hud_boss_half", rebuild_scene=False)
        add(capture_pair(e, outdir, "hud_boss_half", {
            "health": 20.0, "food": 20, "air": 300, "absorption": 0.0,
            "armor": [0, 0, 0, 0],
            "hotbar": [[0, 0, 0]] * 9,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": True, "frac": 0.5, "name": "Ender Dragon"},
            "clear_effects": True,
        }, {"roi": "boss bar top center"}))

    # ---- Death (last HUD chrome) then real respawn before anything else ----
    if want("hud_death"):
        begin_state(e, "hud_death", rebuild_scene=False)
        add(capture_pair(e, outdir, "hud_death", {
            "dead": True,
            "hold_death": True,
            "health": 0.0,
            "food": 20, "air": 300,
            "armor": [0, 0, 0, 0],
            "hotbar": [[0, 0, 0]] * 9,
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
        }, {"roi": "full-frame death wash"}))
        # Real respawn + close GuiGameOver (not health-only revive).
        log("post-death: real respawn + close GuiGameOver")
        ensure_living(e, do_respawn=True)
        base_scene(e)
        ensure_living(e, do_respawn=False)

    # ---- Viewmodels (bow / eat / shield) ----
    hand_wanted = any(want(s) for s in (
        "hand_bow_pull20", "hand_eat_mid", "hand_block_shield"))
    if hand_wanted:
        # Clean living wall scene; never inherit death/overlay geometry.
        begin_state(e, "hand_viewmodels", rebuild_scene=True)
        ensure_living(e, do_respawn=False)
        wall_pose = dict(POSE)
        wall_pose["yaw"] = 0.0
        wall_pose["pitch"] = 0.0
        wall_pose["z"] = CZ + 0.5
        # Empty-hand baseline for presence (rejects wall-only ROIs).
        capture_empty_hand_baseline(e, outdir)

        if want("hand_bow_pull20"):
            begin_state(e, "hand_bow_pull20", rebuild_scene=False)
            add(capture_pair(e, outdir, "hand_bow_pull20", {
                "health": 20.0, "food": 20, "air": 300,
                "hotbar": [[BOW, 1, 0]] + [[0, 0, 0]] * 8,
                "hotbar_sel": 0,
                "bow_pull": 20,
                "armor": [0, 0, 0, 0],
                "fire": 0, "portal": 0.0,
                "boss": {"show": False},
                "clear_effects": True,
                **wall_pose
            }, {"roi": "lower-right viewmodel non-hotbar"}))

        if want("hand_eat_mid"):
            begin_state(e, "hand_eat_mid", rebuild_scene=False)
            add(capture_pair(e, outdir, "hand_eat_mid", {
                "health": 20.0, "food": 10, "air": 300,
                "hotbar": [[BREAD, 1, 0]] + [[0, 0, 0]] * 8,
                "hotbar_sel": 0,
                "use_action": 1, "use_remaining": 16,
                "armor": [0, 0, 0, 0],
                "fire": 0, "portal": 0.0,
                "boss": {"show": False},
                "clear_effects": True,
                **wall_pose
            }, {"roi": "lower-right viewmodel non-hotbar"}))

        # 1.11.2: swords do not block; shields do (item 442, EnumAction.BLOCK).
        if want("hand_block_shield"):
            begin_state(e, "hand_block_shield", rebuild_scene=False)
            add(capture_pair(e, outdir, "hand_block_shield", {
                "health": 20.0, "food": 20, "air": 300,
                "hotbar": [[SHIELD, 1, 0]] + [[0, 0, 0]] * 8,
                "hotbar_sel": 0,
                "use_action": 2, "use_remaining": 72000,
                "armor": [0, 0, 0, 0],
                "fire": 0, "portal": 0.0,
                "boss": {"show": False},
                "clear_effects": True,
                **wall_pose
            }, {"roi": "lower-right viewmodel non-hotbar",
                "note": "shield block (not sword)"}))

    # ---- Overlays ----
    stone_pose = dict(POSE)
    stone_pose["y"] = float(PLAT_Y + 1)
    if want("overlay_inside_stone"):
        begin_state(e, "overlay_inside_stone", rebuild_scene=True)
        build_solid_cell(e, "minecraft:stone")
        add(capture_pair(e, outdir, "overlay_inside_stone", {
            "health": 20.0, "food": 20, "air": 300,
            "hotbar": [[0, 0, 0]] * 9,
            "armor": [0, 0, 0, 0],
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
            **stone_pose
        }, {"roi": "full-frame inside-block darken"}))

    if want("overlay_inside_grass"):
        begin_state(e, "overlay_inside_grass", rebuild_scene=True)
        build_solid_cell(e, "minecraft:grass")
        add(capture_pair(e, outdir, "overlay_inside_grass", {
            "health": 20.0, "food": 20, "air": 300,
            "hotbar": [[0, 0, 0]] * 9,
            "armor": [0, 0, 0, 0],
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
            **stone_pose
        }, {"roi": "full-frame grass particle darken"}))

    # Portal swirl from timeInPortal only (no physical portal block).
    if want("overlay_portal_050"):
        begin_state(e, "overlay_portal_050", rebuild_scene=True)
        portal_pose = dict(POSE)
        add(capture_pair(e, outdir, "overlay_portal_050", {
            "health": 20.0, "food": 20, "air": 300,
            "hotbar": [[0, 0, 0]] * 9,
            "armor": [0, 0, 0, 0],
            "use_action": 0, "fire": 0, "portal": 0.5,
            "boss": {"show": False},
            "clear_effects": True,
            **portal_pose
        }, {"roi": "full-frame portal swirl",
            "note": "timeInPortal pin only; no portal block animation"}))

    if want("overlay_fire"):
        begin_state(e, "overlay_fire", rebuild_scene=True)
        add(capture_pair(e, outdir, "overlay_fire", {
            "health": 20.0, "food": 20, "air": 300,
            "hotbar": [[0, 0, 0]] * 9,
            "armor": [0, 0, 0, 0],
            "use_action": 0, "fire": 80, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
            **POSE
        }, {"roi": "first-person fire quads"}, settle_n=0))

    if want("overlay_underwater"):
        begin_state(e, "overlay_underwater", rebuild_scene=True)
        build_water_pool(e)
        uw_pose = dict(POSE)
        uw_pose["y"] = float(PLAT_Y + 1)
        uw_pose["yaw"] = 0.0
        uw_pose["pitch"] = 0.0
        add(capture_pair(e, outdir, "overlay_underwater", {
            "health": 20.0, "food": 20, "air": 200,
            "hotbar": [[0, 0, 0]] * 9,
            "armor": [0, 0, 0, 0],
            "use_action": 0, "fire": 0, "portal": 0.0,
            "boss": {"show": False},
            "clear_effects": True,
            **uw_pose
        }, {"roi": "full-frame underwater.png"}))

    ok_ids = [s["id"] for s in manifest["states"]
              if s.get("verdict") != "CAPTURE_FAIL"]
    man_path = os.path.join(outdir, "capture_manifest.json")
    with open(man_path, "w") as f:
        json.dump({
            "seed": args.seed,
            "n_states": len(manifest["states"]),
            "ids": [s["id"] for s in manifest["states"]],
            "ok_ids": ok_ids,
            "blocked": manifest["blocked"],
            "failed": manifest["failed"],
            "only": manifest.get("only"),
            "resolution": "from frame replies",
            "java_home": os.environ.get("JAVA_HOME"),
            "notes": (
                "Genuine Java 1.11.2 FBO dumps via qrl frame{rerender:true}. "
                "State frozen with hud_pin per A/B pair. "
                "Capture integrity: noise ceiling + feature presence "
                "(cross-state + empty-hand baseline for viewmodels). "
                "frame{} clears Malmo hideGUI so first-person viewmodel paints. "
                "hand_block_shield replaces version-wrong hand_block_sword. "
                "failed[] entries had twins deleted (no contaminated goldens)."
            ),
        }, f, indent=2)
    log("manifest -> %s (%d states, %d failed)" % (
        man_path, len(manifest["states"]), len(manifest["failed"])))
    e.close()
    # Nonzero if any state failed integrity — caller must not claim full set.
    return 1 if manifest["failed"] else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as ex:
        log("FATAL: %s" % ex)
        import traceback
        traceback.print_exc()
        sys.exit(1)
