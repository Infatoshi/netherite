/* game/world_live.h - LIVE streaming view-distance world (owner: WORLD-LIVE agent).
 *
 * Thin public header: the full prototype contract for GmWorld / GmMeshView lives in
 * game/game.h (the seam contract). This header just re-exposes it so a translation
 * unit can depend on world_live directly without pulling the whole game seam.
 *
 * Implementation (game/world_live.c) wraps a CrWorldMC (world/mesh_mc.c): it caches
 * the per-CrRenderLayer vertex buffers of each meshed chunk with a dirty flag, streams
 * + frustum-culls a Chebyshev view radius (SCN_VIEW_RADIUS) around the camera exactly
 * as verify/chunk_scene.h does (so a fresh world at the frozen pose meshes
 * byte-identically), and re-meshes only dirty / newly-visible chunks. Block ids are the
 * blaze CB_*/PB_* small ints read through the CrLight store.
 */
#ifndef MAGMA_GAME_WORLD_LIVE_H
#define MAGMA_GAME_WORLD_LIVE_H

#include "core/types.h"
#include "game/game.h"

#endif /* MAGMA_GAME_WORLD_LIVE_H */
