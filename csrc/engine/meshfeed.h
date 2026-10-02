/* The device mesher's feed (cuda/meshing): a live renderer (raster.c) that
 * leaves its sections' meshing to the device sends, with each frame, the
 * section passes to mesh and exactly the world bytes those need that the
 * device does not hold yet. This is the host's model of what one device
 * slot holds (its copies of chunk records, by chunk and band slot), kept
 * current by the renderer's own stale marks (raster_live_stale: a record's
 * band changes only where a stale mark names its section) and by comparing
 * each needed chunk's height map, section mask and biomes with what was
 * sent. A request reads the 3x3 chunks around its section: every block a
 * section's mesh reads is at most two blocks past its edges.
 *
 * What a frame sends (struct meshfeed_out) is plain data in slot indices:
 * the device side (csrc/cuda/render, or cuda/meshing's gate) keeps each
 * slot's memory and turns indices into addresses. An epoch names the
 * device state a feed builds on: a new epoch (meshfeed_reset) starts from
 * nothing, and a feed whose (epoch, seq) does not follow the device's last
 * cannot be applied. No CUDA here: play writes feeds into its frame dumps
 * (--obs-dump) on any host. */
#ifndef NETHERITE_MESHFEED_H
#define NETHERITE_MESHFEED_H

#include <stddef.h>
#include <stdint.h>

#include "render_blocks.h"

struct rb_world;

/* one section pass to mesh: chunk[i] is the chunk slot of (cx - 1 + i / 3,
 * cz - 1 + i % 3), MF_NULL for a record the window holds empty (a record
 * of zeros), MF_OUTSIDE for a chunk outside the window (reads as nothing);
 * te and trig index the feed's lists */
enum { MF_NULL = -1, MF_OUTSIDE = -2 };
struct meshfeed_req {
    int32_t cx, s, cz, pass;
    int32_t px, py, pz, no_sky;
    int32_t chunk[9];
    uint32_t key, version;
    int32_t te_first, te_n, trig_first, trig_n;
    /* the section has no band (every cell air): its mesh is empty, nothing
     * to run */
    int32_t empty;
};

/* an upload: a chunk record (kind 0: sizeof(struct chunk) bytes at off, its
 * band offsets named by band slot in band[], -1 none) or a band (kind 1:
 * sizeof(struct chunk_sec_flat) bytes, the band with its nibble arrays
 * inline, then 2048 bytes of high id nibbles when hi) into slot. The device's band then changes by runs (kind 2: band[1]
 * bytes at off to the band image's byte band[0], a multiple of
 * MF_GRANULE; one from byte 0 carries the band's header, hi as in kind 1):
 * a device's band slots read zero at the start of an epoch (cuda/meshing zeroes
 * them), and the feed sends only the granules that differ from what the
 * slot holds. A band goes whole (kind 1) only when a feed nobody took is
 * followed by another that sends the same slot again: the ops the earlier
 * frame sent for it become kind 3 (nothing to do: two copies into one slot
 * in one launch would race). In a band image the header's ids_hi reads 1
 * when hi, its nib offsets name the inline arrays, the rest 0. */
struct meshfeed_op {
    int32_t kind, slot, hi, pad;
    uint64_t off;
    int32_t band[16];
};
enum { MF_CHUNK = 0, MF_BAND = 1, MF_BAND_RUN = 2, MF_SKIP = 3,
       MF_BAND_BYTES = 12320 /* sizeof(struct chunk_sec_flat) + 2048, checked in meshfeed.c */, MF_GRANULE = 64 };

struct meshfeed_out {
    uint32_t epoch, seq;
    int chunk_slots, band_slots;          /* the slots in use can reach these (the device's pools) */
    int nreq, nte, ntrig, nop;
    const struct meshfeed_req *req;
    const struct rb_te *te;
    const struct rb_trig *trig;           /* each request's run sorted (cuda/meshing/kernels.h meshing_trig_cmp) */
    const struct meshfeed_op *op;
    const unsigned char *payload;
    size_t npayload;
    /* the device's answers, when the frame's producer reads them (NULL
     * otherwise): each request's vertex count, and a flag the device sets
     * when it could not apply the feed (the producer then re-meshes
     * everything in a new epoch) */
    int32_t *count_back;
    int *resync;
    /* the square the producer keeps meshes in: a section outside it is
     * meshed again (a new request) before it is drawn again */
    int32_t cx0, cz0, cx1, cz1;
};

struct meshfeed;
struct meshkey_cache;
struct meshfeed *meshfeed_new(void);
/* The bands' content serials (raster_meshkey.h meshkey_band_id): a stale
 * band whose serial is the one its slot was last sent holds the bytes sent,
 * and is not compared again (under the cache's verify, it is, and counted
 * there). NULL, the default: every stale band is compared. */
void meshfeed_band_ids(struct meshfeed *f, struct meshkey_cache *mc);
void meshfeed_free(struct meshfeed *f);
/* a new epoch: the device holds nothing */
void meshfeed_reset(struct meshfeed *f);
/* a stale mark: section s of chunk (cx, cz) may have changed (every one when all) */
void meshfeed_dirty(struct meshfeed *f, int cx, int s, int cz);
void meshfeed_dirty_all(struct meshfeed *f);
/* a frame: its requests over the window W (the records as they are now),
 * then the feed (valid until the next meshfeed_begin). A feed nobody took
 * (meshfeed_take: a frame drawn but not sent, as play draws between its
 * dumped shots) carries on into the next frame's: its requests and uploads
 * stay first, and meshfeed_keep drops the requests a later one superseded
 * (KEEP[i] 0), so a feed that goes out holds everything since the last. */
void meshfeed_begin(struct meshfeed *f);
void meshfeed_request(struct meshfeed *f, const struct rb_world *w, int cx, int s, int cz, int pass,
                      int px, int py, int pz, uint32_t key, uint32_t version);
const struct meshfeed_req *meshfeed_requests(const struct meshfeed *f, int *n);
void meshfeed_keep(struct meshfeed *f, const unsigned char *keep);
const struct meshfeed_out *meshfeed_end(struct meshfeed *f);
void meshfeed_take(struct meshfeed *f);

/* The libm values no request lists (render_blocks.h rb_trig): the liquids'
 * flow angles and the brewing stands' arms, sorted; *n entries, the
 * caller frees. */
struct rb_trig *meshfeed_static_trig(int *n);

#endif
