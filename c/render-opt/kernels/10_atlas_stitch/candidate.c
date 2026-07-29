/* CANDIDATE: C port of MC's texture-atlas Stitcher bin-packer. Must match golden/Golden.java.
 * Faithful translation of Stitcher.allocateSlot / expandAndAllocateSlot / Slot.addSlot / Holder,
 * with mipmapLevel = 0 and maxTileDimension = 0 (so getMipmapDimension is the identity and
 * scaleFactor stays 1.0 - same parameterization as the golden). Control flow is copied line-for-line
 * including the deliberately-transposed-looking Math.max pairings, the Forge XOR expansion block, and
 * subslot creation order; those drive placement and the final rotated flag.
 *
 * Input: line 1 "maxWidth maxHeight count"; then count lines "width height name".
 * Output: per placed sprite "name originX originY width height rotated" (sorted by line, strcmp). */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NAME_MAX 32

/* ---- MathHelper.smallestEncompassingPowerOfTwo (verbatim; uint wrap like kernel 03) ---- */
static int32_t smallestEncompassingPowerOfTwo(int32_t value) {
    int32_t i = (int32_t)((uint32_t)value - 1u);
    i = i | i >> 1;
    i = i | i >> 2;
    i = i | i >> 4;
    i = i | i >> 8;
    i = i | i >> 16;
    return (int32_t)((uint32_t)i + 1u);
}

/* getMipmapDimension: Java precedence -> ((p >> level) + cond) << level */
static int32_t getMipmapDimension(int32_t p, int32_t level) {
    int32_t cond = ((p & ((1 << level) - 1)) == 0) ? 0 : 1;
    return ((p >> level) + cond) << level;
}

/* ---- Holder ---- */
typedef struct {
    int iconWidth, iconHeight;
    int width, height;
    char name[NAME_MAX];
    int mipmapLevelHolder;
    int rotated;       /* bool */
    float scaleFactor;
} Holder;

static Holder *holder_new(int w, int h, const char *name, int mip) {
    Holder *p = (Holder *)calloc(1, sizeof(Holder));
    p->iconWidth = w; p->iconHeight = h;
    p->width = w; p->height = h;
    snprintf(p->name, NAME_MAX, "%s", name);
    p->mipmapLevelHolder = mip;
    p->scaleFactor = 1.0f;
    p->rotated = getMipmapDimension(p->height, mip) > getMipmapDimension(p->width, mip);
    return p;
}

static int holder_getWidth(const Holder *h) {
    int i = h->rotated ? h->height : h->width;
    return getMipmapDimension((int)((float)i * h->scaleFactor), h->mipmapLevelHolder);
}
static int holder_getHeight(const Holder *h) {
    int i = h->rotated ? h->width : h->height;
    return getMipmapDimension((int)((float)i * h->scaleFactor), h->mipmapLevelHolder);
}
static void holder_rotate(Holder *h) { h->rotated = !h->rotated; }

static int holder_compareTo(const Holder *a, const Holder *b) {
    if (holder_getHeight(a) == holder_getHeight(b)) {
        if (holder_getWidth(a) == holder_getWidth(b)) {
            return strcmp(a->name, b->name);   /* names non-null + ASCII: matches String.compareTo sign */
        }
        return holder_getWidth(a) < holder_getWidth(b) ? 1 : -1;
    }
    return holder_getHeight(a) < holder_getHeight(b) ? 1 : -1;
}

static int holder_cmp_qsort(const void *pa, const void *pb) {
    const Holder *a = *(const Holder *const *)pa;
    const Holder *b = *(const Holder *const *)pb;
    return holder_compareTo(a, b);
}

/* ---- Slot ---- */
typedef struct Slot {
    int originX, originY, width, height;
    struct Slot **subSlots;
    int nSub, capSub;
    Holder *holder;
} Slot;

static Slot *slot_new(int x, int y, int w, int h) {
    Slot *s = (Slot *)calloc(1, sizeof(Slot));
    s->originX = x; s->originY = y; s->width = w; s->height = h;
    return s;
}
static void slot_subadd(Slot *s, Slot *child) {
    if (s->nSub == s->capSub) {
        s->capSub = s->capSub ? s->capSub * 2 : 4;
        s->subSlots = (Slot **)realloc(s->subSlots, s->capSub * sizeof(Slot *));
    }
    s->subSlots[s->nSub++] = child;
}

static int imax(int a, int b) { return a > b ? a : b; }

static int slot_addSlot(Slot *self, Holder *holderIn) {
    if (self->holder != NULL) {
        return 0;
    } else {
        int i = holder_getWidth(holderIn);
        int j = holder_getHeight(holderIn);

        if (i <= self->width && j <= self->height) {
            if (i == self->width && j == self->height) {
                self->holder = holderIn;
                return 1;
            } else {
                if (self->subSlots == NULL) {
                    slot_subadd(self, slot_new(self->originX, self->originY, i, j));
                    int k = self->width - i;
                    int l = self->height - j;

                    if (l > 0 && k > 0) {
                        int i1 = imax(self->height, k);
                        int j1 = imax(self->width, l);

                        if (i1 >= j1) {
                            slot_subadd(self, slot_new(self->originX, self->originY + j, i, l));
                            slot_subadd(self, slot_new(self->originX + i, self->originY, k, self->height));
                        } else {
                            slot_subadd(self, slot_new(self->originX + i, self->originY, k, j));
                            slot_subadd(self, slot_new(self->originX, self->originY + j, self->width, l));
                        }
                    } else if (k == 0) {
                        slot_subadd(self, slot_new(self->originX, self->originY + j, i, l));
                    } else if (l == 0) {
                        slot_subadd(self, slot_new(self->originX + i, self->originY, k, j));
                    }
                }

                for (int s = 0; s < self->nSub; ++s) {
                    if (slot_addSlot(self->subSlots[s], holderIn)) {
                        return 1;
                    }
                }

                return 0;
            }
        } else {
            return 0;
        }
    }
}

/* collect placed slots (those with a holder), preorder, matching getAllStitchSlots */
typedef struct { Slot **a; int n, cap; } SlotList;
static void slotlist_add(SlotList *L, Slot *s) {
    if (L->n == L->cap) { L->cap = L->cap ? L->cap * 2 : 64; L->a = (Slot **)realloc(L->a, L->cap * sizeof(Slot *)); }
    L->a[L->n++] = s;
}
static void slot_getAllStitchSlots(Slot *self, SlotList *out) {
    if (self->holder != NULL) {
        slotlist_add(out, self);
    } else if (self->subSlots != NULL) {
        for (int s = 0; s < self->nSub; ++s) {
            slot_getAllStitchSlots(self->subSlots[s], out);
        }
    }
}

/* ---- Stitcher ---- */
typedef struct {
    int mipmapLevelStitcher;
    Holder **holders; int nHolders, capHolders;   /* setStitchHolders */
    Slot **stitchSlots; int nSlots, capSlots;
    int currentWidth, currentHeight;
    int maxWidth, maxHeight;
    int maxTileDimension;
} Stitcher;

static void stitcher_addSprite(Stitcher *st, int w, int h, const char *name) {
    Holder *hd = holder_new(w, h, name, st->mipmapLevelStitcher);
    /* maxTileDimension == 0 -> no setNewDimension */
    if (st->nHolders == st->capHolders) {
        st->capHolders = st->capHolders ? st->capHolders * 2 : 256;
        st->holders = (Holder **)realloc(st->holders, st->capHolders * sizeof(Holder *));
    }
    st->holders[st->nHolders++] = hd;
}

static void stitcher_slotadd(Stitcher *st, Slot *s) {
    if (st->nSlots == st->capSlots) {
        st->capSlots = st->capSlots ? st->capSlots * 2 : 256;
        st->stitchSlots = (Slot **)realloc(st->stitchSlots, st->capSlots * sizeof(Slot *));
    }
    st->stitchSlots[st->nSlots++] = s;
}

static int expandAndAllocateSlot(Stitcher *st, Holder *p) {
    int imin = holder_getWidth(p) < holder_getHeight(p) ? holder_getWidth(p) : holder_getHeight(p); /* Math.min */
    /* int j = Math.max(getWidth, getHeight) in source -- unused thereafter, omitted */
    int k = smallestEncompassingPowerOfTwo(st->currentWidth);
    int l = smallestEncompassingPowerOfTwo(st->currentHeight);
    int i1 = smallestEncompassingPowerOfTwo(st->currentWidth + imin);
    int j1 = smallestEncompassingPowerOfTwo(st->currentHeight + imin);
    int flag1 = i1 <= st->maxWidth;
    int flag2 = j1 <= st->maxHeight;

    if (!flag1 && !flag2) {
        return 0;
    } else {
        int flag3 = flag1 && k != i1;
        int flag4 = flag2 && l != j1;
        int flag;

        if (flag3 ^ flag4) {
            flag = !flag3 && flag1;
        } else {
            flag = flag1 && k <= l;
        }

        Slot *slot;

        if (flag) {
            if (holder_getWidth(p) > holder_getHeight(p)) {
                holder_rotate(p);
            }
            if (st->currentHeight == 0) {
                st->currentHeight = holder_getHeight(p);
            }
            slot = slot_new(st->currentWidth, 0, holder_getWidth(p), st->currentHeight);
            st->currentWidth += holder_getWidth(p);
        } else {
            slot = slot_new(0, st->currentHeight, st->currentWidth, holder_getHeight(p));
            st->currentHeight += holder_getHeight(p);
        }

        slot_addSlot(slot, p);
        stitcher_slotadd(st, slot);
        return 1;
    }
}

static int allocateSlot(Stitcher *st, Holder *p) {
    int flag = p->iconWidth != p->iconHeight;

    for (int n = 0; n < st->nSlots; ++n) {
        if (slot_addSlot(st->stitchSlots[n], p)) {
            return 1;
        }
        if (flag) {
            holder_rotate(p);
            if (slot_addSlot(st->stitchSlots[n], p)) {
                return 1;
            }
            holder_rotate(p);
        }
    }

    return expandAndAllocateSlot(st, p);
}

static void doStitch(Stitcher *st) {
    qsort(st->holders, st->nHolders, sizeof(Holder *), holder_cmp_qsort);
    for (int n = 0; n < st->nHolders; ++n) {
        if (!allocateSlot(st, st->holders[n])) {
            fprintf(stderr, "Unable to fit: %s\n", st->holders[n]->name);
            exit(2);
        }
    }
    st->currentWidth = smallestEncompassingPowerOfTwo(st->currentWidth);
    st->currentHeight = smallestEncompassingPowerOfTwo(st->currentHeight);
}

static int line_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(void) {
    int maxW, maxH, count;
    if (scanf("%d %d %d", &maxW, &maxH, &count) != 3) return 1;

    Stitcher st;
    memset(&st, 0, sizeof st);
    st.mipmapLevelStitcher = 0;
    st.maxWidth = maxW;
    st.maxHeight = maxH;
    st.maxTileDimension = 0;

    for (int n = 0; n < count; n++) {
        int w, h;
        char name[NAME_MAX];
        if (scanf("%d %d %31s", &w, &h, name) != 3) return 1;
        stitcher_addSprite(&st, w, h, name);
    }

    doStitch(&st);

    SlotList placed; placed.a = NULL; placed.n = placed.cap = 0;
    for (int n = 0; n < st.nSlots; ++n) {
        slot_getAllStitchSlots(st.stitchSlots[n], &placed);
    }

    char **lines = (char **)malloc(placed.n * sizeof(char *));
    for (int n = 0; n < placed.n; ++n) {
        Slot *s = placed.a[n];
        Holder *h = s->holder;
        char *buf = (char *)malloc(128);
        snprintf(buf, 128, "%s %d %d %d %d %d", h->name, s->originX, s->originY,
                 holder_getWidth(h), holder_getHeight(h), h->rotated ? 1 : 0);
        lines[n] = buf;
    }
    qsort(lines, placed.n, sizeof(char *), line_cmp);
    for (int n = 0; n < placed.n; ++n) {
        printf("%s\n", lines[n]);
    }
    return 0;
}
