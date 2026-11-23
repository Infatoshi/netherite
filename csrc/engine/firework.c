/* ItemFireworkCharge and ItemFirework's client half; see firework.h. */
#include "firework.h"
#include "itemtag.h"
#include "lang.h"
#include "nbtjson.h"

#include <stdio.h>
#include <string.h>

/* ItemDye.field_150923_a as item.fireworksCharge.<colour> reads in en_US. */
static const char *const DYE_KEYS[16] = {
    "item.fireworksCharge.black",
    "item.fireworksCharge.red",
    "item.fireworksCharge.green",
    "item.fireworksCharge.brown",
    "item.fireworksCharge.blue",
    "item.fireworksCharge.purple",
    "item.fireworksCharge.cyan",
    "item.fireworksCharge.silver",
    "item.fireworksCharge.gray",
    "item.fireworksCharge.pink",
    "item.fireworksCharge.lime",
    "item.fireworksCharge.yellow",
    "item.fireworksCharge.lightBlue",
    "item.fireworksCharge.magenta",
    "item.fireworksCharge.orange",
    "item.fireworksCharge.white",
};

/* ItemFireworkCharge.func_150903_a(stack, key): the Explosion compound's key
 * (getCompoundTag hands back an empty compound when it is missing). */
static const nbt *explosion_key(const nbt *root, const char *key)
{
    const nbt *e = root ? nbt_get(root, "Explosion") : NULL;
    return e && nbt_kind(e) == NBT_COMPOUND ? nbt_get(e, key) : NULL;
}

int firework_charge_color(int tag)
{
    int color = 9079434;
    nbt *root = tag ? itag_tree(tag) : NULL;
    const nbt *c = explosion_key(root, "Colors");
    int n = 0;
    const int *v = c && nbt_kind(c) == NBT_INT_ARRAY ? nbt_int_array(c, &n) : NULL;
    if (v != NULL && n == 1) color = v[0];
    else if (v != NULL && n > 1)
    {
        int r = 0, g = 0, b = 0;
        for (int i = 0; i < n; ++i)
        {
            r += (v[i] & 16711680) >> 16;
            g += (v[i] & 65280) >> 8;
            b += v[i] & 255;
        }
        color = r / n << 16 | g / n << 8 | b / n;
    }
    nbt_free(root);
    return color;
}

/* The colour words of one int array: a dye's name, else "Custom". */
static void color_words(char *out, size_t cap, size_t at, const int *v, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const char *w = lang_text("item.fireworksCharge.customColor");
        for (int d = 0; d < 16; ++d)
            if (v[i] == craft_dye_firework_color(d)) { w = lang_text(DYE_KEYS[d]); break; }
        at += (size_t)snprintf(out + at, at < cap ? cap - at : 0, "%s%s", i ? ", " : "", w);
        if (at >= cap) return;
    }
}

/* NBTTagCompound.getByte: a number's low byte, 0 for anything else. */
static int get_byte(const nbt *e, const char *key)
{
    const nbt *v = e ? nbt_get(e, key) : NULL;
    return v != NULL && nbt_kind(v) <= NBT_LONG ? (signed char)nbt_int_value(v) : 0;
}

/* ItemFireworkCharge.func_150902_a (e NULL: getCompoundTag's empty
 * compound for a missing key): the shape, the colours, the fade, the
 * trail and the twinkle. */
static int explosion_lines(const nbt *e, char *lines, size_t stride, int max)
{
    static const char *const SHAPE[5] = {"item.fireworksCharge.type.0", "item.fireworksCharge.type.1", "item.fireworksCharge.type.2", "item.fireworksCharge.type.3", "item.fireworksCharge.type.4"};
    int n = 0;
#define LINE(...) do { if (n < max) snprintf(lines + (size_t)n++ * stride, stride, __VA_ARGS__); } while (0)
    int type = get_byte(e, "Type");
    LINE("%s", type >= 0 && type <= 4 ? lang_text(SHAPE[type]) : lang_text("item.fireworksCharge.type"));
    int nc = 0, nf = 0;
    const nbt *c = e ? nbt_get(e, "Colors") : NULL, *f = e ? nbt_get(e, "FadeColors") : NULL;
    const int *cv = c && nbt_kind(c) == NBT_INT_ARRAY ? nbt_int_array(c, &nc) : NULL;
    const int *fv = f && nbt_kind(f) == NBT_INT_ARRAY ? nbt_int_array(f, &nf) : NULL;
    if (cv && nc > 0 && n < max)
    {
        char *out = lines + (size_t)n++ * stride;
        out[0] = 0;
        color_words(out, stride, 0, cv, nc);
    }
    if (fv && nf > 0 && n < max)
    {
        char *out = lines + (size_t)n++ * stride;
        size_t at = (size_t)snprintf(out, stride, "%s ", lang_text("item.fireworksCharge.fadeTo"));
        color_words(out, stride, at, fv, nf);
    }
    if (get_byte(e, "Trail")) LINE("%s", lang_text("item.fireworksCharge.trail"));
    if (get_byte(e, "Flicker")) LINE("%s", lang_text("item.fireworksCharge.flicker"));
#undef LINE
    return n;
}

int firework_info_lines(int id, int tag, char *lines, size_t stride, int max)
{
    if (tag == 0 || (id != 401 && id != 402)) return 0;
    nbt *root = itag_tree(tag);
    int n = 0;
    if (id == 402)
    {
        const nbt *e = root ? nbt_get(root, "Explosion") : NULL;
        n = explosion_lines(e && nbt_kind(e) == NBT_COMPOUND ? e : NULL, lines, stride, max);
    }
    else
    {
        /* ItemFirework.addInformation: "Flight Duration: N" when the byte is
         * there, then each explosion's lines with all but the first two
         * spaces in */
        const nbt *fw = root ? nbt_get(root, "Fireworks") : NULL;
        if (fw && nbt_kind(fw) == NBT_COMPOUND)
        {
            const nbt *fl = nbt_get(fw, "Flight");
            /* func_150297_b("Flight", 99): any number */
            if (fl && nbt_kind(fl) <= NBT_DOUBLE && n < max)
                snprintf(lines + (size_t)n++ * stride, stride, "%s %d", lang_text("item.fireworks.flight"), get_byte(fw, "Flight"));
            const nbt *ex = nbt_get(fw, "Explosions");
            for (int i = 0; ex && nbt_kind(ex) == NBT_LIST && i < nbt_list_size(ex) && n < max; ++i)
            {
                const nbt *e = nbt_list_get(ex, i);
                if (!e || nbt_kind(e) != NBT_COMPOUND) continue;
                int k = explosion_lines(e, lines + (size_t)n * stride, stride, max - n);
                for (int j = 1; j < k; ++j)
                {
                    char *l = lines + (size_t)(n + j) * stride;
                    size_t len = strlen(l);
                    if (len + 2 >= stride) len = stride - 3;
                    memmove(l + 2, l, len + 1);
                    l[0] = l[1] = ' ';
                }
                n += k;
            }
        }
    }
    nbt_free(root);
    return n;
}
