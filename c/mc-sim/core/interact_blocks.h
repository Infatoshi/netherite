/* interact_blocks: pure meta state-machine for door/trapdoor/fencegate/button/lever/plate.
 *
 * PORT (1.11.2 oracle, onBlockActivated / neighbor / press-release meta only):
 *   block/BlockDoor.java              wood click cycle OPEN; iron no-op; neighbor OPEN/POWERED
 *   block/BlockTrapDoor.java          wood click cycle OPEN; iron no-op; neighbor sets OPEN
 *   block/BlockFenceGate.java         click open/close (+ facing flip); neighbor OPEN+POWERED
 *   block/BlockLever.java             click cycle POWERED (all orientations)
 *   block/BlockButton.java            click set POWERED; release clear (wood re-check entity)
 *   block/BlockPressurePlate.java     set POWERED from entity presence (meta 0/1)
 *   block/BlockPressurePlateWeighted  set POWER 0..15 from entity count (ceil formula)
 *
 * Packed state: u16 = (blockId << 4) | (meta & 15). No world, no TE, no sound/neighbor notify.
 *
 * Boundaries (OUT): beds, chests, furnaces, hoppers, redstone dust/torch/repeater, pistons,
 * noteblock, droppers, brewing stands, multi-block door half pairing in a live world (we operate
 * on one packed half at a time), IN_WALL fence-gate attach, button attachment survival checks.
 *
 * CPU==CUDA from one MC_HD source; Java golden mirrors the same battery. */
#ifndef MC_INTERACT_BLOCKS_H
#define MC_INTERACT_BLOCKS_H

#include "mc.h"

/* Vanilla 1.11.2 numeric block ids (Block.java registerBlock). */
enum {
    IB_WOODEN_DOOR      = 64,
    IB_LEVER            = 69,
    IB_STONE_PLATE      = 70,
    IB_IRON_DOOR        = 71,
    IB_WOODEN_PLATE     = 72,
    IB_STONE_BUTTON     = 77,
    IB_TRAPDOOR         = 96,
    IB_FENCE_GATE       = 107,
    IB_WOODEN_BUTTON    = 143,
    IB_LIGHT_PLATE      = 147,  /* gold, maxWeight 15 */
    IB_HEAVY_PLATE      = 148,  /* iron, maxWeight 150 */
    IB_IRON_TRAPDOOR    = 167
};

/* Action kinds for the pure SM. */
enum {
    IB_ACT_CLICK    = 0, /* onBlockActivated */
    IB_ACT_NEIGHBOR = 1, /* neighbor power change; arg0 = powered 0/1 */
    IB_ACT_ENTITY   = 2, /* entity press/collide; arg0 = present (0/1) or entity count */
    IB_ACT_RELEASE  = 3  /* scheduled tick / unpress; arg0 = entity present (wood button) */
};

/* Horizontal facing index (EnumFacing.getHorizontalIndex): SOUTH=0 WEST=1 NORTH=2 EAST=3. */
enum {
    IB_H_SOUTH = 0,
    IB_H_WEST  = 1,
    IB_H_NORTH = 2,
    IB_H_EAST  = 3
};

/* Per-case input (identical tape in Golden.java). */
typedef struct {
    i32 block_id;
    i32 meta_in;   /* 0..15 */
    i32 action;    /* IB_ACT_* */
    i32 arg0;      /* click: player h-facing 0..3 for fence gate; neighbor: powered;
                    * entity: count/present; release: entity present for wood button */
} IbCase;

/* Result of one transition. */
typedef struct {
    i32 meta_out;    /* 0..15 */
    i32 is_open;     /* 0/1 */
    i32 is_powered;  /* 0/1 (weighted: meta>0) */
    i32 accepted;    /* 1 if click returned true / state considered handled */
} IbResult;

/* Fields emitted per case: packed_out, is_open, is_powered, accepted. */
enum {
    IB_FIELDS   = 4,
    IB_NCASES   = 48,
    IB_OUT      = (IB_NCASES * IB_FIELDS)
};

MC_HD static inline u16 ib_pack(i32 id, i32 meta) {
    return (u16)(((id & 0xFFF) << 4) | (meta & 15));
}

MC_HD static inline i32 ib_id(u16 p)  { return (i32)(p >> 4); }
MC_HD static inline i32 ib_meta(u16 p) { return (i32)(p & 15); }

/* ---- classifiers ---- */

MC_HD static inline int ib_is_wood_door(i32 id) {
    return id == IB_WOODEN_DOOR;
}
MC_HD static inline int ib_is_iron_door(i32 id) {
    return id == IB_IRON_DOOR;
}
MC_HD static inline int ib_is_door(i32 id) {
    return ib_is_wood_door(id) || ib_is_iron_door(id);
}
MC_HD static inline int ib_is_wood_trap(i32 id) {
    return id == IB_TRAPDOOR;
}
MC_HD static inline int ib_is_iron_trap(i32 id) {
    return id == IB_IRON_TRAPDOOR;
}
MC_HD static inline int ib_is_trap(i32 id) {
    return ib_is_wood_trap(id) || ib_is_iron_trap(id);
}
MC_HD static inline int ib_is_gate(i32 id) {
    return id == IB_FENCE_GATE;
}
MC_HD static inline int ib_is_lever(i32 id) {
    return id == IB_LEVER;
}
MC_HD static inline int ib_is_stone_btn(i32 id) {
    return id == IB_STONE_BUTTON;
}
MC_HD static inline int ib_is_wood_btn(i32 id) {
    return id == IB_WOODEN_BUTTON;
}
MC_HD static inline int ib_is_btn(i32 id) {
    return ib_is_stone_btn(id) || ib_is_wood_btn(id);
}
MC_HD static inline int ib_is_bin_plate(i32 id) {
    return id == IB_STONE_PLATE || id == IB_WOODEN_PLATE;
}
MC_HD static inline int ib_is_weighted(i32 id) {
    return id == IB_LIGHT_PLATE || id == IB_HEAVY_PLATE;
}
MC_HD static inline int ib_plate_max_weight(i32 id) {
    if (id == IB_LIGHT_PLATE) return 15;
    if (id == IB_HEAVY_PLATE) return 150;
    return 0;
}

/* MathHelper.ceil(float) */
MC_HD static inline i32 ib_ceil_f(float v) {
    i32 i = (i32)v;
    return (v > (float)i) ? i + 1 : i;
}

/* Weighted plate: min(count,maxW)/maxW * 15, ceil. */
MC_HD static inline i32 ib_weighted_strength(i32 count, i32 max_w) {
    i32 i;
    float f;
    if (count < 0) count = 0;
    if (max_w <= 0) return 0;
    i = count < max_w ? count : max_w;
    if (i <= 0) return 0;
    f = (float)i / (float)max_w;
    return ib_ceil_f(f * 15.0f);
}

/* ---- bit queries on meta ---- */

MC_HD static inline int ib_door_is_upper(i32 meta) { return (meta & 8) != 0; }
MC_HD static inline int ib_door_open(i32 meta) {
    /* OPEN lives on lower half only. */
    return !ib_door_is_upper(meta) && (meta & 4) != 0;
}
MC_HD static inline int ib_door_powered(i32 meta) {
    /* POWERED lives on upper half only. */
    return ib_door_is_upper(meta) && (meta & 2) != 0;
}
MC_HD static inline int ib_trap_open(i32 meta) { return (meta & 4) != 0; }
MC_HD static inline int ib_gate_open(i32 meta) { return (meta & 4) != 0; }
MC_HD static inline int ib_gate_powered(i32 meta) { return (meta & 8) != 0; }
MC_HD static inline int ib_lever_powered(i32 meta) { return (meta & 8) != 0; }
MC_HD static inline int ib_btn_powered(i32 meta) { return (meta & 8) != 0; }
MC_HD static inline int ib_bin_plate_powered(i32 meta) { return meta == 1; }

MC_HD static inline void ib_fill_flags(i32 id, i32 meta, IbResult *r) {
    r->is_open = 0;
    r->is_powered = 0;
    if (ib_is_door(id)) {
        r->is_open = ib_door_open(meta) ? 1 : 0;
        r->is_powered = ib_door_powered(meta) ? 1 : 0;
    } else if (ib_is_trap(id)) {
        r->is_open = ib_trap_open(meta) ? 1 : 0;
        r->is_powered = r->is_open; /* trapdoor has no separate POWERED; open tracks redstone */
    } else if (ib_is_gate(id)) {
        r->is_open = ib_gate_open(meta) ? 1 : 0;
        r->is_powered = ib_gate_powered(meta) ? 1 : 0;
    } else if (ib_is_lever(id)) {
        r->is_powered = ib_lever_powered(meta) ? 1 : 0;
    } else if (ib_is_btn(id)) {
        r->is_powered = ib_btn_powered(meta) ? 1 : 0;
    } else if (ib_is_bin_plate(id)) {
        r->is_powered = ib_bin_plate_powered(meta) ? 1 : 0;
    } else if (ib_is_weighted(id)) {
        r->is_powered = (meta > 0) ? 1 : 0;
    }
}

/* ---- transitions ---- */

/* Door click: wood lower half toggles OPEN (bit 4). Iron no-op. Upper half has no OPEN bit. */
MC_HD static inline IbResult ib_door_click(i32 id, i32 meta) {
    IbResult r;
    r.meta_out = meta & 15;
    r.accepted = 0;
    if (ib_is_iron_door(id)) {
        ib_fill_flags(id, r.meta_out, &r);
        return r; /* return false in vanilla */
    }
    if (ib_door_is_upper(meta)) {
        /* Pure SM: no world lower half; treat as no-op. */
        r.accepted = 1; /* wood still "handles" click path only when lower resolved */
        ib_fill_flags(id, r.meta_out, &r);
        /* Match vanilla: if we only have upper, we cannot cycle OPEN. accepted=0. */
        r.accepted = 0;
        return r;
    }
    r.meta_out = (meta & 15) ^ 4; /* cycleProperty(OPEN) */
    r.accepted = 1;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Door neighbor: lower sets OPEN from powered; upper sets POWERED from powered. */
MC_HD static inline IbResult ib_door_neighbor(i32 id, i32 meta, i32 powered) {
    IbResult r;
    i32 m = meta & 15;
    i32 flag = powered ? 1 : 0;
    r.accepted = 1;
    if (ib_door_is_upper(m)) {
        if (flag) m = m | 2;
        else m = m & ~2;
    } else {
        if (flag) m = m | 4;
        else m = m & ~4;
    }
    r.meta_out = m & 15;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Trapdoor click: wood cycle OPEN; iron no-op. */
MC_HD static inline IbResult ib_trap_click(i32 id, i32 meta) {
    IbResult r;
    r.meta_out = meta & 15;
    if (ib_is_iron_trap(id)) {
        r.accepted = 0;
        ib_fill_flags(id, r.meta_out, &r);
        return r;
    }
    r.meta_out = (meta & 15) ^ 4;
    r.accepted = 1;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Trapdoor neighbor: set OPEN = powered. */
MC_HD static inline IbResult ib_trap_neighbor(i32 id, i32 meta, i32 powered) {
    IbResult r;
    i32 m = meta & 15;
    if (powered) m = m | 4;
    else m = m & ~4;
    r.meta_out = m;
    r.accepted = 1;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Fence gate click: close if open; else maybe flip facing then open.
 * arg0 = player horizontal facing index (fromAngle(yaw)). */
MC_HD static inline IbResult ib_gate_click(i32 id, i32 meta, i32 player_h) {
    IbResult r;
    i32 m = meta & 15;
    i32 facing = m & 3;
    i32 open = (m & 4) != 0;
    i32 powered = (m & 8) != 0;
    player_h &= 3;
    if (open) {
        open = 0;
    } else {
        if (facing == ((player_h + 2) & 3)) /* opposite */
            facing = player_h;
        open = 1;
    }
    m = (facing & 3) | (open ? 4 : 0) | (powered ? 8 : 0);
    r.meta_out = m;
    r.accepted = 1;
    ib_fill_flags(id, r.meta_out, &r);
    (void)id;
    return r;
}

/* Fence gate neighbor: if POWERED != flag, set POWERED=flag and OPEN=flag. */
MC_HD static inline IbResult ib_gate_neighbor(i32 id, i32 meta, i32 powered) {
    IbResult r;
    i32 m = meta & 15;
    i32 flag = powered ? 1 : 0;
    i32 cur_p = (m & 8) != 0;
    if (cur_p != flag) {
        i32 facing = m & 3;
        m = (facing & 3) | (flag ? 4 : 0) | (flag ? 8 : 0);
    }
    r.meta_out = m & 15;
    r.accepted = 1;
    ib_fill_flags(id, r.meta_out, &r);
    (void)id;
    return r;
}

/* Lever click: cycle POWERED (bit 8); orientation (bits 0-2) preserved. */
MC_HD static inline IbResult ib_lever_click(i32 id, i32 meta) {
    IbResult r;
    r.meta_out = (meta & 15) ^ 8;
    r.accepted = 1;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Button click: if already powered, no state change (still accepted); else set powered. */
MC_HD static inline IbResult ib_btn_click(i32 id, i32 meta) {
    IbResult r;
    i32 m = meta & 15;
    r.accepted = 1;
    if ((m & 8) == 0)
        m = m | 8;
    r.meta_out = m;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Button release: stone always unpress; wood unpress only if !entity (arg0). */
MC_HD static inline IbResult ib_btn_release(i32 id, i32 meta, i32 entity_present) {
    IbResult r;
    i32 m = meta & 15;
    r.accepted = 1;
    if ((m & 8) != 0) {
        if (ib_is_wood_btn(id)) {
            if (!entity_present)
                m = m & ~8;
            /* else stays pressed (checkPressed re-schedules) */
        } else {
            m = m & ~8;
        }
    }
    r.meta_out = m;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Wood button entity collide: if unpowered and present, press. */
MC_HD static inline IbResult ib_btn_entity(i32 id, i32 meta, i32 present) {
    IbResult r;
    i32 m = meta & 15;
    r.accepted = 1;
    if (ib_is_wood_btn(id) && present && (m & 8) == 0)
        m = m | 8;
    r.meta_out = m;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Binary pressure plate: meta = (count>0)?1:0. */
MC_HD static inline IbResult ib_bin_plate_set(i32 id, i32 meta, i32 count) {
    IbResult r;
    (void)meta;
    r.meta_out = (count > 0) ? 1 : 0;
    r.accepted = 1;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Weighted pressure plate: meta = strength 0..15 from count. */
MC_HD static inline IbResult ib_w_plate_set(i32 id, i32 meta, i32 count) {
    IbResult r;
    (void)meta;
    r.meta_out = ib_weighted_strength(count, ib_plate_max_weight(id)) & 15;
    r.accepted = 1;
    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Dispatch one case. Unknown id: identity, accepted=0. */
MC_HD static inline IbResult ib_apply(const IbCase *c) {
    IbResult r;
    i32 id = c->block_id;
    i32 meta = c->meta_in & 15;
    i32 act = c->action;
    i32 a0 = c->arg0;

    r.meta_out = meta;
    r.is_open = 0;
    r.is_powered = 0;
    r.accepted = 0;

    if (ib_is_door(id)) {
        if (act == IB_ACT_CLICK) return ib_door_click(id, meta);
        if (act == IB_ACT_NEIGHBOR) return ib_door_neighbor(id, meta, a0);
    } else if (ib_is_trap(id)) {
        if (act == IB_ACT_CLICK) return ib_trap_click(id, meta);
        if (act == IB_ACT_NEIGHBOR) return ib_trap_neighbor(id, meta, a0);
    } else if (ib_is_gate(id)) {
        if (act == IB_ACT_CLICK) return ib_gate_click(id, meta, a0);
        if (act == IB_ACT_NEIGHBOR) return ib_gate_neighbor(id, meta, a0);
    } else if (ib_is_lever(id)) {
        if (act == IB_ACT_CLICK) return ib_lever_click(id, meta);
    } else if (ib_is_btn(id)) {
        if (act == IB_ACT_CLICK) return ib_btn_click(id, meta);
        if (act == IB_ACT_RELEASE) return ib_btn_release(id, meta, a0);
        if (act == IB_ACT_ENTITY) return ib_btn_entity(id, meta, a0);
    } else if (ib_is_bin_plate(id)) {
        if (act == IB_ACT_ENTITY || act == IB_ACT_RELEASE)
            return ib_bin_plate_set(id, meta, a0);
    } else if (ib_is_weighted(id)) {
        if (act == IB_ACT_ENTITY || act == IB_ACT_RELEASE)
            return ib_w_plate_set(id, meta, a0);
    }

    ib_fill_flags(id, r.meta_out, &r);
    return r;
}

/* Fixed battery tape (must match Golden.java exactly). */
MC_HD static inline void ib_get_cases(IbCase *t) {
    int i = 0;
    /* --- wooden door lower: facing=0 closed; open; re-close; iron no-op --- */
    t[i++] = (IbCase){IB_WOODEN_DOOR, 0,  IB_ACT_CLICK, 0};       /* 0: open */
    t[i++] = (IbCase){IB_WOODEN_DOOR, 4,  IB_ACT_CLICK, 0};       /* 1: close */
    t[i++] = (IbCase){IB_WOODEN_DOOR, 1,  IB_ACT_CLICK, 0};       /* 2: facing1 open */
    t[i++] = (IbCase){IB_IRON_DOOR,   0,  IB_ACT_CLICK, 0};       /* 3: iron refuse */
    t[i++] = (IbCase){IB_WOODEN_DOOR, 0,  IB_ACT_NEIGHBOR, 1};    /* 4: power open */
    t[i++] = (IbCase){IB_WOODEN_DOOR, 4,  IB_ACT_NEIGHBOR, 0};    /* 5: unpower close */
    t[i++] = (IbCase){IB_IRON_DOOR,   0,  IB_ACT_NEIGHBOR, 1};    /* 6: iron power open */
    t[i++] = (IbCase){IB_WOODEN_DOOR, 8,  IB_ACT_NEIGHBOR, 1};    /* 7: upper set POWERED */
    t[i++] = (IbCase){IB_WOODEN_DOOR, 10, IB_ACT_NEIGHBOR, 0};    /* 8: upper clear POWERED (8|2) */
    t[i++] = (IbCase){IB_WOODEN_DOOR, 8,  IB_ACT_CLICK, 0};       /* 9: upper click no-op */

    /* --- trapdoor wood/iron --- */
    t[i++] = (IbCase){IB_TRAPDOOR,      0,  IB_ACT_CLICK, 0};    /* 10: open bottom */
    t[i++] = (IbCase){IB_TRAPDOOR,      4,  IB_ACT_CLICK, 0};    /* 11: close */
    t[i++] = (IbCase){IB_TRAPDOOR,      8,  IB_ACT_CLICK, 0};    /* 12: top half open */
    t[i++] = (IbCase){IB_TRAPDOOR,      3,  IB_ACT_CLICK, 0};    /* 13: facing east open */
    t[i++] = (IbCase){IB_IRON_TRAPDOOR, 0,  IB_ACT_CLICK, 0};    /* 14: iron refuse */
    t[i++] = (IbCase){IB_TRAPDOOR,      0,  IB_ACT_NEIGHBOR, 1}; /* 15: power open */
    t[i++] = (IbCase){IB_TRAPDOOR,      4,  IB_ACT_NEIGHBOR, 0}; /* 16: unpower */
    t[i++] = (IbCase){IB_IRON_TRAPDOOR, 0,  IB_ACT_NEIGHBOR, 1}; /* 17: iron power open */

    /* --- fence gate --- */
    t[i++] = (IbCase){IB_FENCE_GATE, 0, IB_ACT_CLICK, IB_H_SOUTH}; /* 18: open south */
    t[i++] = (IbCase){IB_FENCE_GATE, 4, IB_ACT_CLICK, IB_H_SOUTH}; /* 19: close */
    t[i++] = (IbCase){IB_FENCE_GATE, 0, IB_ACT_CLICK, IB_H_NORTH}; /* 20: open + flip facing S->N */
    t[i++] = (IbCase){IB_FENCE_GATE, 2, IB_ACT_CLICK, IB_H_SOUTH}; /* 21: facing N, player S -> flip */
    t[i++] = (IbCase){IB_FENCE_GATE, 1, IB_ACT_CLICK, IB_H_WEST};  /* 22: facing W player W no flip */
    t[i++] = (IbCase){IB_FENCE_GATE, 0, IB_ACT_NEIGHBOR, 1};       /* 23: power open+powered */
    t[i++] = (IbCase){IB_FENCE_GATE, 12, IB_ACT_NEIGHBOR, 0};      /* 24: unpower close (open|powered) */
    t[i++] = (IbCase){IB_FENCE_GATE, 4, IB_ACT_NEIGHBOR, 1};       /* 25: open but !powered -> set both */

    /* --- lever all orientations 0..7 --- */
    t[i++] = (IbCase){IB_LEVER, 0, IB_ACT_CLICK, 0}; /* 26: down_x on */
    t[i++] = (IbCase){IB_LEVER, 8, IB_ACT_CLICK, 0}; /* 27: off */
    t[i++] = (IbCase){IB_LEVER, 1, IB_ACT_CLICK, 0}; /* 28: east on */
    t[i++] = (IbCase){IB_LEVER, 2, IB_ACT_CLICK, 0}; /* 29: west on */
    t[i++] = (IbCase){IB_LEVER, 3, IB_ACT_CLICK, 0}; /* 30: south on */
    t[i++] = (IbCase){IB_LEVER, 4, IB_ACT_CLICK, 0}; /* 31: north on */
    t[i++] = (IbCase){IB_LEVER, 5, IB_ACT_CLICK, 0}; /* 32: up_z on */
    t[i++] = (IbCase){IB_LEVER, 6, IB_ACT_CLICK, 0}; /* 33: up_x on */
    t[i++] = (IbCase){IB_LEVER, 7, IB_ACT_CLICK, 0}; /* 34: down_z on */

    /* --- stone / wood button --- */
    t[i++] = (IbCase){IB_STONE_BUTTON,  5, IB_ACT_CLICK, 0};     /* 35: press up-facing */
    t[i++] = (IbCase){IB_STONE_BUTTON, 13, IB_ACT_CLICK, 0};     /* 36: already powered (5|8) */
    t[i++] = (IbCase){IB_STONE_BUTTON, 13, IB_ACT_RELEASE, 0};   /* 37: stone release */
    t[i++] = (IbCase){IB_WOODEN_BUTTON, 1, IB_ACT_CLICK, 0};     /* 38: wood press east */
    t[i++] = (IbCase){IB_WOODEN_BUTTON, 9, IB_ACT_RELEASE, 0};   /* 39: wood release no entity */
    t[i++] = (IbCase){IB_WOODEN_BUTTON, 9, IB_ACT_RELEASE, 1};   /* 40: wood release entity stays */
    t[i++] = (IbCase){IB_WOODEN_BUTTON, 1, IB_ACT_ENTITY, 1};    /* 41: wood entity press */

    /* --- binary pressure plates --- */
    t[i++] = (IbCase){IB_STONE_PLATE,  0, IB_ACT_ENTITY, 1};     /* 42: press */
    t[i++] = (IbCase){IB_STONE_PLATE,  1, IB_ACT_ENTITY, 0};     /* 43: release */
    t[i++] = (IbCase){IB_WOODEN_PLATE, 0, IB_ACT_ENTITY, 3};     /* 44: press multi-entity */

    /* --- weighted plates (light maxW=15, heavy maxW=150) --- */
    t[i++] = (IbCase){IB_LIGHT_PLATE, 0, IB_ACT_ENTITY, 0};      /* 45: empty */
    t[i++] = (IbCase){IB_LIGHT_PLATE, 0, IB_ACT_ENTITY, 1};      /* 46: 1/15 *15 = 1 */
    t[i++] = (IbCase){IB_HEAVY_PLATE, 0, IB_ACT_ENTITY, 15};     /* 47: 15/150*15=1.5 ceil=2 */
    /* Exactly IB_NCASES entries (0..47). */
    (void)i;
}

/* Emit packed_out, is_open, is_powered, accepted as u32 each. */
MC_HD static inline void ib_run_battery(u32 *out) {
    IbCase cases[IB_NCASES];
    int c;
    ib_get_cases(cases);
    for (c = 0; c < IB_NCASES; ++c) {
        IbResult r = ib_apply(&cases[c]);
        int o = c * IB_FIELDS;
        out[o + 0] = (u32)ib_pack(cases[c].block_id, r.meta_out);
        out[o + 1] = (u32)r.is_open;
        out[o + 2] = (u32)r.is_powered;
        out[o + 3] = (u32)r.accepted;
    }
}

#endif /* MC_INTERACT_BLOCKS_H */
