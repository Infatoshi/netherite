/* The animated atlas sprites: TextureAtlasSprite.updateAnimation (water, lava,
 * fire, the portal), TextureClock and TextureCompass, TextureMap's
 * updateAnimations over both ticked atlases, and the re-upload each one does
 * (TextureUtil.func_147955_a), as a patch over a recorded atlas.
 *
 * The sources come from a recording's state/anim.json and state/anim.rgba
 * (RenderStateProbe.animFrames: every animated sprite of the blocks and items
 * atlases in listAnimatedSprites order, its AnimationMetadataSection and its
 * level-0 frames). The state is the counters and dial doubles, one struct per
 * moment: a frame row's "anim" block, atlas.json's for the dump itself, or
 * one texanim_tick after another. */
#ifndef NETHERITE_TEXANIM_H
#define NETHERITE_TEXANIM_H

#include "det.h"

#include <stddef.h>
#include <stdint.h>

struct jval;

enum { TEXANIM_MAPS = 2, TEXANIM_MAX_SPRITES = 16, TEXANIM_MAX_FRAMES = 64 };
enum { TEXANIM_BLOCKS = 0, TEXANIM_ITEMS = 1 };
enum { TEXANIM_SPRITE, TEXANIM_CLOCK, TEXANIM_COMPASS };

struct texanim_sprite {
    char name[32];
    int kind, x, y, w, h;
    int frametime;                        /* AnimationMetadataSection.frameTime */
    int nframes;                          /* the frame list */
    int16_t index[TEXANIM_MAX_FRAMES];    /* getFrameIndex */
    int16_t time[TEXANIM_MAX_FRAMES];     /* getFrameTimeSingle */
    int ndata;                            /* framesTextureData.size() */
    const unsigned char *data[TEXANIM_MAX_FRAMES]; /* RGBA, w * h * 4; NULL if never named */
};

struct texanim {
    int n[TEXANIM_MAPS];
    struct texanim_sprite s[TEXANIM_MAPS][TEXANIM_MAX_SPRITES];
    unsigned char *rgba;
};

/* frameCounter and tickCounter per sprite; the clock's field_94239_h and
 * field_94240_i and the compass's currentAngle and angleDelta. */
struct texanim_state {
    int fc[TEXANIM_MAPS][TEXANIM_MAX_SPRITES], tc[TEXANIM_MAPS][TEXANIM_MAX_SPRITES];
    double clock_h, clock_i, compass_a, compass_d;
};

/* What the dials read at the texture tick: whether the client has a world and
 * a player, WorldProvider.isSurfaceWorld, getCelestialAngle(1.0F), the
 * world's spawn point and the player's posX, posZ and rotationYaw. */
struct texanim_world {
    int world, surface;
    float celestial;
    int spawn_x, spawn_z;
    double px, pz, yaw;
};

/* Loads DIR/anim.json and DIR/anim.rgba; 0 on success, -1 when absent. */
int texanim_load(struct texanim *a, const char *dir);
void texanim_free(struct texanim *a);

/* An "anim" block (a frame row's or atlas.json's) into a state; 0 on success. */
int texanim_state_read(const struct texanim *a, const struct jval *anim, struct texanim_state *st);

/* One TextureManager.tick: updateAnimations over the blocks atlas then the
 * items atlas, each sprite in list order. Off the surface the clock and the
 * compass each draw Math.random on the client stream of DET (in list order);
 * DET may be NULL only on the surface. */
void texanim_tick(const struct texanim *a, struct texanim_state *st,
                  const struct texanim_world *w, det_state *det);

/* The plain sprites' counters as TextureAtlasSprite.loadSprite leaves them
 * (frameCounter and tickCounter 0), then N updateAnimation calls each; the
 * dials are left as they are. */
void texanim_sprites_from_start(const struct texanim *a, struct texanim_state *st, int64_t n);

/* The dials alone, for a caller with no sources (the client world's tick):
 * the same arithmetic over a 64-frame clock and a 32-frame compass, the
 * clock first as listAnimatedSprites holds them. */
void texanim_dials_tick(struct texanim_state *st, int clock_frames, int compass_frames,
                        const struct texanim_world *w, det_state *det);

/* The frame index a sprite shows: getFrameIndex(frameCounter) for a plain
 * sprite, frameCounter itself for the dials. */
int texanim_shown(const struct texanim *a, const struct texanim_state *st, int map, int i);

/* Writes every animated sprite of MAP into an RGBA atlas of AW x AH as its
 * current frame. */
void texanim_apply(const struct texanim *a, const struct texanim_state *st, int map,
                   unsigned char *atlas, int aw, int ah);

/* TextureCompass.updateCompass with its last flag set (the angle taken as
 * is) for a given yaw at a position: the frame RenderItemFrame shows. */
int texanim_compass_frame(const struct texanim_world *w, int frames);

/* A recorded scene's patch: loads SCENE/state/anim.json once, reads the
 * frame's state from SCENE/state/frames.jsonl (its first row) and applies it
 * to the atlas the caller just read. A scene without anim.json is left as is.
 * Returns 1 when it patched. */
int texanim_patch_scene(const char *scene, int map, unsigned char *atlas, int aw, int ah);
/* texanim_patch_scene for an atlas no one else writes after its load: the
 * animation and each sprite's frame last written into it are kept in MEMO,
 * and a patch that would write the same frames writes nothing. Clear
 * memo->atlas when the atlas is loaded again. */
struct texanim_memo {
    const unsigned char *atlas;
    const struct texanim *a;
    int map, n;
    int shown[TEXANIM_MAX_SPRITES];
};
int texanim_patch_scene_memo(const char *scene, int map, unsigned char *atlas, int aw, int ah, struct texanim_memo *memo);
/* texanim_apply with such a MEMO */
void texanim_apply_memo(const struct texanim *a, const struct texanim_state *st, int map, unsigned char *atlas,
                        int aw, int ah, struct texanim_memo *memo);

/* DIR/anim.json and anim.rgba with the state of DIR/frames.jsonl's first row
 * (a recording's state directory); 0 on success. */
int texanim_open(const char *dir, struct texanim *a, struct texanim_state *st);

/* The live client's animation: while set, texanim_patch_scene applies it in
 * place of any scene's recorded frame. NULL clears it. */
void texanim_live_set(const struct texanim *a, const struct texanim_state *st);

/* The recorded scene's sources and its current state (the frame row's, moved
 * on by texanim_compass_render_step): 1 when the scene has them. */
int texanim_scene(const char *scene, const struct texanim **a, struct texanim_state **st);

/* RenderItemFrame's tail for a compass: updateAnimation once more, during the
 * frame (the RENDER role's Math.random off the surface), after drawing the
 * frame's own angle. The hand and the HUD then show the moved state. */
void texanim_compass_render_step(const struct texanim *a, struct texanim_state *st,
                                 const struct texanim_world *w, det_state *det);

#endif
