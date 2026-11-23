/* The light engine's update (World.updateLightByType, dev.cuh), a
 * unit of its own (-rdc): the window loop and the writes it makes. */
#include "dev.cuh"

/* a nibble store with the band rules of world.h's setters: storage made for
 * a value that is not 0, a shared band made the chunk's own unless it
 * already holds the value */
static __device__ void store_nib(uint8_t *c, int field, int lx, int y, int lz, int v)
{
    int s = y >> 4, i = cell_in_sec(lx, y, lz);
    uint8_t *sec = sec_at(c, s);

    if (sec == NULL)
    {
        if (v == 0) return;
        ((volatile int64_t *)bands(c))[s] = home(s);
        sec = c + home(s);
    }
    else if (*(volatile uint32_t *)(sec + dL.sec_shared) != 0)
    {
        if (nib(sec + field, i) == v) return;
        *(volatile uint32_t *)(sec + dL.sec_shared) = 0;
    }
    nib_set(sec + field, i, v);
}

/* chunk_touch: the chunk's stamp, write sequence and edges. C sets them to
 * the write's epoch; the writes of a record have rising epochs, so the value
 * after the record is the largest of them, which atomicMax gives once the
 * value from before the record is cleared (update does that on the record's
 * first write of each; a chunk's stamps may exceed the world's epoch, as the
 * client world's copies of the server's chunks do). */
static __device__ void touch(cl_call &S, uint8_t *c, int lx, int y, int lz, uint64_t k)
{
    unsigned long long e = S.epoch + k, ws = S.wseq + k;

    atomicMax((unsigned long long *)(c + dL.stamp), e);
    atomicMax((unsigned long long *)(c + dL.wseq), ws);

    unsigned long long *edge = (unsigned long long *)(c + dL.edge_stamp);

    if (y < 0 || !present(c, y))
        for (int i = 0; i < 4; ++i) atomicMax(edge + i, e);
    else
    {
        if (lx == 0) atomicMax(edge + 0, e);
        if (lx == 15) atomicMax(edge + 1, e);
        if (lz == 0) atomicMax(edge + 2, e);
        if (lz == 15) atomicMax(edge + 3, e);
    }
}

/* Chunk.generateSkylightMap (world.c generate_skylight_map), a column per
 * thread; every thread of the block calls it */
static __device__ void gen_skylight_map(uint8_t *c)
{
    unsigned m = cmask(c);
    int top = 0;

    for (int s = 15; s >= 0; --s)
        if (m & (1u << s))
        {
            top = s * 16;
            break;
        }
    if (threadIdx.x == 0) *(volatile int32_t *)(c + dL.height_min) = INT_MAX;
    __syncthreads();

    int col = threadIdx.x;

    if (col < 256)
    {
        int x = col & 15, z = col >> 4;

        ((int32_t *)(c + dL.precip))[x + (z << 4)] = -999;

        int v = top + 15;

        while (v > 0)
        {
            if (bands(c)[(v - 1) >> 4] == 0)
            {
                v = ((v - 1) >> 4) << 4;
                continue;
            }

            uint8_t *s = sec_at(c, (v - 1) >> 4);

            if (dOp[s[dL.sec_ids + cell_in_sec(x, v - 1, z)]] == 0)
            {
                --v;
                continue;
            }
            heights(c)[z << 4 | x] = v;
            atomicMin((int *)(c + dL.height_min), v);
            break;
        }
    }
    __syncthreads();

    /* then the sky, as C fills it once every column's height is set */
    if (col < 256)
    {
        int x = col & 15, z = col >> 4;

        if (!c[dL.no_sky])
        {
            int light = 15, y = top + 15;

            do
            {
                uint8_t *s = sec_at(c, y >> 4);
                int id = s ? s[dL.sec_ids + cell_in_sec(x, y, z)] : 0;
                int o = dOp[id];

                if (o == 0 && light != 15) o = 1;
                light -= o;
                if (light > 0 && (m & (1u << (y >> 4)))) store_nib(c, dL.sec_sky, x, y, z, light);
                --y;
            }
            while (y > 0 && light > 0);
        }
    }
    __syncthreads();
}


static __device__ __forceinline__ int map_key(const cl_call &S, int x, int y, int z)
{
    if (y < 0) y = 0;
    if (y >= 256) y = 255;
    return ((x - S.x + 32) & 63) | ((y - S.y + 32) & 63) << 6 | ((z - S.z + 32) & 63) << 12;
}

static __device__ __forceinline__ int iabs(int v)
{
    return v < 0 ? -v : v;
}

/* World.updateLightByType on S's window: every thread of the block calls it */
__device__ void cl_update(cl_call &S, cl_scratch *d, cl_scan &ts)
{
    int tid = threadIdx.x;
    int32_t *q = d->queue, *writer = d->writer;

    if (tid == 0)
    {
        S.tail = S.head = 0;
        S.overflow = 0;
        S.nt = 0;
        S.spread = 1;

        int ok = box_loaded(S);

        S.ret = ok;
        if (ok)
        {
            int here = get_light(S, S.type, S.x, S.y, S.z);
            int want = compute(S, S.type, S.x, S.y, S.z);

            if (want > here) q[S.tail++] = 133152;
            else if (want < here)
            {
                q[S.tail++] = 133152 | here << 18;
                S.spread = 0;
            }
        }
    }
    __syncthreads();

    for (;;)
    {
        if (S.head >= S.tail)
        {
            int sp = S.spread;

            __syncthreads();
            if (sp) break;
            if (tid == 0)
            {
                S.spread = 1;
                S.head = 0;
            }
            __syncthreads();
            continue;
        }

        int h = S.head, e = min(h + (int)blockDim.x, S.tail), spread = S.spread, type = S.type;
        int i = h + tid, active = i < e;
        int px = 0, py = 0, pz = 0, lvl = 0, got = 0, v = 0, dist = 0;
        int touches = 0, marks = 0, expand = 0, create = 0;
        unsigned cand = 0, nls = 0;
        uint8_t *pc = NULL;

        if (tid == 0)
        {
            S.jstar = CL_NONE;
            S.wnew = 0;
            for (int j = 0; j < 4; ++j) S.wenew[j] = 0;
            S.jcreate = CL_NONE;
            S.acc = 0;
            S.cr = -1;
        }
        __syncthreads();

        if (active)
        {
            int ent = q[i];

            px = (ent & 63) - 32 + S.x;
            py = (ent >> 6 & 63) - 32 + S.y;
            pz = (ent >> 12 & 63) - 32 + S.z;
            lvl = ent >> 18 & 15;
            dist = iabs(px - S.x) + iabs(py - S.y) + iabs(pz - S.z);
            pc = xz_ok(px) && xz_ok(pz) ? win_at(S, px >> 4, pz >> 4) : NULL;

            int inw = pc != NULL && py >= 0 && py < 256;

            got = get_light(S, type, px, py, pz);
            if (!spread)
            {
                int fire = got == lvl;

                touches = fire && inw;
                marks = touches && got != 0;
                expand = fire && lvl > 0 && dist < 17;
                v = 0;
                if (expand)
                    for (int f = 0; f < 6; ++f)
                    {
                        int nx = px + FX[f], ny = py + FY[f], nz = pz + FZ[f];
                        int op = dOp[get_block(S, nx, ny, nz)];
                        int nl = lvl - (op < 1 ? 1 : op);
                        /* above 255 or below 0 the read is clamped to this
                         * very cell, which C has just set to 0 */
                        int self = touches && (ny < 0 || ny > 255);

                        if (nl >= 0 && (self ? 0 : get_light(S, type, nx, ny, nz)) == nl)
                        {
                            cand |= 1u << f;
                            nls |= (unsigned)nl << (4 * f);
                        }
                    }
            }
            else
            {
                v = compute(S, type, px, py, pz);
                touches = v != got && inw;
                marks = touches;
                expand = v > got && dist < 17;
                if (expand)
                    for (int f = 0; f < 6; ++f)
                    {
                        int ny = py + SY[f];
                        /* a clamped read of this very cell sees C's write */
                        int nl = touches && (ny < 0 || ny > 255) ? v : get_light(S, type, px + SX[f], ny, pz + SZ[f]);

                        if (nl < v) cand |= 1u << f;
                    }
            }
            create = touches && !present(pc, py);
            if (marks) atomicMin(&writer[map_key(S, px, py, pz)], i);
            if (create) atomicMin(&S.jcreate, i);
        }
        __syncthreads();

        if (active)
        {
            int own = writer[map_key(S, px, py, pz)] < i, nconf = 0;
            int reads = spread || (!own && expand);

            /* an entry outside the world reads the clamped cell another
             * entry wrote: not the same cell, so evaluated again */
            if (own && (py < 0 || py > 255))
            {
                own = 0;
                nconf = 1;
            }

            if (reads)
                for (int f = 0; f < 6; ++f)
                    if (writer[map_key(S, px + FX[f], py + FY[f], pz + FZ[f])] < i) nconf = 1;
            if (nconf) atomicMin(&S.jstar, i);
            else if (own)
            {
                /* a cell an earlier entry of the window wrote: the decrease
                 * left it 0, the spread left it at the light this entry
                 * computes (same neighbours), so the entry does what it
                 * does on that value */
                if (!spread)
                {
                    touches = lvl == 0 && pc != NULL && py >= 0 && py < 256;
                    expand = 0;
                    cand = 0;
                }
                else
                {
                    touches = 0;
                    expand = 0;
                    cand = 0;
                }
                create = touches && !present(pc, py);
            }
        }
        __syncthreads();

        int jend = min(S.jstar, e);

        if (S.jcreate != CL_NONE) jend = min(jend, S.jcreate + 1);

        int committed = active && i < jend;
        /* the section-making entry reads its neighbours after the section
         * exists (C's world_set_light makes it and regenerates the chunk's
         * sky before the entry's neighbour loop): its appends come after the
         * window's (it is the last committed), below */
        int crx = committed && touches && create;
        int ncand = committed && expand && !crx ? __popc(cand) : 0;
        int pre, agg;

        /* the chunks and edges this window writes first in the record: their
         * values from before the record are cleared, so the atomicMax of the
         * writes' epochs is C's last write */
        if (committed && touches)
        {
            unsigned b = 1u << win_index(S, px >> 4, pz >> 4);

            atomicOr(&S.wnew, b);
            if (!present(pc, py))
                for (int j = 0; j < 4; ++j) atomicOr(&S.wenew[j], b);
            else
            {
                if ((px & 15) == 0) atomicOr(&S.wenew[0], b);
                if ((px & 15) == 15) atomicOr(&S.wenew[1], b);
                if ((pz & 15) == 0) atomicOr(&S.wenew[2], b);
                if ((pz & 15) == 15) atomicOr(&S.wenew[3], b);
            }
        }
        __syncthreads();
        if (tid < LC_WIN_N)
        {
            unsigned b = 1u << tid;
            uint8_t *c = S.win[tid];

            if ((S.wnew & b) && !(S.touched & b))
            {
                *(volatile unsigned long long *)(c + dL.stamp) = 0;
                *(volatile unsigned long long *)(c + dL.wseq) = 0;
            }
            for (int j = 0; j < 4; ++j)
                if ((S.wenew[j] & b) && !(S.etouched[j] & b)) ((volatile unsigned long long *)(c + dL.edge_stamp))[j] = 0;
        }
        __syncthreads();
        if (tid == 0)
        {
            S.touched |= S.wnew;
            for (int j = 0; j < 4; ++j) S.etouched[j] |= S.wenew[j];
        }

        cl_exclusive_sum(ts, committed ? (touches << 16 | ncand) : 0, pre, agg);

        int tpre = pre >> 16, apre = pre & 0xffff;

        if (committed)
        {
            uint64_t k = (uint64_t)S.nt + (uint64_t)tpre + 1;

            if (touches && !create)
            {
                touch(S, pc, px & 15, py, pz & 15, k);
                atomicOr(&S.touched, 1u << win_index(S, px >> 4, pz >> 4));
                if (!(type == SKY && pc[dL.no_sky]))
                    store_nib(pc, type == SKY ? dL.sec_sky : dL.sec_blocklight, px & 15, py, pz & 15, v);
            }
            else if (touches)
            {
                S.cr = i;
                S.cr_x = px;
                S.cr_y = py;
                S.cr_z = pz;
                S.cr_v = v;
                S.cr_k = (int)k;
                S.cr_exp = expand;
                S.cr_lvl = lvl;
            }

            if (ncand)
            {
                int pos = S.tail + apre, n = 0;

                if (!spread)
                {
                    for (int f = 0; f < 6; ++f)
                    {
                        if (!(cand >> f & 1)) continue;
                        if (pos < CL_QUEUE)
                        {
                            q[pos++] = (px + FX[f] - S.x + 32) | (py + FY[f] - S.y + 32) << 6 |
                                       (pz + FZ[f] - S.z + 32) << 12 | (int)(nls >> (4 * f) & 15) << 18;
                            ++n;
                        }
                        else S.overflow = 1;
                    }
                }
                else if (pos < CL_QUEUE - 6)
                {
                    for (int f = 0; f < 6; ++f)
                        if (cand >> f & 1)
                        {
                            q[pos++] = (px + SX[f] - S.x + 32) | (py + SY[f] - S.y + 32) << 6 | (pz + SZ[f] - S.z + 32) << 12;
                            ++n;
                        }
                }
                else S.overflow = 1;
                if (n) atomicAdd(&S.acc, n);
            }
        }
        /* a spread entry refused at the cap with nothing to append still
         * counts: C skips it at the same test */
        if (committed && spread && expand && !ncand && !crx && S.tail + apre >= CL_QUEUE - 6) S.overflow = 1;
        __syncthreads();

        if (active && marks) writer[map_key(S, px, py, pz)] = CL_NONE;

        if (S.cr >= 0)
        {
            uint8_t *c = win_at(S, S.cr_x >> 4, S.cr_z >> 4);

            if (tid == 0)
            {
                touch(S, c, S.cr_x & 15, S.cr_y, S.cr_z & 15, (uint64_t)S.cr_k);
                atomicOr(&S.touched, 1u << win_index(S, S.cr_x >> 4, S.cr_z >> 4));
                S.regen |= 1u << win_index(S, S.cr_x >> 4, S.cr_z >> 4);
                *(volatile uint16_t *)(c + dL.mask) = (uint16_t)(cmask(c) | 1u << (S.cr_y >> 4));
            }
            __syncthreads();
            gen_skylight_map(c);
            if (tid == 0 && !(type == SKY && c[dL.no_sky]))
                store_nib(c, type == SKY ? dL.sec_sky : dL.sec_blocklight, S.cr_x & 15, S.cr_y, S.cr_z & 15, S.cr_v);
            if (tid == 0 && S.cr_exp)
            {
                /* its neighbours on the state after the section, as C reads
                 * them, appended after every other entry's of the window */
                int x = S.cr_x, y = S.cr_y, z = S.cr_z, pos = S.tail + S.acc, n = 0;

                if (!spread)
                {
                    for (int f = 0; f < 6; ++f)
                    {
                        int nx = x + FX[f], ny = y + FY[f], nz = z + FZ[f];
                        int op = dOp[get_block(S, nx, ny, nz)];
                        int nl = S.cr_lvl - (op < 1 ? 1 : op);

                        if (nl < 0 || get_light(S, type, nx, ny, nz) != nl) continue;
                        if (pos < CL_QUEUE)
                        {
                            q[pos++] = (nx - S.x + 32) | (ny - S.y + 32) << 6 | (nz - S.z + 32) << 12 | nl << 18;
                            ++n;
                        }
                        else S.overflow = 1;
                    }
                }
                else if (pos < CL_QUEUE - 6)
                {
                    for (int f = 0; f < 6; ++f)
                    {
                        int nx = x + SX[f], ny = y + SY[f], nz = z + SZ[f];

                        if (get_light(S, type, nx, ny, nz) >= S.cr_v) continue;
                        q[pos++] = (nx - S.x + 32) | (ny - S.y + 32) << 6 | (nz - S.z + 32) << 12;
                        ++n;
                    }
                }
                else S.overflow = 1;
                S.acc += n;
            }
        }
        __syncthreads();

        if (tid == 0)
        {
            S.head = jend;
            S.tail += S.acc;
            S.nt += (uint32_t)(agg >> 16);
            S.windows++;
        }
        __syncthreads();
    }
    if (tid == 0)
    {
        S.epoch += S.nt;
        S.wseq += S.nt;
        S.calls++;
        S.tail_sum += (uint64_t)S.tail;
        S.ovf_any |= S.overflow;
    }
    __syncthreads();
}

/* one update of type at (x, y, z); its return value */
__device__ int cl_run_update(cl_call &S, cl_scratch *d, cl_scan &ts, int type, int x, int y, int z)
{
    if (threadIdx.x == 0)
    {
        S.type = type;
        S.x = x;
        S.y = y;
        S.z = z;
    }
    __syncthreads();
    cl_update(S, d, ts);

    int r = S.ret;

    __syncthreads();
    return r;
}

/* World.func_147451_t: the sky pass where the world has a sky, then block light */
__device__ int cl_check_light_at(cl_call &S, cl_scratch *d, cl_scan &ts, int x, int y, int z)
{
    int changed = 0;

    if (S.dim == 0) changed |= cl_run_update(S, d, ts, SKY, x, y, z);
    changed |= cl_run_update(S, d, ts, BLOCK, x, y, z);
    return changed;
}
