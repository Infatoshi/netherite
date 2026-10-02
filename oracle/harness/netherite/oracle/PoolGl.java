package netherite.oracle;

import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL14;
import org.lwjgl.opengl.GLContext;

/**
 * A pool member's GL state between jobs. The heap rewind puts every static
 * back, but the GL context keeps what a job left in it: RenderSlime enables
 * GL_NORMALIZE and nothing in oracle/src disables it, so a member that drew a
 * slime would light the next job's scaled draws (a sign board) as a fresh
 * JVM does only after its own first slime. At the pristine snapshot
 * (Pool.init: the state a fresh run launches its world in) the member pushes
 * every server and client attribute group and keeps each matrix stack's top;
 * between jobs it pops them back and pushes them again, so each job starts
 * from the capture's fixed-function state. Display lists a job compiled are
 * rebuilt by the next world; texture objects a job made are the rewind's
 * (Pool.rewind).
 *
 * Two kinds of state the attribute stacks do not hold are restored one by
 * one (lane/poolleak):
 * - GL_FOG_DISTANCE_MODE_NV (EXTRA): EntityRenderer.setupFog sets it to
 *   GL_EYE_RADIAL_NV on its air and blindness paths only; the water, lava
 *   and cloud paths keep what is there, and Mesa's glPopAttrib does not
 *   restore it. A member that drew any frame in air drew the next job's
 *   first underwater frame with radial fog where a fresh JVM has the
 *   default GL_EYE_PLANE_ABSOLUTE_NV (render/worldfx_underwater's t=25 sky
 *   and every fogged draw after it, 1 to 6 levels darker).
 * - The contents and sampling parameters of the textures that exist at the
 *   capture (TEX): the lightmap (EntityRenderer.enableLightmap sets its
 *   filters, updateLightmap its texels), the framebuffer's colour texture,
 *   the atlases, the font. Each level is read back and compared, and put
 *   back where a job changed it.
 *
 * The fingerprint (capabilities, the fixed-function values a vanilla client
 * sets, and EXTRA) checks it: after the restore it must equal the capture's,
 * or the member ends. What differed before the restore is logged as the
 * job's leak (ORACLE POOL gl restored ...). --pool-skip-reset gl is the
 * negative control: the member keeps the last job's GL state.
 */
final class PoolGl
{
    private PoolGl() {}

    static final int[] CAPS = {
        GL11.GL_ALPHA_TEST, GL11.GL_BLEND, GL11.GL_COLOR_MATERIAL, GL11.GL_CULL_FACE, GL11.GL_DEPTH_TEST, GL11.GL_FOG,
        GL11.GL_LIGHTING, GL11.GL_LIGHT0, GL11.GL_LIGHT1, GL11.GL_LIGHT2, GL11.GL_NORMALIZE, GL12.GL_RESCALE_NORMAL,
        GL11.GL_POLYGON_OFFSET_FILL, GL11.GL_POLYGON_OFFSET_LINE, GL11.GL_LINE_SMOOTH, GL11.GL_POINT_SMOOTH,
        GL11.GL_POLYGON_SMOOTH, GL11.GL_SCISSOR_TEST, GL11.GL_STENCIL_TEST, GL11.GL_COLOR_LOGIC_OP, GL11.GL_DITHER,
        GL11.GL_TEXTURE_GEN_S, GL11.GL_TEXTURE_GEN_T, GL11.GL_TEXTURE_GEN_R, GL11.GL_TEXTURE_GEN_Q, GL11.GL_CLIP_PLANE0,
        GL11.GL_TEXTURE_1D, GL11.GL_TEXTURE_2D, GL11.GL_VERTEX_ARRAY, GL11.GL_NORMAL_ARRAY, GL11.GL_COLOR_ARRAY,
        GL11.GL_TEXTURE_COORD_ARRAY,
    };
    static final String[] CAP_NAMES = {
        "ALPHA_TEST", "BLEND", "COLOR_MATERIAL", "CULL_FACE", "DEPTH_TEST", "FOG", "LIGHTING", "LIGHT0", "LIGHT1", "LIGHT2",
        "NORMALIZE", "RESCALE_NORMAL", "POLYGON_OFFSET_FILL", "POLYGON_OFFSET_LINE", "LINE_SMOOTH", "POINT_SMOOTH",
        "POLYGON_SMOOTH", "SCISSOR_TEST", "STENCIL_TEST", "COLOR_LOGIC_OP", "DITHER", "TEXTURE_GEN_S", "TEXTURE_GEN_T",
        "TEXTURE_GEN_R", "TEXTURE_GEN_Q", "CLIP_PLANE0", "TEXTURE_1D", "TEXTURE_2D", "VERTEX_ARRAY", "NORMAL_ARRAY",
        "COLOR_ARRAY", "TEXTURE_COORD_ARRAY",
    };
    static final int[] INTS = {
        GL11.GL_BLEND_SRC, GL11.GL_BLEND_DST, GL11.GL_DEPTH_FUNC, GL11.GL_DEPTH_WRITEMASK, GL11.GL_ALPHA_TEST_FUNC,
        GL11.GL_SHADE_MODEL, GL11.GL_CULL_FACE_MODE, GL11.GL_FRONT_FACE, GL11.GL_MATRIX_MODE, GL11.GL_FOG_MODE,
        GL11.GL_COLOR_MATERIAL_FACE, GL11.GL_COLOR_MATERIAL_PARAMETER, GL13.GL_ACTIVE_TEXTURE, GL13.GL_CLIENT_ACTIVE_TEXTURE,
        GL11.GL_TEXTURE_BINDING_2D, GL11.GL_LIST_BASE, GL11.GL_ATTRIB_STACK_DEPTH, GL11.GL_CLIENT_ATTRIB_STACK_DEPTH,
        GL11.GL_MODELVIEW_STACK_DEPTH, GL11.GL_PROJECTION_STACK_DEPTH, GL11.GL_TEXTURE_STACK_DEPTH,
    };
    static final String[] INT_NAMES = {
        "BLEND_SRC", "BLEND_DST", "DEPTH_FUNC", "DEPTH_WRITEMASK", "ALPHA_TEST_FUNC", "SHADE_MODEL", "CULL_FACE_MODE",
        "FRONT_FACE", "MATRIX_MODE", "FOG_MODE", "COLOR_MATERIAL_FACE", "COLOR_MATERIAL_PARAMETER", "ACTIVE_TEXTURE",
        "CLIENT_ACTIVE_TEXTURE", "TEXTURE_BINDING_2D", "LIST_BASE", "ATTRIB_STACK_DEPTH", "CLIENT_ATTRIB_STACK_DEPTH",
        "MODELVIEW_STACK_DEPTH", "PROJECTION_STACK_DEPTH", "TEXTURE_STACK_DEPTH",
    };
    static final int[] FLOATS = {
        GL11.GL_CURRENT_COLOR, GL11.GL_CURRENT_NORMAL, GL11.GL_CURRENT_TEXTURE_COORDS, GL11.GL_ALPHA_TEST_REF,
        GL11.GL_FOG_START, GL11.GL_FOG_END, GL11.GL_FOG_DENSITY, GL11.GL_FOG_COLOR, GL11.GL_LIGHT_MODEL_AMBIENT,
        GL11.GL_COLOR_CLEAR_VALUE, GL11.GL_LINE_WIDTH, GL11.GL_POLYGON_OFFSET_FACTOR, GL11.GL_POLYGON_OFFSET_UNITS,
        GL11.GL_VIEWPORT, GL11.GL_COLOR_WRITEMASK, GL11.GL_MODELVIEW_MATRIX, GL11.GL_PROJECTION_MATRIX, GL11.GL_TEXTURE_MATRIX,
    };
    static final String[] FLOAT_NAMES = {
        "CURRENT_COLOR", "CURRENT_NORMAL", "CURRENT_TEXTURE_COORDS", "ALPHA_TEST_REF", "FOG_START", "FOG_END",
        "FOG_DENSITY", "FOG_COLOR", "LIGHT_MODEL_AMBIENT", "COLOR_CLEAR_VALUE", "LINE_WIDTH", "POLYGON_OFFSET_FACTOR",
        "POLYGON_OFFSET_UNITS", "VIEWPORT", "COLOR_WRITEMASK", "MODELVIEW_MATRIX", "PROJECTION_MATRIX", "TEXTURE_MATRIX",
    };
    /** The texture units whose per-unit state is fingerprinted (vanilla uses 0 and the lightmap's 1). */
    static final int UNITS = 2;

    /** GL_NV_fog_distance's GL_FOG_DISTANCE_MODE_NV: vanilla sets it, glPushAttrib does not save it. */
    static final int FOG_DISTANCE_MODE_NV = 0x855A;
    static boolean fogDistance;
    static int fogDistanceMode;

    /** One texture object that exists at the capture: its sampling parameters and every level's texels. */
    static final class Tex
    {
        int id;
        int[] params;
        float[] fparams;
        int[] w, h;
        IntBuffer[] levels;
    }
    static final int[] TEX_PARAMS = {
        GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_TEXTURE_WRAP_S, GL11.GL_TEXTURE_WRAP_T,
        GL12.GL_TEXTURE_BASE_LEVEL, GL12.GL_TEXTURE_MAX_LEVEL,
    };
    static final int[] TEX_FPARAMS = { GL12.GL_TEXTURE_MIN_LOD, GL12.GL_TEXTURE_MAX_LOD, GL14.GL_TEXTURE_LOD_BIAS };
    /** EXT_texture_filter_anisotropic's GL_TEXTURE_MAX_ANISOTROPY_EXT (TextureUtil sets it where supported). */
    static final int TEXTURE_MAX_ANISOTROPY = 34046;
    /** The texture names looked at: a client's own at the pristine point are the first few dozen. */
    static final int TEX_NAMES = 1024;
    static Tex[] textures;
    static IntBuffer scratch;

    /** The capture's fingerprint and matrix tops (modelview, projection, texture per unit). */
    static String[] captured;
    static FloatBuffer[] matrices;
    static boolean skip;

    /** Pool.init, on the client thread: keep the pristine GL state. */
    static void capture()
    {
        skip = "gl".equals(Pool.skipReset);
        if (skip) System.out.println("ORACLE POOL negative control: the GL state is never restored");
        fogDistance = GLContext.getCapabilities().GL_NV_fog_distance;
        if (fogDistance) fogDistanceMode = GL11.glGetInteger(FOG_DISTANCE_MODE_NV);
        matrices = readMatrices();
        captureTextures();
        GL11.glPushAttrib(GL11.GL_ALL_ATTRIB_BITS);
        GL11.glPushClientAttrib(GL11.GL_ALL_CLIENT_ATTRIB_BITS);
        captured = fingerprint();
    }

    /** Pool.rewind: the capture's GL state back; null when it holds, else why the member cannot go on. */
    static String restore()
    {
        if (skip) return null;
        // an error the job left pending is the job's (vanilla's checkGLError clears them each frame)
        int pending = GL11.glGetError();
        if (pending != GL11.GL_NO_ERROR) Pool.out0.println("ORACLE POOL gl error " + pending + " pending after the job");
        String[] now = fingerprint();
        String leak = differences(captured, now);
        // a job must leave every stack as it found it: a job that did not cannot be undone by a pop
        if (!now[idx("ATTRIB_STACK_DEPTH")].equals(captured[idx("ATTRIB_STACK_DEPTH")])
            || !now[idx("CLIENT_ATTRIB_STACK_DEPTH")].equals(captured[idx("CLIENT_ATTRIB_STACK_DEPTH")]))
            return "a job left the GL attribute stacks unbalanced: " + leak;
        // the textures first: the pops then undo the bindings and pixel store the comparison used
        String tex = restoreTextures();
        if (tex != null && tex.startsWith("!")) return tex.substring(1);
        GL11.glPopClientAttrib();
        GL11.glPopAttrib();
        writeMatrices(matrices);
        if (fogDistance) GL11.glFogi(FOG_DISTANCE_MODE_NV, fogDistanceMode);
        GL11.glPushAttrib(GL11.GL_ALL_ATTRIB_BITS);
        GL11.glPushClientAttrib(GL11.GL_ALL_CLIENT_ATTRIB_BITS);
        String after = differences(captured, fingerprint());
        if (!after.isEmpty()) return "the GL state differs from the capture after the restore: " + after;
        int e = GL11.glGetError();
        if (e != GL11.GL_NO_ERROR) return "GL error " + e + " restoring the GL state";
        if (tex != null) leak = leak.isEmpty() ? tex : leak + " " + tex;
        if (!leak.isEmpty()) Pool.out0.println("ORACLE POOL gl restored " + leak);
        return null;
    }

    static int idx(String name)
    {
        for (int i = 0; i < INT_NAMES.length; ++i) if (INT_NAMES[i].equals(name)) return CAPS.length * UNITS + i;
        throw new IllegalArgumentException(name);
    }

    /** Each capability per unit, each integer, then each float value per unit, as text. */
    static String[] fingerprint()
    {
        int unit = GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE), client = GL11.glGetInteger(GL13.GL_CLIENT_ACTIVE_TEXTURE);
        String[] f = new String[CAPS.length * UNITS + INTS.length + FLOATS.length * UNITS + EXTRA_NAMES.length];
        IntBuffer ib = BufferUtils.createIntBuffer(16);
        FloatBuffer fb = BufferUtils.createFloatBuffer(16);
        int k = 0;
        for (int u = 0; u < UNITS; ++u)
        {
            GL13.glActiveTexture(GL13.GL_TEXTURE0 + u);
            GL13.glClientActiveTexture(GL13.GL_TEXTURE0 + u);
            for (int c : CAPS) f[k++] = GL11.glIsEnabled(c) ? "1" : "0";
        }
        GL13.glActiveTexture(unit);
        GL13.glClientActiveTexture(client);
        for (int c : INTS)
        {
            ib.clear();
            GL11.glGetInteger(c, ib);
            f[k++] = Integer.toString(ib.get(0));
        }
        for (int u = 0; u < UNITS; ++u)
        {
            GL13.glActiveTexture(GL13.GL_TEXTURE0 + u);
            for (int c : FLOATS)
            {
                fb.clear();
                for (int i = 0; i < 16; ++i) fb.put(i, Float.NaN);
                GL11.glGetFloat(c, fb);
                StringBuilder s = new StringBuilder();
                for (int i = 0; i < 16 && !Float.isNaN(fb.get(i)); ++i) s.append(i > 0 ? "," : "").append(fb.get(i));
                f[k++] = s.toString();
            }
        }
        GL13.glActiveTexture(unit);
        f[k++] = fogDistance ? Integer.toString(GL11.glGetInteger(FOG_DISTANCE_MODE_NV)) : "-";
        return f;
    }

    /** The state after the float groups, each read and restored by hand. */
    static final String[] EXTRA_NAMES = { "FOG_DISTANCE_MODE_NV" };

    static String name(int k)
    {
        int n = CAPS.length * UNITS;
        if (k < n) return CAP_NAMES[k % CAPS.length] + (k / CAPS.length > 0 ? "@" + k / CAPS.length : "");
        if (k < n + INTS.length) return INT_NAMES[k - n];
        k -= n + INTS.length;
        if (k < FLOATS.length * UNITS) return FLOAT_NAMES[k % FLOATS.length] + (k / FLOATS.length > 0 ? "@" + k / FLOATS.length : "");
        return EXTRA_NAMES[k - FLOATS.length * UNITS];
    }

    // ------------------------------------------------------------ textures

    /** Pool.init, before the pushes: every texture object that exists now, its parameters and texels. */
    static void captureTextures()
    {
        GL11.glPushAttrib(GL11.GL_TEXTURE_BIT);
        GL11.glPushClientAttrib(GL11.GL_CLIENT_PIXEL_STORE_BIT);
        pixelStore();
        java.util.List<Tex> out = new java.util.ArrayList<Tex>();
        int most = 0;
        GL13.glActiveTexture(GL13.GL_TEXTURE0);
        for (int id = 1; id < TEX_NAMES; ++id)
        {
            if (!GL11.glIsTexture(id)) continue;
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, id);
            if (GL11.glGetError() != GL11.GL_NO_ERROR) continue; // not a 2D texture: vanilla makes none
            Tex t = new Tex();
            t.id = id;
            t.params = new int[TEX_PARAMS.length];
            for (int i = 0; i < TEX_PARAMS.length; ++i) t.params[i] = GL11.glGetTexParameteri(GL11.GL_TEXTURE_2D, TEX_PARAMS[i]);
            t.fparams = fparams();
            java.util.List<IntBuffer> lv = new java.util.ArrayList<IntBuffer>();
            java.util.List<int[]> dims = new java.util.ArrayList<int[]>();
            for (int l = 0; l <= 16; ++l)
            {
                int w = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, l, GL11.GL_TEXTURE_WIDTH), h = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, l, GL11.GL_TEXTURE_HEIGHT);
                if (w <= 0 || h <= 0) break;
                IntBuffer b = BufferUtils.createIntBuffer(w * h);
                GL11.glGetTexImage(GL11.GL_TEXTURE_2D, l, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, b);
                lv.add(b);
                dims.add(new int[] {w, h});
                most = Math.max(most, w * h);
            }
            t.levels = lv.toArray(new IntBuffer[0]);
            t.w = new int[dims.size()];
            t.h = new int[dims.size()];
            for (int i = 0; i < t.w.length; ++i) { t.w[i] = dims.get(i)[0]; t.h[i] = dims.get(i)[1]; }
            out.add(t);
        }
        textures = out.toArray(new Tex[0]);
        scratch = BufferUtils.createIntBuffer(Math.max(1, most));
        GL11.glPopClientAttrib();
        GL11.glPopAttrib();
    }

    static float[] fparams()
    {
        float[] f = new float[TEX_FPARAMS.length + 1];
        for (int i = 0; i < TEX_FPARAMS.length; ++i) f[i] = GL11.glGetTexParameterf(GL11.GL_TEXTURE_2D, TEX_FPARAMS[i]);
        f[TEX_FPARAMS.length] = net.minecraft.client.renderer.OpenGlHelper.anisotropicFilteringSupported ? GL11.glGetTexParameterf(GL11.GL_TEXTURE_2D, TEXTURE_MAX_ANISOTROPY) : 0.0F;
        return f;
    }

    /** Tightly packed rows for the read-backs and uploads (the pops put the job's pixel store back after). */
    static void pixelStore()
    {
        for (int p : new int[] {GL11.GL_PACK_ROW_LENGTH, GL11.GL_PACK_SKIP_ROWS, GL11.GL_PACK_SKIP_PIXELS, GL11.GL_UNPACK_ROW_LENGTH, GL11.GL_UNPACK_SKIP_ROWS, GL11.GL_UNPACK_SKIP_PIXELS})
            GL11.glPixelStorei(p, 0);
        GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT, 4);
        GL11.glPixelStorei(GL11.GL_UNPACK_ALIGNMENT, 4);
        GL11.glPixelStorei(GL11.GL_PACK_SWAP_BYTES, 0);
        GL11.glPixelStorei(GL11.GL_UNPACK_SWAP_BYTES, 0);
    }

    /**
     * Each captured texture as the capture had it: its parameters and each
     * level's texels, compared and put back where they differ. Before the
     * pops (they undo the bindings and pixel store used here). Returns what
     * was put back ("tex 7 16x16 params texels ..."), null for nothing, or
     * "!why" when a texture cannot be put back (it was deleted or resized).
     */
    static String restoreTextures()
    {
        pixelStore();
        GL13.glActiveTexture(GL13.GL_TEXTURE0);
        StringBuilder s = new StringBuilder();
        for (Tex t : textures)
        {
            if (!GL11.glIsTexture(t.id)) return "!texture " + t.id + " of the capture was deleted";
            GL11.glBindTexture(GL11.GL_TEXTURE_2D, t.id);
            StringBuilder what = new StringBuilder();
            boolean params = false;
            for (int i = 0; i < TEX_PARAMS.length; ++i)
            {
                if (GL11.glGetTexParameteri(GL11.GL_TEXTURE_2D, TEX_PARAMS[i]) == t.params[i]) continue;
                GL11.glTexParameteri(GL11.GL_TEXTURE_2D, TEX_PARAMS[i], t.params[i]);
                params = true;
            }
            float[] f = fparams();
            for (int i = 0; i < TEX_FPARAMS.length; ++i)
            {
                if (Float.floatToRawIntBits(f[i]) == Float.floatToRawIntBits(t.fparams[i])) continue;
                GL11.glTexParameterf(GL11.GL_TEXTURE_2D, TEX_FPARAMS[i], t.fparams[i]);
                params = true;
            }
            if (Float.floatToRawIntBits(f[TEX_FPARAMS.length]) != Float.floatToRawIntBits(t.fparams[TEX_FPARAMS.length]))
            {
                GL11.glTexParameterf(GL11.GL_TEXTURE_2D, TEXTURE_MAX_ANISOTROPY, t.fparams[TEX_FPARAMS.length]);
                params = true;
            }
            if (params) what.append(" params");
            for (int l = 0; l < t.levels.length; ++l)
            {
                int w = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, l, GL11.GL_TEXTURE_WIDTH), h = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, l, GL11.GL_TEXTURE_HEIGHT);
                if (w != t.w[l] || h != t.h[l]) return "!texture " + t.id + " level " + l + " is " + w + "x" + h + ", " + t.w[l] + "x" + t.h[l] + " at the capture";
                scratch.clear();
                scratch.limit(w * h);
                GL11.glGetTexImage(GL11.GL_TEXTURE_2D, l, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, scratch);
                t.levels[l].clear();
                if (scratch.equals(t.levels[l])) continue;
                GL11.glTexSubImage2D(GL11.GL_TEXTURE_2D, l, 0, 0, w, h, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, t.levels[l]);
                what.append(" level").append(l);
            }
            if (what.length() > 0) s.append(s.length() > 0 ? " " : "").append("tex").append(t.id).append('(').append(t.w[0]).append('x').append(t.h[0]).append(')').append(what);
        }
        int e = GL11.glGetError();
        if (e != GL11.GL_NO_ERROR) return "!GL error " + e + " restoring the textures";
        return s.length() > 0 ? s.toString() : null;
    }

    static String differences(String[] a, String[] b)
    {
        StringBuilder s = new StringBuilder();
        for (int k = 0; k < a.length; ++k)
            if (!a[k].equals(b[k])) s.append(s.length() > 0 ? " " : "").append(name(k)).append('=').append(a[k]).append("->").append(b[k]);
        return s.toString();
    }

    /** The top of the modelview and projection stacks, and each unit's texture matrix. */
    static FloatBuffer[] readMatrices()
    {
        int mode = GL11.glGetInteger(GL11.GL_MATRIX_MODE), unit = GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);
        FloatBuffer[] m = new FloatBuffer[2 + UNITS];
        m[0] = BufferUtils.createFloatBuffer(16);
        GL11.glGetFloat(GL11.GL_MODELVIEW_MATRIX, m[0]);
        m[1] = BufferUtils.createFloatBuffer(16);
        GL11.glGetFloat(GL11.GL_PROJECTION_MATRIX, m[1]);
        for (int u = 0; u < UNITS; ++u)
        {
            GL13.glActiveTexture(GL13.GL_TEXTURE0 + u);
            m[2 + u] = BufferUtils.createFloatBuffer(16);
            GL11.glGetFloat(GL11.GL_TEXTURE_MATRIX, m[2 + u]);
        }
        GL13.glActiveTexture(unit);
        GL11.glMatrixMode(mode);
        return m;
    }

    static void writeMatrices(FloatBuffer[] m)
    {
        int mode = GL11.glGetInteger(GL11.GL_MATRIX_MODE), unit = GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);
        GL11.glMatrixMode(GL11.GL_MODELVIEW);
        m[0].rewind();
        GL11.glLoadMatrix(m[0]);
        GL11.glMatrixMode(GL11.GL_PROJECTION);
        m[1].rewind();
        GL11.glLoadMatrix(m[1]);
        GL11.glMatrixMode(GL11.GL_TEXTURE);
        for (int u = 0; u < UNITS; ++u)
        {
            GL13.glActiveTexture(GL13.GL_TEXTURE0 + u);
            m[2 + u].rewind();
            GL11.glLoadMatrix(m[2 + u]);
        }
        GL13.glActiveTexture(unit);
        GL11.glMatrixMode(mode);
    }
}
