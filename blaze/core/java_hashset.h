/* java_hashset.h - JDK 8 java.util.HashMap for BlockPos keys.
 *
 * HashSet is HashMap.put(key, PRESENT). Iteration is table[0..cap) then
 * Node.next (HashMap.HashIterator / KeySet.forEach). Tree bins keep that
 * next chain; treeify then moveRootToFront may reorder a bin.
 *
 * Citations: openjdk 8u HashMap.java
 *   DEFAULT_INITIAL_CAPACITY 16, DEFAULT_LOAD_FACTOR 0.75f
 *   TREEIFY_THRESHOLD 8, UNTREEIFY_THRESHOLD 6, MIN_TREEIFY_CAPACITY 64
 *   hash: h ^ (h >>> 16)
 *   putVal, resize, treeifyBin, TreeNode.treeify / putTreeVal / split
 *   / moveRootToFront / rotateLeft / rotateRight / balanceInsertion
 *
 * Key is Vec3i/BlockPos (Vec3i.java:47-49):
 *   hashCode = (y + z*31)*31 + x   (int32 wrap)
 *   equals   = x,y,z
 * BlockPos extends Vec3i implements Comparable<Vec3i>, so
 * HashMap.comparableClassFor(BlockPos) is null (not Comparable<BlockPos>).
 * Equal-hash tieBreakOrder uses identityHashCode; compact explosion
 * BlockPos hashes are unique so that path is unused. insert_id is the
 * stand-in if it ever fires.
 *
 * Size-4 TNT/creeper: a 16^3 face-cell set (n=1352) on JDK 8 has table
 * cap 2048, max chain 4, treeBins=0. TREEIFY does not run for a size-4
 * blast. Tree code is still ported so a colliding set matches Java.
 */
#ifndef MC_JAVA_HASHSET_H
#define MC_JAVA_HASHSET_H

#include "mc.h"

#define JHS_NIL (-1)
#define JHS_DEFAULT_CAP 16
#define JHS_TREEIFY_THRESHOLD 8
#define JHS_UNTREEIFY_THRESHOLD 6
#define JHS_MIN_TREEIFY_CAPACITY 64
#define JHS_MAX_NODES 4096
#define JHS_MAX_CAP 8192

typedef struct {
    i32 hash, x, y, z;
    i32 next, prev;
    i32 parent, left, right;
    i32 red;
    i32 tree;
} JhsNode;

typedef struct {
    JhsNode nodes[JHS_MAX_NODES];
    i32 tab[JHS_MAX_CAP];
    i32 oldtab[JHS_MAX_CAP]; /* resize scratch; not on CUDA stack */
    i32 cap, size, threshold, nalloc;
    i32 overflow; /* adds past JHS_MAX_NODES; must stay 0 */
} JavaHashSet;

MC_HD static inline i32 jhs_pos_hash(i32 x, i32 y, i32 z) {
    /* Vec3i.hashCode then HashMap.hash. >>> is unsigned. */
    i32 h = (y + z * 31) * 31 + x;
    return h ^ (i32)((u32)h >> 16);
}

MC_HD static inline int jhs_eq(const JhsNode *n, i32 x, i32 y, i32 z) {
    return n->x == x && n->y == y && n->z == z;
}

MC_HD static inline void jhs_init(JavaHashSet *s) {
    int i;
    s->cap = 0;
    s->size = 0;
    s->threshold = 0;
    s->nalloc = 0;
    s->overflow = 0;
    for (i = 0; i < JHS_MAX_CAP; ++i) s->tab[i] = JHS_NIL;
}

MC_HD static inline i32 jhs_new_node(JavaHashSet *s, i32 hash, i32 x, i32 y,
                                     i32 z, i32 next) {
    JhsNode *n;
    i32 i;
    if (s->nalloc < 0 || s->nalloc >= JHS_MAX_NODES) {
        s->overflow++;
        return JHS_NIL;
    }
    i = s->nalloc;
    s->nalloc = i + 1;
    n = &s->nodes[i];
    n->hash = hash;
    n->x = x;
    n->y = y;
    n->z = z;
    n->next = next;
    n->prev = n->parent = n->left = n->right = JHS_NIL;
    n->red = 0;
    n->tree = 0;
    return i;
}

MC_HD static inline i32 jhs_rot_left(JavaHashSet *s, i32 root, i32 p) {
    i32 r, pp, rl;
    if (p == JHS_NIL || (r = s->nodes[p].right) == JHS_NIL) return root;
    rl = s->nodes[p].right = s->nodes[r].left;
    if (rl != JHS_NIL) s->nodes[rl].parent = p;
    pp = s->nodes[r].parent = s->nodes[p].parent;
    if (pp == JHS_NIL) {
        root = r;
        s->nodes[r].red = 0;
    } else if (s->nodes[pp].left == p)
        s->nodes[pp].left = r;
    else
        s->nodes[pp].right = r;
    s->nodes[r].left = p;
    s->nodes[p].parent = r;
    return root;
}

MC_HD static inline i32 jhs_rot_right(JavaHashSet *s, i32 root, i32 p) {
    i32 l, pp, lr;
    if (p == JHS_NIL || (l = s->nodes[p].left) == JHS_NIL) return root;
    lr = s->nodes[p].left = s->nodes[l].right;
    if (lr != JHS_NIL) s->nodes[lr].parent = p;
    pp = s->nodes[l].parent = s->nodes[p].parent;
    if (pp == JHS_NIL) {
        root = l;
        s->nodes[l].red = 0;
    } else if (s->nodes[pp].right == p)
        s->nodes[pp].right = l;
    else
        s->nodes[pp].left = l;
    s->nodes[l].right = p;
    s->nodes[p].parent = l;
    return root;
}

MC_HD static inline i32 jhs_balance_ins(JavaHashSet *s, i32 root, i32 x) {
    s->nodes[x].red = 1;
    for (;;) {
        i32 xp, xpp, xppl, xppr;
        xp = s->nodes[x].parent;
        if (xp == JHS_NIL) {
            s->nodes[x].red = 0;
            return x;
        }
        if (!s->nodes[xp].red || (xpp = s->nodes[xp].parent) == JHS_NIL)
            return root;
        xppl = s->nodes[xpp].left;
        if (xp == xppl) {
            xppr = s->nodes[xpp].right;
            if (xppr != JHS_NIL && s->nodes[xppr].red) {
                s->nodes[xppr].red = 0;
                s->nodes[xp].red = 0;
                s->nodes[xpp].red = 1;
                x = xpp;
            } else {
                if (x == s->nodes[xp].right) {
                    x = xp;
                    root = jhs_rot_left(s, root, x);
                    xp = s->nodes[x].parent;
                    xpp = (xp == JHS_NIL) ? JHS_NIL : s->nodes[xp].parent;
                }
                if (xp != JHS_NIL) {
                    s->nodes[xp].red = 0;
                    if (xpp != JHS_NIL) {
                        s->nodes[xpp].red = 1;
                        root = jhs_rot_right(s, root, xpp);
                    }
                }
            }
        } else {
            if (xppl != JHS_NIL && s->nodes[xppl].red) {
                s->nodes[xppl].red = 0;
                s->nodes[xp].red = 0;
                s->nodes[xpp].red = 1;
                x = xpp;
            } else {
                if (x == s->nodes[xp].left) {
                    x = xp;
                    root = jhs_rot_right(s, root, x);
                    xp = s->nodes[x].parent;
                    xpp = (xp == JHS_NIL) ? JHS_NIL : s->nodes[xp].parent;
                }
                if (xp != JHS_NIL) {
                    s->nodes[xp].red = 0;
                    if (xpp != JHS_NIL) {
                        s->nodes[xpp].red = 1;
                        root = jhs_rot_left(s, root, xpp);
                    }
                }
            }
        }
    }
}

MC_HD static inline void jhs_move_root_front(JavaHashSet *s, i32 root) {
    i32 n, index, first, rn, rp;
    if (root == JHS_NIL || s->cap <= 0) return;
    n = s->cap;
    index = (n - 1) & s->nodes[root].hash;
    first = s->tab[index];
    if (root == first) return;
    rn = s->nodes[root].next;
    rp = s->nodes[root].prev;
    s->tab[index] = root;
    if (rn != JHS_NIL) s->nodes[rn].prev = rp;
    if (rp != JHS_NIL) s->nodes[rp].next = rn;
    if (first != JHS_NIL) s->nodes[first].prev = root;
    s->nodes[root].next = first;
    s->nodes[root].prev = JHS_NIL;
}

MC_HD static inline i32 jhs_tie(const JavaHashSet *s, i32 a, i32 b) {
    /* HashMap.tieBreakOrder identityHashCode stand-in: earlier alloc is
     * smaller. Only used when hashCodes collide. */
    (void)s;
    return (a <= b) ? -1 : 1;
}

MC_HD static inline void jhs_treeify(JavaHashSet *s, i32 hd, i32 index) {
    i32 root = JHS_NIL, x, next;
    (void)index;
    for (x = hd; x != JHS_NIL; x = next) {
        next = s->nodes[x].next;
        s->nodes[x].left = s->nodes[x].right = JHS_NIL;
        if (root == JHS_NIL) {
            s->nodes[x].parent = JHS_NIL;
            s->nodes[x].red = 0;
            root = x;
        } else {
            i32 h = s->nodes[x].hash, p = root;
            for (;;) {
                i32 dir, ph = s->nodes[p].hash, xp;
                if (ph > h)
                    dir = -1;
                else if (ph < h)
                    dir = 1;
                else
                    dir = jhs_tie(s, x, p);
                xp = p;
                p = (dir <= 0) ? s->nodes[p].left : s->nodes[p].right;
                if (p == JHS_NIL) {
                    s->nodes[x].parent = xp;
                    if (dir <= 0)
                        s->nodes[xp].left = x;
                    else
                        s->nodes[xp].right = x;
                    root = jhs_balance_ins(s, root, x);
                    break;
                }
            }
        }
    }
    jhs_move_root_front(s, root);
}

MC_HD static inline void jhs_untreeify(JavaHashSet *s, i32 hd) {
    i32 q;
    for (q = hd; q != JHS_NIL; q = s->nodes[q].next) {
        s->nodes[q].tree = 0;
        s->nodes[q].prev = s->nodes[q].parent = s->nodes[q].left =
            s->nodes[q].right = JHS_NIL;
        s->nodes[q].red = 0;
    }
}

MC_HD static inline i32 jhs_tree_find(const JavaHashSet *s, i32 p, i32 h,
                                      i32 x, i32 y, i32 z) {
    i32 stk[64];
    int sp = 0;
    while (p != JHS_NIL || sp) {
        const JhsNode *n;
        if (p == JHS_NIL) {
            p = stk[--sp];
            continue;
        }
        n = &s->nodes[p];
        if (n->hash > h)
            p = n->left;
        else if (n->hash < h)
            p = n->right;
        else if (jhs_eq(n, x, y, z))
            return p;
        else if (n->left == JHS_NIL)
            p = n->right;
        else if (n->right == JHS_NIL)
            p = n->left;
        else {
            if (sp < 63) stk[sp++] = n->left;
            p = n->right;
        }
    }
    return JHS_NIL;
}

MC_HD static inline i32 jhs_put_tree(JavaHashSet *s, i32 first, i32 h, i32 x,
                                     i32 y, i32 z) {
    i32 root, p, searched = 0;
    root = first;
    if (s->nodes[root].parent != JHS_NIL) {
        i32 r = root;
        while (s->nodes[r].parent != JHS_NIL) r = s->nodes[r].parent;
        root = r;
    }
    p = root;
    for (;;) {
        i32 dir, ph = s->nodes[p].hash, xp, xi, xpn;
        if (ph > h)
            dir = -1;
        else if (ph < h)
            dir = 1;
        else if (jhs_eq(&s->nodes[p], x, y, z))
            return p;
        else {
            i32 q;
            if (!searched) {
                searched = 1;
                if (s->nodes[p].left != JHS_NIL &&
                    (q = jhs_tree_find(s, s->nodes[p].left, h, x, y, z)) !=
                        JHS_NIL)
                    return q;
                if (s->nodes[p].right != JHS_NIL &&
                    (q = jhs_tree_find(s, s->nodes[p].right, h, x, y, z)) !=
                        JHS_NIL)
                    return q;
            }
            dir = jhs_tie(s, s->nalloc, p);
        }
        xp = p;
        p = (dir <= 0) ? s->nodes[p].left : s->nodes[p].right;
        if (p == JHS_NIL) {
            xpn = s->nodes[xp].next;
            xi = jhs_new_node(s, h, x, y, z, xpn);
            if (xi == JHS_NIL) return JHS_NIL;
            s->nodes[xi].tree = 1;
            if (dir <= 0)
                s->nodes[xp].left = xi;
            else
                s->nodes[xp].right = xi;
            s->nodes[xp].next = xi;
            s->nodes[xi].parent = s->nodes[xi].prev = xp;
            if (xpn != JHS_NIL) s->nodes[xpn].prev = xi;
            jhs_move_root_front(s, jhs_balance_ins(s, root, xi));
            return JHS_NIL;
        }
    }
}

MC_HD static inline void jhs_split(JavaHashSet *s, i32 b, i32 *newtab, i32 index,
                                   i32 bit) {
    i32 loHead = JHS_NIL, loTail = JHS_NIL;
    i32 hiHead = JHS_NIL, hiTail = JHS_NIL;
    i32 lc = 0, hc = 0, e, next;
    for (e = b; e != JHS_NIL; e = next) {
        next = s->nodes[e].next;
        s->nodes[e].next = JHS_NIL;
        if ((s->nodes[e].hash & bit) == 0) {
            s->nodes[e].prev = loTail;
            if (loTail == JHS_NIL)
                loHead = e;
            else
                s->nodes[loTail].next = e;
            loTail = e;
            ++lc;
        } else {
            s->nodes[e].prev = hiTail;
            if (hiTail == JHS_NIL)
                hiHead = e;
            else
                s->nodes[hiTail].next = e;
            hiTail = e;
            ++hc;
        }
    }
    if (loHead != JHS_NIL) {
        if (lc <= JHS_UNTREEIFY_THRESHOLD) {
            jhs_untreeify(s, loHead);
            newtab[index] = loHead;
        } else {
            newtab[index] = loHead;
            if (hiHead != JHS_NIL) jhs_treeify(s, loHead, index);
        }
    }
    if (hiHead != JHS_NIL) {
        if (hc <= JHS_UNTREEIFY_THRESHOLD) {
            jhs_untreeify(s, hiHead);
            newtab[index + bit] = hiHead;
        } else {
            newtab[index + bit] = hiHead;
            if (loHead != JHS_NIL) jhs_treeify(s, hiHead, index + bit);
        }
    }
}

MC_HD static inline void jhs_resize(JavaHashSet *s) {
    i32 oldCap = s->cap, oldThr = s->threshold;
    i32 newCap, newThr = 0, j;
    if (oldCap > 0) {
        newCap = oldCap << 1;
        if (newCap > JHS_MAX_CAP) newCap = JHS_MAX_CAP;
        if (oldCap >= JHS_DEFAULT_CAP) newThr = oldThr << 1;
    } else {
        newCap = JHS_DEFAULT_CAP;
        newThr = (i32)(0.75f * (float)JHS_DEFAULT_CAP);
    }
    if (newThr == 0) {
        float ft = (float)newCap * 0.75f;
        newThr = (i32)ft;
    }
    if (newCap > JHS_MAX_CAP) newCap = JHS_MAX_CAP;
    s->threshold = newThr;
    for (j = 0; j < oldCap; ++j) s->oldtab[j] = s->tab[j];
    for (j = 0; j < JHS_MAX_CAP; ++j) s->tab[j] = JHS_NIL;
    s->cap = newCap;
    if (oldCap == 0) return;
    for (j = 0; j < oldCap; ++j) {
        i32 e = s->oldtab[j];
        if (e == JHS_NIL) continue;
        if (s->nodes[e].next == JHS_NIL)
            s->tab[s->nodes[e].hash & (newCap - 1)] = e;
        else if (s->nodes[e].tree)
            jhs_split(s, e, s->tab, j, oldCap);
        else {
            i32 loHead = JHS_NIL, loTail = JHS_NIL;
            i32 hiHead = JHS_NIL, hiTail = JHS_NIL, next;
            do {
                next = s->nodes[e].next;
                if ((s->nodes[e].hash & oldCap) == 0) {
                    if (loTail == JHS_NIL)
                        loHead = e;
                    else
                        s->nodes[loTail].next = e;
                    loTail = e;
                } else {
                    if (hiTail == JHS_NIL)
                        hiHead = e;
                    else
                        s->nodes[hiTail].next = e;
                    hiTail = e;
                }
                e = next;
            } while (e != JHS_NIL);
            if (loTail != JHS_NIL) {
                s->nodes[loTail].next = JHS_NIL;
                s->tab[j] = loHead;
            }
            if (hiTail != JHS_NIL) {
                s->nodes[hiTail].next = JHS_NIL;
                s->tab[j + oldCap] = hiHead;
            }
        }
    }
}

MC_HD static inline void jhs_treeify_bin(JavaHashSet *s, i32 hash) {
    i32 n = s->cap, index, e, hd = JHS_NIL, tl = JHS_NIL;
    if (n < JHS_MIN_TREEIFY_CAPACITY) {
        jhs_resize(s);
        return;
    }
    index = (n - 1) & hash;
    e = s->tab[index];
    if (e == JHS_NIL) return;
    do {
        s->nodes[e].tree = 1;
        if (tl == JHS_NIL)
            hd = e;
        else {
            s->nodes[e].prev = tl;
            s->nodes[tl].next = e;
        }
        tl = e;
        e = s->nodes[e].next;
    } while (e != JHS_NIL);
    s->tab[index] = hd;
    if (hd != JHS_NIL) jhs_treeify(s, hd, index);
}

/* HashSet.add. Returns 1 if inserted, 0 if already present. */
MC_HD static inline int jhs_add(JavaHashSet *s, i32 x, i32 y, i32 z) {
    i32 hash, n, i, p, e, binCount;
    hash = jhs_pos_hash(x, y, z);
    if (s->cap == 0) jhs_resize(s);
    n = s->cap;
    i = (n - 1) & hash;
    p = s->tab[i];
    if (p == JHS_NIL) {
        e = jhs_new_node(s, hash, x, y, z, JHS_NIL);
        if (e == JHS_NIL) return 0;
        s->tab[i] = e;
    } else {
        if (s->nodes[p].hash == hash && jhs_eq(&s->nodes[p], x, y, z))
            return 0;
        if (s->nodes[p].tree) {
            i32 ov = s->overflow;
            if (jhs_put_tree(s, p, hash, x, y, z) != JHS_NIL) return 0;
            if (s->overflow != ov) return 0;
        } else {
            for (binCount = 0;; ++binCount) {
                e = s->nodes[p].next;
                if (e == JHS_NIL) {
                    i32 nn = jhs_new_node(s, hash, x, y, z, JHS_NIL);
                    if (nn == JHS_NIL) return 0;
                    s->nodes[p].next = nn;
                    if (binCount >= JHS_TREEIFY_THRESHOLD - 1)
                        jhs_treeify_bin(s, hash);
                    break;
                }
                if (s->nodes[e].hash == hash && jhs_eq(&s->nodes[e], x, y, z))
                    return 0;
                p = e;
            }
        }
    }
    if (++s->size > s->threshold) jhs_resize(s);
    return 1;
}

typedef struct {
    i32 tab_i;
    i32 node;
} JhsIter;

MC_HD static inline void jhs_iter_init(const JavaHashSet *s, JhsIter *it) {
    it->tab_i = 0;
    it->node = JHS_NIL;
    if (s->cap <= 0 || s->size <= 0) return;
    while (it->tab_i < s->cap && s->tab[it->tab_i] == JHS_NIL) ++it->tab_i;
    if (it->tab_i < s->cap) it->node = s->tab[it->tab_i++];
}

MC_HD static inline int jhs_iter_next(const JavaHashSet *s, JhsIter *it, i32 *x,
                                      i32 *y, i32 *z) {
    const JhsNode *n;
    if (it->node == JHS_NIL) return 0;
    n = &s->nodes[it->node];
    *x = n->x;
    *y = n->y;
    *z = n->z;
    if (n->next != JHS_NIL)
        it->node = n->next;
    else {
        it->node = JHS_NIL;
        while (it->tab_i < s->cap && s->tab[it->tab_i] == JHS_NIL) ++it->tab_i;
        if (it->tab_i < s->cap) it->node = s->tab[it->tab_i++];
    }
    return 1;
}

#endif /* MC_JAVA_HASHSET_H */
