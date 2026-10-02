/* Entity.writeToNBT for every kind the tape replay ticks, in the order Java
 * sets the keys (nbtbin.c reproduces the compound's HashMap order from that
 * insertion order), plus Rows.capture's digest over them. */
#ifndef NETHERITE_ENTITY_NBT_H
#define NETHERITE_ENTITY_NBT_H

#include <stdint.h>

#include "item_entity.h"
#include "nbtjson.h"
#include "player.h"

/* Entity.writeToNBT's own block into a fresh compound, which the kind-specific
 * writers extend: the order Java sets the keys in, which the binary writer
 * needs. */
nbt *ent_nbt_base(const struct entity *e, int dim, int air, int64_t uuid_msb, int64_t uuid_lsb,
                  float yaw, float pitch);

/* EntityItem / EntityXPOrb / EntityArrow / the throwables and fireballs. */
nbt *ent_nbt_ie(const ie_ent *en);

/* EntityPlayerMP: Entity, EntityLivingBase, EntityPlayer and the player's own
 * fields, with the constant parts read out of the snapshot's tree
 * (server_player.snapshot_tree). */
nbt *ent_nbt_player(const struct server_player *p);

/* The same three through a writer (nbtw.h): into the open compound, which
 * the tree forms above start fresh. ent_w_base takes the PortalCooldown the
 * kind's own write replaces the base's 0 with. */
struct nbtw;
void ent_w_base(struct nbtw *w, const struct entity *e, int dim, int air, int portal_cooldown,
                int64_t uuid_msb, int64_t uuid_lsb, float yaw, float pitch);
void ent_w_ie(struct nbtw *w, const ie_ent *en);
void ent_w_player(struct nbtw *w, const struct server_player *p);

/* Rows.capture's chain: h = h * 1000003 + entityId, then
 * h = h * 1000003 + nbtLong(entity), through nbtbin_long_memo keyed by the
 * entity id. */
uint64_t ent_digest_add(uint64_t h, int entity_id, const nbt *tag);
/* The same for the tag w just wrote into nw_env->nbtbin.memo_scratch
 * (nbtw_bin_long_memo). */
uint64_t ent_digest_add_w(uint64_t h, int entity_id, struct nbtw *w);

/* The player's UUID (the snapshot's player NBT). */
void ent_nbt_player_uuid(const struct server_player *p, int64_t *msb, int64_t *lsb);

#endif