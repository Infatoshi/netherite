package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.charset.Charset;
import java.util.HashMap;
import java.util.Map;
import java.util.TreeSet;
import net.minecraft.block.Block;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.client.gui.GuiIngame;
import net.minecraft.client.gui.ScaledResolution;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.DestroyBlockProgress;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.passive.EntityChicken;
import net.minecraft.entity.passive.EntitySheep;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.boss.BossStatus;
import net.minecraft.item.Item;
import net.minecraft.item.ItemBlock;
import net.minecraft.item.ItemStack;
import net.minecraft.potion.Potion;
import net.minecraft.util.IIcon;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ResourceLocation;
import net.minecraft.world.biome.BiomeGenBase;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;

/**
 * Per-frame render state dump: everything the client computes before it draws a
 * frame that decides pixels. Recorded during a tape replay started with
 * --renderstate DIR; the three hook calls in EntityRenderer.renderWorld do
 * nothing when the recorder is off. One line per frame in frames.jsonl:
 * the inputs (options, the renderer's own state, the client player, the world's
 * time and weather) and the outputs (the lightmap, the fog color, the two GL
 * matrices, the camera position, the sky/fog/cloud/sunrise colors, star and sun
 * brightness, the celestial angle, and the RENDER role's Det state). Floats and
 * doubles go out as raw-bit hex ("f:" / "d:") so nothing rounds on the way
 * through JSON.
 *
 * Hooks (all in EntityRenderer.renderWorld, one line each):
 *   fog(...)     after updateFogColor: renderer + world inputs, world getters,
 *                the final fog color fields
 *   camera(...)  after setupCameraTransform: the projection and modelview
 *                matrices read back with glGetFloat, the camera position
 *   fogGL()      after the prepareterrain setupFog(0): the GL fog state
 */
public final class RenderStateProbe
{
    static PrintWriter out;
    static JsonObject row;
    static int frames;
    static String tape;
    static File dir;
    static boolean hudTextures;
    static boolean mobTextures;

    static FloatBuffer fbuf16, fbuf4;
    static IntBuffer ibuf16;
    static final Map<String, Field> fields = new HashMap<String, Field>();

    private RenderStateProbe() {}

    /** Main flag --renderstate DIR: the recorder is on for this run. */
    public static void open(String dir) throws IOException
    {
        File f = new File(dir);
        if (!f.isDirectory() && !f.mkdirs()) throw new IOException("renderstate: cannot mkdir " + dir);
        tape = Oracle.replayPath != null ? Oracle.replayPath : Oracle.tapePath;
        RenderStateProbe.dir = f;
        out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "frames.jsonl")), "UTF-8"));
        fbuf16 = BufferUtils.createFloatBuffer(16);
        ibuf16 = BufferUtils.createIntBuffer(16);
        Runtime.getRuntime().addShutdownHook(new Thread(new Runnable()
        {
            public void run()
            {
                RenderStateProbe.close();
            }
        }, "RenderState Close"));
        System.out.println("ORACLE RENDERSTATE on");
    }

    static synchronized void close()
    {
        if (out == null) return;
        PrintWriter w = out;
        out = null;
        w.close();
        try
        {
            File f = new File(new File("."), "manifest.json").getAbsoluteFile();
            PrintWriter m = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
            JsonObject o = new JsonObject();
            o.addProperty("kind", "renderstate");
            o.addProperty("tape", tape);
            o.addProperty("seed", Oracle.seed);
            o.addProperty("frames", frames);
            m.println(o.toString());
            m.close();
        }
        catch (IOException e)
        {
            throw new RuntimeException(e);
        }
    }

    static String f(float v) { return String.format("f:%08x", Float.floatToRawIntBits(v)); }
    static String d(double v) { return String.format("d:%016x", Double.doubleToRawLongBits(v)); }

    static Field fld(Object o, String name)
    {
        String k = o.getClass().getName() + "." + name;
        Field f = fields.get(k);
        if (f == null)
        {
            try
            {
                for (Class<?> c = o.getClass(); f == null && c != null; c = c.getSuperclass())
                {
                    try { f = c.getDeclaredField(name); }
                    catch (NoSuchFieldException e) { }
                }
                if (f == null) throw new NoSuchFieldException(name + " on " + o.getClass().getName());
                f.setAccessible(true);
            }
            catch (Exception e)
            {
                throw new RuntimeException(e);
            }
            fields.put(k, f);
        }
        return f;
    }

    static Object getf(Object o, String name)
    {
        try
        {
            return fld(o, name).get(o);
        }
        catch (IllegalAccessException e)
        {
            throw new RuntimeException(e);
        }
    }

    static JsonArray farr(float[] v, int n)
    {
        JsonArray a = new JsonArray();
        for (int i = 0; i < n; ++i) a.add(new com.google.gson.JsonPrimitive(f(v[i])));
        return a;
    }

    static void put(JsonObject o, String k, float v) { o.addProperty(k, f(v)); }
    static void put(JsonObject o, String k, double v) { o.addProperty(k, d(v)); }
    static void put(JsonObject o, String k, Number v) { o.addProperty(k, v); }

    // -------------------------------------------------------------------- hud

    /**
     * The 2D overlay's inputs (GuiIngame, the achievement toast, the held item
     * tooltip) read at frame time, before the frame draws them. Everything the
     * overlay computes itself, like the toast's slide-in position, is left for
     * the renderer: only the inputs and both ends of the toast's animation
     * clock go out, and the clock is tick time in agent mode.
     */
    static JsonObject hud(Minecraft mc, EntityClientPlayerMP p, float pt) throws Exception
    {
        ScaledResolution sr = new ScaledResolution(mc, mc.displayWidth, mc.displayHeight);
        JsonObject o = new JsonObject();
        o.addProperty("sw", sr.getScaledWidth());
        o.addProperty("sh", sr.getScaledHeight());
        put(o, "swd", sr.getScaledWidth_double());
        put(o, "shd", sr.getScaledHeight_double());
        o.addProperty("sf", sr.getScaleFactor());
        o.addProperty("hide", mc.gameSettings.hideGUI ? 1 : 0);
        o.addProperty("screen", mc.currentScreen != null ? 1 : 0);
        JsonObject screen = ScreenSetup.capture(mc, pt);
        if (screen != null) o.add("guiScreen", screen);
        o.addProperty("fancy", Minecraft.isFancyGraphicsEnabled() ? 1 : 0);
        o.addProperty("scale", mc.gameSettings.guiScale);
        boolean cleanHud = netherite.oracle.Oracle.cleanHud();
        JsonObject toastJson = toast(mc);
        if (netherite.oracle.Oracle.cleanHudToast()) toastJson.addProperty("on", 0);
        o.add("toast", toastJson);
        o.add("bar", bars(mc, p));
        GuiIngame g = mc.ingameGUI;
        o.addProperty("uc", g.getUpdateCounter());
        put(o, "vign", g.prevVignetteBrightness);
        put(o, "bright", p.getBrightness(pt));
        o.add("chat", chat(mc, g));
        o.addProperty("boss", BossStatus.bossName);
        o.addProperty("bosst", BossStatus.statusBarTime);
        put(o, "bossh", BossStatus.healthScale);
        o.addProperty("rec", (String)getf(g, "recordPlaying"));
        o.addProperty("recup", cleanHud ? Integer.valueOf(0) : (Integer)getf(g, "recordPlayingUpFor"));
        o.addProperty("recplay", (Boolean)getf(g, "recordIsPlaying") ? 1 : 0);
        o.addProperty("tooltips", (cleanHud ? false : mc.gameSettings.heldItemTooltips) ? 1 : 0);
        o.addProperty("hlt", (Integer)getf(g, "remainingHighlightTicks"));
        ItemStack hl = (ItemStack)getf(g, "highlightingItemStack");
        o.add("hl", item(hl));
        o.addProperty("hlname", hl == null ? null : hl.getDisplayName());
        o.add("inv", inventory(p.inventory.mainInventory, 9));
        o.add("armor", inventory(p.inventory.armorInventory, 4));
        o.addProperty("cur", p.inventory.currentItem);
        // renderGameOverlay's two full-screen overlays: the pumpkin blur (first
        // person, a pumpkin on the head) and the portal swirl (its own timer,
        // suppressed by the confusion effect).
        o.addProperty("pumpkin", mc.gameSettings.thirdPersonView == 0
            && p.inventory.armorItemInSlot(3) != null
            && p.inventory.armorItemInSlot(3).getItem() == Item.getItemFromBlock(net.minecraft.init.Blocks.pumpkin) ? 1 : 0);
        put(o, "portal", p.prevTimeInPortal + (p.timeInPortal - p.prevTimeInPortal) * pt);
        o.addProperty("tportal", p.isPotionActive(Potion.confusion) ? 1 : 0);
        hudTextures(mc);
        return o;
    }

    /**
     * GuiNewChat at frame time: whether renderGameOverlay draws it (the --chat
     * hook), its settings, every message (field_146252_h, newest first) as the
     * parts func_146237_a splits, each its style's formatting code and its own
     * text, and the drawn lines (field_146253_i) as Java built them, which the
     * native split is checked against.
     */
    static JsonObject chat(Minecraft mc, GuiIngame g) throws Exception
    {
        net.minecraft.client.gui.GuiNewChat c = g.getChatGUI();
        net.minecraft.client.settings.GameSettings gs = mc.gameSettings;
        JsonObject o = new JsonObject();
        o.addProperty("on", netherite.oracle.Oracle.cleanHudChat() ? 0 : 1);
        o.addProperty("vis", gs.chatVisibility.getChatVisibility());
        o.addProperty("col", gs.chatColours ? 1 : 0);
        put(o, "op", gs.chatOpacity);
        put(o, "sc", gs.chatScale);
        put(o, "wd", gs.chatWidth);
        put(o, "hf", gs.chatHeightFocused);
        put(o, "hu", gs.chatHeightUnfocused);
        o.addProperty("open", c.func_146241_e() ? 1 : 0);
        o.addProperty("scroll", (Integer)getf(c, "field_146250_j"));
        JsonArray msgs = new JsonArray();
        for (Object lo : (java.util.List)getf(c, "field_146252_h"))
        {
            net.minecraft.client.gui.ChatLine line = (net.minecraft.client.gui.ChatLine)lo;
            JsonObject m = new JsonObject();
            m.addProperty("t", line.getUpdatedCounter());
            m.addProperty("id", line.getChatLineID());
            JsonArray parts = new JsonArray();
            for (Object po : com.google.common.collect.Lists.newArrayList(line.func_151461_a()))
            {
                net.minecraft.util.IChatComponent pc = (net.minecraft.util.IChatComponent)po;
                JsonArray part = new JsonArray();
                part.add(new com.google.gson.JsonPrimitive(pc.getChatStyle().getFormattingCode()));
                part.add(new com.google.gson.JsonPrimitive(pc.getUnformattedTextForChat()));
                parts.add(part);
            }
            m.add("parts", parts);
            msgs.add(m);
        }
        o.add("msgs", msgs);
        JsonArray lines = new JsonArray();
        for (Object lo : (java.util.List)getf(c, "field_146253_i"))
        {
            net.minecraft.client.gui.ChatLine line = (net.minecraft.client.gui.ChatLine)lo;
            JsonArray l = new JsonArray();
            l.add(new com.google.gson.JsonPrimitive(line.getUpdatedCounter()));
            l.add(new com.google.gson.JsonPrimitive(line.func_151461_a().getFormattedText()));
            lines.add(l);
        }
        o.add("lines", lines);
        return o;
    }

    /** The achievement toast GuiAchievement holds at frame time. */
    static JsonObject toast(Minecraft mc) throws Exception
    {
        Object ga = mc.guiAchievement;
        // func_146254_a only draws when the player is there and both the
        // achievement and its clock are set
        Object ach = getf(ga, "field_146266_k");
        JsonObject o = new JsonObject();
        o.addProperty("on", ach != null && (Long)getf(ga, "field_146263_l") != 0L
            && mc.thePlayer != null ? 1 : 0);
        o.addProperty("l", (Long)getf(ga, "field_146263_l"));
        o.addProperty("now", Minecraft.getSystemTime());
        o.addProperty("desc", (Boolean)getf(ga, "field_146262_n") ? 1 : 0);
        o.addProperty("title", (String)getf(ga, "field_146268_i"));
        o.addProperty("sub", (String)getf(ga, "field_146265_j"));
        o.add("item", ach == null ? null : item((ItemStack)getf(ach, "theItemStack")));
        return o;
    }

    /** The numbers func_110327_a and the rest of renderGameOverlay read. */
    static JsonObject bars(Minecraft mc, EntityClientPlayerMP p)
    {
        JsonObject o = new JsonObject();
        put(o, "hp", p.getHealth());
        put(o, "prevhp", p.prevHealth);
        IAttributeInstance mh = p.getEntityAttribute(SharedMonsterAttributes.maxHealth);
        put(o, "mhp", (float)mh.getAttributeValue());
        put(o, "abs", p.getAbsorptionAmount());
        o.addProperty("hurtres", p.hurtResistantTime);
        o.addProperty("armorv", p.getTotalArmorValue());
        o.addProperty("food", p.getFoodStats().getFoodLevel());
        o.addProperty("pfood", p.getFoodStats().getPrevFoodLevel());
        put(o, "sat", p.getFoodStats().getSaturationLevel());
        o.addProperty("air", p.getAir());
        o.addProperty("inwater", p.isInsideOfMaterial(net.minecraft.block.material.Material.water) ? 1 : 0);
        o.addProperty("hardcore", mc.theWorld.getWorldInfo().isHardcoreModeEnabled() ? 1 : 0);
        put(o, "xp", p.experience);
        o.addProperty("lvl", p.experienceLevel);
        o.addProperty("xpc", p.xpBarCap());
        o.addProperty("hud", mc.playerController.shouldDrawHUD() ? 1 : 0);
        o.addProperty("surv", mc.playerController.gameIsSurvivalOrAdventure() ? 1 : 0);
        o.addProperty("horse", p.isRidingHorse() ? 1 : 0);
        if (p.isRidingHorse()) put(o, "jump", p.getHorseJumpPower());
        Entity ride = p.ridingEntity;
        if (ride instanceof EntityLivingBase)
        {
            o.addProperty("mount", 1);
            put(o, "mhp2", ((EntityLivingBase)ride).getHealth());
            put(o, "mmax", ((EntityLivingBase)ride).getMaxHealth());
        }
        o.addProperty("poison", p.isPotionActive(Potion.poison) ? 1 : 0);
        o.addProperty("wither", p.isPotionActive(Potion.wither) ? 1 : 0);
        o.addProperty("regen", p.isPotionActive(Potion.regeneration) ? 1 : 0);
        o.addProperty("hunger", p.isPotionActive(Potion.hunger) ? 1 : 0);
        return o;
    }

    static JsonArray inventory(ItemStack[] slots, int n)
    {
        JsonArray a = new JsonArray();
        for (int i = 0; i < n; ++i) a.add(item(slots != null && i < slots.length ? slots[i] : null));
        return a;
    }

    /**
     * One item the overlay draws, the way RenderItem.renderItemIntoGUI picks
     * its path: 0 a flat icon from the sprite's own atlas, 1 a block as an
     * isometric model, 2 the multi-pass (layered) icon. The tint and the
     * sprite's uv range come along because the atlas dimensions are not known
     * to the renderer any other way.
     */
    static JsonObject item(ItemStack s)
    {
        if (s == null) return null;
        Item it = s.getItem();
        JsonObject o = new JsonObject();
        o.addProperty("id", Item.getIdFromItem(it));
        o.addProperty("dmg", s.getItemDamage());
        o.addProperty("n", s.stackSize);
        o.addProperty("sprite", s.getItemSpriteNumber());
        int mode = 0;
        if (s.getItemSpriteNumber() == 0 && it instanceof ItemBlock
            && RenderBlocks.renderItemIn3d(Block.getBlockFromItem(it).getRenderType())) mode = 1;
        else if (it.requiresMultipleRenderPasses()) mode = 2;
        o.addProperty("mode", mode);
        o.addProperty("effect", s.hasEffect() ? 1 : 0);
        // renderItemOverlayIntoGUI's durability bar reads isItemDamaged and
        // getMaxDamage: 0 here when the stack cannot be damaged
        o.addProperty("maxd", s.isItemStackDamageable() ? s.getMaxDamage() : 0);
        o.addProperty("tint", it.getColorFromItemStack(s, 0));
        IIcon ic = mode == 2 ? it.getIconFromDamageForRenderPass(s.getItemDamage(), 0) : s.getIconIndex();
        if (ic != null)
        {
            o.addProperty("icon", ic.getIconName());
            put(o, "minU", ic.getMinU());
            put(o, "maxU", ic.getMaxU());
            put(o, "minV", ic.getMinV());
            put(o, "maxV", ic.getMaxV());
        }
        if (mode == 2)
        {
            JsonObject next = new JsonObject();
            next.addProperty("tint", it.getColorFromItemStack(s, 1));
            IIcon second = it.getIconFromDamageForRenderPass(s.getItemDamage(), 1);
            if (second != null)
            {
                next.addProperty("icon", second.getIconName());
                put(next, "minU", second.getMinU());
                put(next, "maxU", second.getMaxU());
                put(next, "minV", second.getMinV());
                put(next, "maxV", second.getMaxV());
            }
            o.add("pass1", next);
        }
        return o;
    }

    // --------------------------------------------------------- gui textures

    /** The overlay's textures, in GL's own pixels, written once per recording. */
    static void hudTextures(Minecraft mc) throws Exception
    {
        if (hudTextures) return;
        hudTextures = true;
        JsonObject o = new JsonObject();
        o.add("ascii", dumpTexture(mc, "gui_ascii",
            (ResourceLocation)getf(mc.fontRenderer, "locationFontTexture")));
        o.add("widgets", dumpTexture(mc, "gui_widgets", new ResourceLocation("textures/gui/widgets.png")));
        o.add("icons", dumpTexture(mc, "gui_icons", new ResourceLocation("textures/gui/icons.png")));
        o.add("achievement", dumpTexture(mc, "gui_achievement",
            new ResourceLocation("textures/gui/achievement/achievement_background.png")));
        o.add("vignette", dumpTexture(mc, "gui_vignette", new ResourceLocation("textures/misc/vignette.png")));
        o.add("glint", dumpTexture(mc, "gui_glint", new ResourceLocation("textures/misc/enchanted_item_glint.png")));
        o.add("items", dumpAtlas(mc, "gui_items"));
        o.add("chest", dumpTexture(mc, "gui_chest", new ResourceLocation("textures/entity/chest/normal.png")));
        o.add("underwater", dumpTexture(mc, "gui_underwater", new ResourceLocation("textures/misc/underwater.png")));
        o.add("pumpkin", dumpTexture(mc, "gui_pumpkin", new ResourceLocation("textures/misc/pumpkinblur.png")));
        o.add("rain", dumpTexture(mc, "gui_rain", new ResourceLocation("textures/environment/rain.png")));
        o.add("snow", dumpTexture(mc, "gui_snow", new ResourceLocation("textures/environment/snow.png")));
        o.add("orb", dumpTexture(mc, "gui_orb", new ResourceLocation("textures/entity/experience_orb.png")));
        o.add("particles", dumpTexture(mc, "gui_particles", new ResourceLocation("textures/particle/particles.png")));
        o.add("explosion", dumpTexture(mc, "gui_explosion", new ResourceLocation("textures/entity/explosion.png")));
        // GuiWinGame's dirt background and logo, the other two chest item textures
        o.add("options_bg", dumpTexture(mc, "gui_options_bg", net.minecraft.client.gui.Gui.optionsBackground));
        o.add("title", dumpTexture(mc, "gui_title", new ResourceLocation("textures/gui/title/minecraft.png")));
        o.add("chest_ender", dumpTexture(mc, "gui_chest_ender", new ResourceLocation("textures/entity/chest/ender.png")));
        o.add("chest_trapped", dumpTexture(mc, "gui_chest_trapped", new ResourceLocation("textures/entity/chest/trapped.png")));
        ItemTable.dumpGui(dir);
        PrintWriter w = new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(new File(dir, "gui.json")), Charset.forName("UTF-8")));
        w.println(o.toString());
        w.close();
    }

    /** One whole bound texture: pixels, size and the GL filters it is sampled with. */
    static JsonObject dumpTexture(Minecraft mc, String name, ResourceLocation loc) throws Exception
    {
        mc.getTextureManager().bindTexture(loc);
        int w = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_WIDTH);
        int h = GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D, 0, GL11.GL_TEXTURE_HEIGHT);
        if (w <= 0 || h <= 0) throw new IllegalStateException("texture " + loc + " is " + w + "x" + h);
        JsonObject o = new JsonObject();
        o.addProperty("w", w);
        o.addProperty("h", h);
        o.addProperty("min", GL11.glGetTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER));
        o.addProperty("mag", GL11.glGetTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER));
        ByteBuffer px = BufferUtils.createByteBuffer(w * h * 4);
        GL11.glGetTexImage(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, px);
        FileOutputStream f = new FileOutputStream(new File(dir, name + ".rgba"));
        byte[] row = new byte[w * 4];
        px.position(0);
        for (int y = 0; y < h; ++y)
        {
            px.get(row);
            f.write(row);
        }
        f.close();
        return o;
    }

    /** The stitched item atlas plus every sprite rect the item icons need. */
    static JsonObject dumpAtlas(Minecraft mc, String name) throws Exception
    {
        TextureMap map = (TextureMap)mc.getTextureManager().getTexture(TextureMap.locationItemsTexture);
        JsonObject o = dumpTexture(mc, name, TextureMap.locationItemsTexture);
        Map uploaded = (Map)fld(map, "mapUploadedSprites").get(map);
        TreeSet<String> names = new TreeSet<String>();
        for (Object k : uploaded.keySet()) names.add((String)k);
        JsonArray arr = new JsonArray();
        for (String n : names)
        {
            TextureAtlasSprite sp = (TextureAtlasSprite)uploaded.get(n);
            JsonObject s = new JsonObject();
            s.addProperty("name", n);
            s.addProperty("x", sp.getOriginX());
            s.addProperty("y", sp.getOriginY());
            s.addProperty("w", sp.getIconWidth());
            s.addProperty("h", sp.getIconHeight());
            put(s, "minU", sp.getMinU());
            put(s, "maxU", sp.getMaxU());
            put(s, "minV", sp.getMinV());
            put(s, "maxV", sp.getMaxV());
            s.addProperty("rotated", (Boolean)fld(sp, "rotated").get(sp) ? 1 : 0);
            arr.add(s);
        }
        o.add("sprites", arr);
        return o;
    }

    /** The client Math.random stream at the start of the latest tick (Oracle.preTick). */
    static long tickMath;

    static void tickStart()
    {
        if (out == null) return;
        tickMath = Det.mathState(Det.CLIENT);
        ClientStateProbe.tickStart(Minecraft.getMinecraft());
        ParticleLog.tickStart(Minecraft.getMinecraft());
    }

    static boolean animFrames;

    /** The two ticked atlases in TextureManager's tick order (blocks, then items). */
    static TextureMap[] animMaps(Minecraft mc)
    {
        return new TextureMap[] {
            (TextureMap)mc.getTextureManager().getTexture(TextureMap.locationBlocksTexture),
            (TextureMap)mc.getTextureManager().getTexture(TextureMap.locationItemsTexture)};
    }

    /**
     * Every animated sprite's state at frame time, per atlas in
     * TextureMap.listAnimatedSprites order (the order updateAnimations ticks
     * them): the frame and tick counters, the clock's two dial doubles and the
     * compass's angle and delta; with the client world's spawn point, which
     * the compass reads. MeshProbe writes the same object into atlas.json for
     * the moment it reads the block atlas back.
     */
    static JsonObject animState(Minecraft mc) throws Exception
    {
        JsonObject o = new JsonObject();
        JsonArray maps = new JsonArray();
        for (TextureMap map : animMaps(mc))
        {
            JsonArray a = new JsonArray();
            for (Object s : (java.util.List)fld(map, "listAnimatedSprites").get(map))
            {
                TextureAtlasSprite sp = (TextureAtlasSprite)s;
                JsonObject j = new JsonObject();
                j.addProperty("n", sp.getIconName());
                j.addProperty("fc", (Integer)getf(sp, "frameCounter"));
                j.addProperty("tc", (Integer)getf(sp, "tickCounter"));
                if (sp instanceof net.minecraft.client.renderer.texture.TextureClock)
                {
                    put(j, "h", ((Double)getf(sp, "field_94239_h")).doubleValue());
                    put(j, "i", ((Double)getf(sp, "field_94240_i")).doubleValue());
                }
                if (sp instanceof net.minecraft.client.renderer.texture.TextureCompass)
                {
                    put(j, "a", ((net.minecraft.client.renderer.texture.TextureCompass)sp).currentAngle);
                    put(j, "d", ((net.minecraft.client.renderer.texture.TextureCompass)sp).angleDelta);
                }
                a.add(j);
            }
            maps.add(a);
        }
        o.add("maps", maps);
        if (mc.theWorld != null)
        {
            net.minecraft.util.ChunkCoordinates c = mc.theWorld.getSpawnPoint();
            JsonArray sp = new JsonArray();
            sp.add(new com.google.gson.JsonPrimitive(c.posX));
            sp.add(new com.google.gson.JsonPrimitive(c.posY));
            sp.add(new com.google.gson.JsonPrimitive(c.posZ));
            o.add("spawn", sp);
            o.addProperty("surface", mc.theWorld.provider.isSurfaceWorld() ? 1 : 0);
        }
        return o;
    }

    /**
     * Once per recording: every animated sprite's source frames, anim.rgba
     * (each frame's level-0 pixels as RGBA bytes, frame after frame, sprite
     * after sprite) and anim.json (per atlas in list order: the sprite's name,
     * kind, origin and size, its AnimationMetadataSection frame list and
     * default frame time, and each frame's byte offset in anim.rgba, -1 for an
     * index the frame list never names).
     */
    static void animFrames(Minecraft mc) throws Exception
    {
        if (animFrames) return;
        animFrames = true;
        java.io.BufferedOutputStream bin = new java.io.BufferedOutputStream(
            new FileOutputStream(new File(dir, "anim.rgba")));
        long off = 0;
        JsonArray maps = new JsonArray();
        for (TextureMap map : animMaps(mc))
        {
            JsonArray a = new JsonArray();
            for (Object s : (java.util.List)fld(map, "listAnimatedSprites").get(map))
            {
                TextureAtlasSprite sp = (TextureAtlasSprite)s;
                JsonObject j = new JsonObject();
                j.addProperty("n", sp.getIconName());
                j.addProperty("kind", sp instanceof net.minecraft.client.renderer.texture.TextureClock ? "clock"
                    : sp instanceof net.minecraft.client.renderer.texture.TextureCompass ? "compass" : "sprite");
                j.addProperty("x", sp.getOriginX());
                j.addProperty("y", sp.getOriginY());
                j.addProperty("w", sp.getIconWidth());
                j.addProperty("h", sp.getIconHeight());
                net.minecraft.client.resources.data.AnimationMetadataSection meta =
                    (net.minecraft.client.resources.data.AnimationMetadataSection)getf(sp, "animationMetadata");
                j.addProperty("frametime", meta.getFrameTime());
                JsonArray fl = new JsonArray();
                for (int i = 0; i < meta.getFrameCount(); ++i)
                {
                    JsonArray e = new JsonArray();
                    e.add(new com.google.gson.JsonPrimitive(meta.getFrameIndex(i)));
                    e.add(new com.google.gson.JsonPrimitive(meta.getFrameTimeSingle(i)));
                    fl.add(e);
                }
                j.add("frames", fl);
                JsonArray offs = new JsonArray();
                java.util.List data = (java.util.List)getf(sp, "framesTextureData");
                for (Object fr : data)
                {
                    int[][] levels = (int[][])fr;
                    if (levels == null || levels[0] == null) { offs.add(new com.google.gson.JsonPrimitive(-1)); continue; }
                    int[] px = levels[0];
                    if (px.length != sp.getIconWidth() * sp.getIconHeight())
                        throw new IllegalStateException("anim frame of " + sp.getIconName() + " has " + px.length + " pixels");
                    offs.add(new com.google.gson.JsonPrimitive(off));
                    for (int v : px)
                    {
                        bin.write(v >> 16 & 255);
                        bin.write(v >> 8 & 255);
                        bin.write(v & 255);
                        bin.write(v >>> 24);
                    }
                    off += (long)px.length * 4;
                }
                j.add("data", offs);
                a.add(j);
            }
            maps.add(a);
        }
        bin.close();
        JsonObject o = new JsonObject();
        o.add("maps", maps);
        PrintWriter w = new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(new File(dir, "anim.json")), Charset.forName("UTF-8")));
        w.println(o.toString());
        w.close();
    }

    static void mobTextures(Minecraft mc) throws Exception
    {
        if (mobTextures) return;
        mobTextures = true;
        String[][] paths = {
            {"pig", "textures/entity/pig/pig.png"},
            {"pig_saddle", "textures/entity/pig/pig_saddle.png"},
            {"cow", "textures/entity/cow/cow.png"},
            {"sheep", "textures/entity/sheep/sheep.png"},
            {"sheep_fur", "textures/entity/sheep/sheep_fur.png"},
            {"chicken", "textures/entity/chicken.png"},
            {"zombie", "textures/entity/zombie/zombie.png"},
            {"pigman", "textures/entity/zombie_pigman.png"},
            {"ghast", "textures/entity/ghast/ghast.png"},
            {"ghast_shooting", "textures/entity/ghast/ghast_shooting.png"},
            {"blaze", "textures/entity/blaze.png"},
            {"magmacube", "textures/entity/slime/magmacube.png"},
            {"villager", "textures/entity/villager/villager.png"},
            {"farmer", "textures/entity/villager/farmer.png"},
            {"librarian", "textures/entity/villager/librarian.png"},
            {"priest", "textures/entity/villager/priest.png"},
            {"smith", "textures/entity/villager/smith.png"},
            {"butcher", "textures/entity/villager/butcher.png"},
            {"iron_golem", "textures/entity/iron_golem.png"},
            {"squid", "textures/entity/squid.png"},
            {"bat", "textures/entity/bat.png"},
            {"mooshroom", "textures/entity/cow/mooshroom.png"},
            {"skeleton", "textures/entity/skeleton/skeleton.png"},
            {"wither_skeleton", "textures/entity/skeleton/wither_skeleton.png"},
            {"creeper", "textures/entity/creeper/creeper.png"},
            {"creeper_armor", "textures/entity/creeper/creeper_armor.png"},
            {"spider", "textures/entity/spider/spider.png"},
            {"cave_spider", "textures/entity/spider/cave_spider.png"},
            {"spider_eyes", "textures/entity/spider_eyes.png"},
            {"enderman", "textures/entity/enderman/enderman.png"},
            {"enderman_eyes", "textures/entity/enderman/enderman_eyes.png"},
            {"witch", "textures/entity/witch.png"},
            {"slime", "textures/entity/slime/slime.png"},
            {"silverfish", "textures/entity/silverfish.png"},
            {"dragon", "textures/entity/enderdragon/dragon.png"},
            {"dragon_eyes", "textures/entity/enderdragon/dragon_eyes.png"},
            {"dragon_exploding", "textures/entity/enderdragon/dragon_exploding.png"},
            {"endercrystal", "textures/entity/endercrystal/endercrystal.png"},
            {"endercrystal_beam", "textures/entity/endercrystal/endercrystal_beam.png"},
            {"shadow", "textures/misc/shadow.png"}
        };
        JsonObject info = new JsonObject();
        for (String[] pair : paths) info.add(pair[0], dumpTexture(mc, pair[0], new ResourceLocation(pair[1])));
        PrintWriter w = new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(new File(dir, "mobs.json")), Charset.forName("UTF-8")));
        w.println(info.toString());
        w.close();
    }

    static JsonArray mobs(WorldClient w, float pt)
    {
        JsonArray a = new JsonArray();
        for (Object obj : w.getLoadedEntityList())
        {
            if (!(obj instanceof EntityLivingBase)) continue;
            EntityLivingBase e = (EntityLivingBase)obj;
            JsonObject m = new JsonObject();
            m.addProperty("id", e.getEntityId());
            m.addProperty("class", e.getClass().getSimpleName());
            put(m, "x", e.lastTickPosX + (e.posX - e.lastTickPosX) * (double)pt);
            put(m, "y", e.lastTickPosY + (e.posY - e.lastTickPosY) * (double)pt);
            put(m, "z", e.lastTickPosZ + (e.posZ - e.lastTickPosZ) * (double)pt);
            put(m, "height", e.height);
            put(m, "width", e.width);
            put(m, "byaw", mobYaw(e.prevRenderYawOffset, e.renderYawOffset, pt));
            put(m, "hyaw", mobYaw(e.prevRotationYawHead, e.rotationYawHead, pt));
            put(m, "pitch", e.prevRotationPitch + (e.rotationPitch - e.prevRotationPitch) * pt);
            put(m, "limb", e.limbSwing - e.limbSwingAmount * (1.0f - pt));
            put(m, "limba", e.prevLimbSwingAmount + (e.limbSwingAmount - e.prevLimbSwingAmount) * pt);
            put(m, "swing", e.getSwingProgress(pt));
            m.addProperty("age", e.ticksExisted);
            m.addProperty("hurt", e.hurtTime);
            m.addProperty("death", e.deathTime);
            m.addProperty("child", e.isChild() ? 1 : 0);
            m.addProperty("brf", e.getBrightnessForRender(pt));
            put(m, "bright", e.getBrightness(pt));
            m.addProperty("invisible", e.isInvisible() ? 1 : 0);
            m.addProperty("burning", e.isBurning() ? 1 : 0);
            // the client copy's lead holder (the S1B type 1 it last took), -1 none
            if (e instanceof net.minecraft.entity.EntityLiving)
            {
                Entity holder = ((net.minecraft.entity.EntityLiving)e).getLeashedToEntity();
                m.addProperty("leash", holder != null ? holder.getEntityId() : -1);
            }
            if (e instanceof net.minecraft.entity.monster.EntitySkeleton)
                m.addProperty("skeletonType", ((net.minecraft.entity.monster.EntitySkeleton)e).getSkeletonType());
            if (e instanceof net.minecraft.entity.monster.EntityCreeper)
            {
                net.minecraft.entity.monster.EntityCreeper c = (net.minecraft.entity.monster.EntityCreeper)e;
                m.addProperty("charged", c.getPowered() ? 1 : 0);
                put(m, "flash", c.getCreeperFlashIntensity(pt));
            }
            if (e instanceof net.minecraft.entity.monster.EntityEnderman)
            {
                net.minecraft.entity.monster.EntityEnderman n = (net.minecraft.entity.monster.EntityEnderman)e;
                m.addProperty("carried", net.minecraft.block.Block.getIdFromBlock(n.func_146080_bZ()));
                m.addProperty("carriedMeta", n.getCarryingData());
                m.addProperty("screaming", n.isScreaming() ? 1 : 0);
            }
            if (e instanceof net.minecraft.entity.monster.EntitySlime)
            {
                net.minecraft.entity.monster.EntitySlime s = (net.minecraft.entity.monster.EntitySlime)e;
                m.addProperty("size", s.getSlimeSize());
                put(m, "squish", s.prevSquishFactor + (s.squishFactor - s.prevSquishFactor) * pt);
            }
            if (e instanceof EntitySheep)
            {
                EntitySheep s = (EntitySheep)e;
                m.addProperty("sheared", s.getSheared() ? 1 : 0);
                m.addProperty("color", s.getFleeceColor());
                put(m, "eatHeadY", s.func_70894_j(pt));
                put(m, "eatHeadX", s.func_70890_k(pt));
            }
            if (e instanceof EntityPig) m.addProperty("saddle", ((EntityPig)e).getSaddled() ? 1 : 0);
            if (e instanceof EntityChicken)
            {
                EntityChicken c = (EntityChicken)e;
                float flap = c.field_70888_h + (c.field_70886_e - c.field_70888_h) * pt;
                float power = c.field_70884_g + (c.destPos - c.field_70884_g) * pt;
                put(m, "wing", (MathHelper.sin(flap) + 1.0f) * power);
            }
            if (e instanceof net.minecraft.entity.passive.EntityVillager)
                m.addProperty("profession", ((net.minecraft.entity.passive.EntityVillager)e).getProfession());
            if (e instanceof net.minecraft.entity.passive.EntityBat)
                m.addProperty("hanging", ((net.minecraft.entity.passive.EntityBat)e).getIsBatHanging() ? 1 : 0);
            if (e instanceof net.minecraft.entity.monster.EntityGhast)
            {
                net.minecraft.entity.monster.EntityGhast g = (net.minecraft.entity.monster.EntityGhast)e;
                m.addProperty("shooting", g.func_110182_bF() ? 1 : 0);
                put(m, "attackProgress", (g.prevAttackCounter + (g.attackCounter - g.prevAttackCounter) * pt) / 20.0f);
            }
            if (e instanceof net.minecraft.entity.passive.EntitySquid)
            {
                net.minecraft.entity.passive.EntitySquid s = (net.minecraft.entity.passive.EntitySquid)e;
                put(m, "tentacle", s.lastTentacleAngle + (s.tentacleAngle - s.lastTentacleAngle) * pt);
                put(m, "squidPitch", s.prevSquidPitch + (s.squidPitch - s.prevSquidPitch) * pt);
                put(m, "squidYaw", s.prevSquidYaw + (s.squidYaw - s.prevSquidYaw) * pt);
            }
            if (e instanceof net.minecraft.entity.monster.EntityIronGolem)
            {
                net.minecraft.entity.monster.EntityIronGolem g = (net.minecraft.entity.monster.EntityIronGolem)e;
                m.addProperty("attackTimer", g.getAttackTimer());
                m.addProperty("roseTimer", g.getHoldRoseTick());
            }
            if (e instanceof net.minecraft.entity.boss.EntityDragon)
            {
                net.minecraft.entity.boss.EntityDragon dg =
                    (net.minecraft.entity.boss.EntityDragon)e;
                // ModelDragon.render's animTime (var9) and the 24 movement
                // offsets the neck, head and tail chains read (getMovementOffsets
                // applies the partial tick itself, so this is the render input).
                put(m, "anim", dg.prevAnimTime + (dg.animTime - dg.prevAnimTime) * pt);
                // RenderDragon.doRender's BossStatus.setBossStatus inputs
                put(m, "hp", dg.getHealth());
                put(m, "mhp", dg.getMaxHealth());
                m.addProperty("bossName", dg.func_145748_c_().getFormattedText());
                JsonArray offs = new JsonArray();
                for (int i = 0; i < 24; ++i)
                {
                    double[] o = dg.getMovementOffsets(i, pt);
                    JsonArray t = new JsonArray();
                    t.add(new com.google.gson.JsonPrimitive(d(o[0])));
                    t.add(new com.google.gson.JsonPrimitive(d(o[1])));
                    t.add(new com.google.gson.JsonPrimitive(d(o[2])));
                    offs.add(t);
                }
                m.add("offsets", offs);
                m.addProperty("deathTicks", dg.deathTicks);
                if (dg.healingEnderCrystal != null)
                {
                    // RenderDragon.doRender's beam inputs, in its own order.
                    net.minecraft.entity.item.EntityEnderCrystal c = dg.healingEnderCrystal;
                    float rot = (float)c.innerRotation + pt;
                    float dx = (float)(c.posX - dg.posX - (dg.prevPosX - dg.posX) * (double)(1.0f - pt));
                    float dy = (float)((double)(MathHelper.sin(rot * 0.2F) / 2.0F + 0.5F) + c.posY - 1.0D - dg.posY
                        - (dg.prevPosY - dg.posY) * (double)(1.0f - pt));
                    float dz = (float)(c.posZ - dg.posZ - (dg.prevPosZ - dg.posZ) * (double)(1.0f - pt));
                    JsonObject beam = new JsonObject();
                    put(beam, "rot", rot);
                    put(beam, "dx", dx);
                    put(beam, "dy", dy);
                    put(beam, "dz", dz);
                    put(beam, "len", MathHelper.sqrt_float(dx * dx + dy * dy + dz * dz));
                    beam.addProperty("ticks", dg.ticksExisted);
                    m.add("beam", beam);
                }
            }
            EntityRenderProbe.living(m, e);
            a.add(m);
        }
        return a;
    }

    /**
     * The End crystals in range. EntityEnderCrystal is not an EntityLivingBase,
     * so it is not in mobs(). RenderEnderCrystal reads innerRotation and the
     * interpolated position; the model's own inputs (var10, var11) follow from
     * innerRotation + partialTicks in ModelEnderCrystal.render.
     */
    static JsonArray crystals(WorldClient w, float pt)
    {
        JsonArray a = new JsonArray();
        for (Object obj : w.getLoadedEntityList())
        {
            if (!(obj instanceof net.minecraft.entity.item.EntityEnderCrystal)) continue;
            net.minecraft.entity.item.EntityEnderCrystal c =
                (net.minecraft.entity.item.EntityEnderCrystal)obj;
            JsonObject m = new JsonObject();
            m.addProperty("id", c.getEntityId());
            put(m, "x", c.lastTickPosX + (c.posX - c.lastTickPosX) * (double)pt);
            put(m, "y", c.lastTickPosY + (c.posY - c.lastTickPosY) * (double)pt);
            put(m, "z", c.lastTickPosZ + (c.posZ - c.lastTickPosZ) * (double)pt);
            put(m, "rot", (float)c.innerRotation + pt);
            m.addProperty("ticks", c.ticksExisted);
            m.addProperty("brf", c.getBrightnessForRender(pt));
            a.add(m);
        }
        return a;
    }

    /**
     * The particles this frame's renderParticles / renderLitParticles draw:
     * every EntityFX in mc.effectRenderer.fxLayers[0..3] at fog-hook time
     * (updateEffects has run for the tick, nothing has drawn yet). The dump
     * carries the interpolated draw position and the fields each layer's
     * renderParticle reads, raw bits for the floats and doubles so nothing
     * rounds on the way through JSON.
     */
    static JsonArray particles(Minecraft mc, float pt) throws Exception
    {
        Object er = mc.effectRenderer;
        JsonArray a = new JsonArray();
        if (er == null) return a;
        Object[] layers = (Object[])getf(er, "fxLayers");
        for (int layer = 0; layer < 4; ++layer)
        {
            java.util.List list = (java.util.List)layers[layer];
            for (int i = 0; i < list.size(); ++i)
            {
                net.minecraft.client.particle.EntityFX fx =
                    (net.minecraft.client.particle.EntityFX)list.get(i);
                JsonObject m = new JsonObject();
                m.addProperty("cls", fx.getClass().getSimpleName());
                m.addProperty("layer", layer);
                put(m, "px", fx.prevPosX);
                put(m, "py", fx.prevPosY);
                put(m, "pz", fx.prevPosZ);
                put(m, "x", fx.posX);
                put(m, "y", fx.posY);
                put(m, "z", fx.posZ);
                put(m, "mx", fx.motionX);
                put(m, "my", fx.motionY);
                put(m, "mz", fx.motionZ);
                m.addProperty("age", ((Integer)getf(fx, "particleAge")).intValue());
                m.addProperty("maxage", ((Integer)getf(fx, "particleMaxAge")).intValue());
                put(m, "scale", ((Float)getf(fx, "particleScale")).floatValue());
                put(m, "red", ((Float)getf(fx, "particleRed")).floatValue());
                put(m, "green", ((Float)getf(fx, "particleGreen")).floatValue());
                put(m, "blue", ((Float)getf(fx, "particleBlue")).floatValue());
                put(m, "alpha", ((Float)getf(fx, "particleAlpha")).floatValue());
                put(m, "jitx", ((Float)getf(fx, "particleTextureJitterX")).floatValue());
                put(m, "jity", ((Float)getf(fx, "particleTextureJitterY")).floatValue());
                m.addProperty("tix", ((Integer)getf(fx, "particleTextureIndexX")).intValue());
                m.addProperty("tiy", ((Integer)getf(fx, "particleTextureIndexY")).intValue());
                put(m, "gravity", ((Float)getf(fx, "particleGravity")).floatValue());
                m.addProperty("brf", fx.getBrightnessForRender(pt));
                IIcon ic = (IIcon)getf(fx, "particleIcon");
                if (ic != null)
                {
                    m.addProperty("icon", ic.getIconName());
                    put(m, "minU", ic.getMinU());
                    put(m, "maxU", ic.getMaxU());
                    put(m, "minV", ic.getMinV());
                    put(m, "maxV", ic.getMaxV());
                }
                if (fx instanceof net.minecraft.client.particle.EntityHugeExplodeFX)
                {
                    m.addProperty("tstart", (Integer)getf(fx, "timeSinceStart"));
                    m.addProperty("tmax", (Integer)getf(fx, "maximumTime"));
                }
                if (fx instanceof net.minecraft.client.particle.EntityLargeExplodeFX)
                {
                    put(m, "vx", (Number)getf(fx, "field_70581_a"));
                    put(m, "vq", (Integer)getf(fx, "field_70584_aq"));
                    put(m, "vs", (Float)getf(fx, "field_70582_as"));
                }
                if (fx instanceof net.minecraft.client.particle.EntitySmokeFX)
                    put(m, "smscale", (Float)getf(fx, "smokeParticleScale"));
                if (fx instanceof net.minecraft.client.particle.EntityPickupFX)
                    EntityRenderProbe.pickup(m, fx, pt);
                if (fx instanceof net.minecraft.client.particle.EntityFlameFX)
                    put(m, "fscale", (Float)getf(fx, "flameScale"));
                particleState(m, fx);
                a.add(m);
            }
        }
        return a;
    }

    /**
     * What the native particle tick needs to continue one EntityFX from this
     * dump: its own Random, the entity box moveEntity moves (the position
     * follows the box), the size and offsets, onGround and noClip, and each
     * subclass's own fields (the scales renderParticle animates, the portal's
     * home, the crit's colour decay base, the spell's texture base, the
     * EntityCrit2FX emitter's target and life).
     */
    static void particleState(JsonObject m, net.minecraft.client.particle.EntityFX fx) throws Exception
    {
        m.addProperty("rs", Rows.hex(Det.state((java.util.Random)getf(fx, "rand"))));
        AxisAlignedBB b = fx.boundingBox;
        JsonArray bb = new JsonArray();
        for (double v : new double[] {b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ})
            bb.add(new com.google.gson.JsonPrimitive(d(v)));
        m.add("bb", bb);
        put(m, "w", fx.width);
        put(m, "h", fx.height);
        put(m, "yo", fx.yOffset);
        put(m, "ys", fx.ySize);
        m.addProperty("og", fx.onGround ? 1 : 0);
        m.addProperty("nc", fx.noClip ? 1 : 0);
        m.addProperty("dead", fx.isDead ? 1 : 0);
        if (fx instanceof net.minecraft.client.particle.EntityReddustFX)
            put(m, "rdscale", ((Float)getf(fx, "reddustParticleScale")).floatValue());
        if (fx instanceof net.minecraft.client.particle.EntityPortalFX)
        {
            put(m, "pscale", ((Float)getf(fx, "portalParticleScale")).floatValue());
            put(m, "ppx", ((Double)getf(fx, "portalPosX")).doubleValue());
            put(m, "ppy", ((Double)getf(fx, "portalPosY")).doubleValue());
            put(m, "ppz", ((Double)getf(fx, "portalPosZ")).doubleValue());
        }
        if (fx instanceof net.minecraft.client.particle.EntityEnchantmentTableParticleFX)
        {
            put(m, "escale", ((Float)getf(fx, "field_70565_a")).floatValue());
            put(m, "ppx", ((Double)getf(fx, "field_70568_aq")).doubleValue());
            put(m, "ppy", ((Double)getf(fx, "field_70567_ar")).doubleValue());
            put(m, "ppz", ((Double)getf(fx, "field_70566_as")).doubleValue());
        }
        if (fx instanceof net.minecraft.client.particle.EntityCritFX)
            put(m, "cscale", ((Float)getf(fx, "initialParticleScale")).floatValue());
        if (fx instanceof net.minecraft.client.particle.EntitySpellParticleFX)
            m.addProperty("sbase", ((Integer)getf(fx, "baseSpellTextureIndex")).intValue());
        if (fx instanceof net.minecraft.client.particle.EntityLavaFX)
            put(m, "lscale", ((Float)getf(fx, "lavaParticleScale")).floatValue());
        if (fx instanceof net.minecraft.client.particle.EntityDropParticleFX)
        {
            m.addProperty("bob", ((Integer)getf(fx, "bobTimer")).intValue());
            m.addProperty("lava", getf(fx, "materialType") == net.minecraft.block.material.Material.lava ? 1 : 0);
        }
        if (fx instanceof net.minecraft.client.particle.EntityCrit2FX)
        {
            m.addProperty("ent", ((Entity)getf(fx, "theEntity")).getEntityId());
            m.addProperty("life", ((Integer)getf(fx, "currentLife")).intValue());
            m.addProperty("mlife", ((Integer)getf(fx, "maximumLife")).intValue());
            m.addProperty("pname", (String)getf(fx, "particleName"));
        }
    }

    /**
     * What EntityRenderer.renderHand and ItemRenderer.renderItemInFirstPerson
     * read at frame time: the equip animation's two ends, the item the
     * renderer holds (itemToRender, which lags the inventory while the
     * animation runs), the player's swing and arm lag fields, the item in use,
     * and the light at the player's block. The held item goes out with the
     * icon of each render pass as EntityPlayer.getItemIcon picks it (the bow's
     * pull stage) and the numbers the item paths branch on.
     */
    static JsonObject hand(Minecraft mc, EntityClientPlayerMP p, WorldClient w, float pt) throws Exception
    {
        net.minecraft.client.renderer.ItemRenderer ir = mc.entityRenderer.itemRenderer;
        JsonObject o = new JsonObject();
        put(o, "eq", (Float)getf(ir, "equippedProgress"));
        put(o, "peq", (Float)getf(ir, "prevEquippedProgress"));
        o.addProperty("slot", (Integer)getf(ir, "equippedItemSlot"));
        ItemStack s = (ItemStack)getf(ir, "itemToRender");
        o.add("item", handItem(p, s));
        put(o, "sw", p.swingProgress);
        put(o, "psw", p.prevSwingProgress);
        o.addProperty("swi", p.swingProgressInt);
        o.addProperty("swing", p.isSwingInProgress ? 1 : 0);
        put(o, "gsp", p.getSwingProgress(pt));
        put(o, "ap", p.renderArmPitch);
        put(o, "pap", p.prevRenderArmPitch);
        put(o, "ay", p.renderArmYaw);
        put(o, "pay", p.prevRenderArmYaw);
        o.addProperty("use", p.getItemInUseCount());
        o.addProperty("light", w.getLightBrightnessForSkyBlocks(MathHelper.floor_double(p.posX),
            MathHelper.floor_double(p.posY), MathHelper.floor_double(p.posZ), 0));
        o.addProperty("inv", p.isInvisible() ? 1 : 0);
        o.addProperty("esm", mc.playerController.enableEverythingIsScrewedUpMode() ? 1 : 0);
        o.addProperty("far", mc.gameSettings.renderDistanceChunks * 16);
        skinTexture(mc, p);
        return o;
    }

    static JsonObject handItem(EntityClientPlayerMP p, ItemStack s)
    {
        if (s == null) return null;
        Item it = s.getItem();
        JsonObject o = item(s);
        Block b = Block.getBlockFromItem(it);
        o.addProperty("action", s.getItemUseAction().ordinal());
        o.addProperty("maxuse", s.getMaxItemUseDuration());
        o.addProperty("rot", it.shouldRotateAroundWhenRendering() ? 1 : 0);
        o.addProperty("multi", it.requiresMultipleRenderPasses() ? 1 : 0);
        o.addProperty("cloth", it instanceof net.minecraft.item.ItemCloth ? 1 : 0);
        o.addProperty("map", it == net.minecraft.init.Items.filled_map ? 1 : 0);
        o.addProperty("eff", s.hasEffect() ? 1 : 0);
        if (b != null && b != net.minecraft.init.Blocks.air)
        {
            o.addProperty("rc", b.getRenderColor(s.getItemDamage()));
            o.addProperty("bpass", b.getRenderBlockPass());
            o.addProperty("rt", b.getRenderType());
            o.add("bounds", EntityRenderProbe.bounds(b));
        }
        JsonArray passes = new JsonArray();
        int n = it.requiresMultipleRenderPasses() ? 2 : 1;
        for (int pass = 0; pass < n; ++pass)
        {
            JsonObject q = new JsonObject();
            q.addProperty("tint", it.getColorFromItemStack(s, pass));
            IIcon ic = p.getItemIcon(s, pass);
            if (ic != null)
            {
                q.addProperty("icon", ic.getIconName());
                q.addProperty("iw", ic.getIconWidth());
                q.addProperty("ih", ic.getIconHeight());
                put(q, "minU", ic.getMinU());
                put(q, "maxU", ic.getMaxU());
                put(q, "minV", ic.getMinV());
                put(q, "maxV", ic.getMaxV());
            }
            passes.add(q);
        }
        o.add("passes", passes);
        return o;
    }

    static boolean skinTexture;

    /** The player's skin as GL holds it (the arm's texture) and the water overlay renderOverlays draws. */
    static void skinTexture(Minecraft mc, EntityClientPlayerMP p) throws Exception
    {
        if (skinTexture) return;
        skinTexture = true;
        JsonObject info = new JsonObject();
        info.add("skin", dumpTexture(mc, "skin", p.getLocationSkin()));
        info.add("underwater", dumpTexture(mc, "underwater", new ResourceLocation("textures/misc/underwater.png")));
        PrintWriter w = new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(new File(dir, "hand.json")), Charset.forName("UTF-8")));
        w.println(info.toString());
        w.close();
    }

    static float mobYaw(float from, float to, float pt)
    {
        float delta = to - from;
        while (delta < -180.0f) delta += 360.0f;
        while (delta >= 180.0f) delta -= 360.0f;
        return from + pt * delta;
    }

    // ------------------------------------------------------------------ hooks

    /** Hook 1: after updateFogColor. */
    public static void fog(Object er, float pt)
    {
        if (out == null) return;
        try
        {
            doFog(er, pt);
        }
        catch (Throwable t)
        {
            t.printStackTrace();
            out.close();
            out = null;
        }
    }

    /** Hook 2: after setupCameraTransform. */
    public static void camera(Object er, float pt)
    {
        if (row == null) return;
        try
        {
            doCamera(er, pt);
        }
        catch (Throwable t)
        {
            t.printStackTrace();
            out.close();
            out = null;
            row = null;
        }
    }

    /** Hook 3: after the prepareterrain setupFog(0). */
    public static void fogGL()
    {
        if (row == null) return;
        try
        {
            doFogGL();
        }
        catch (Throwable t)
        {
            t.printStackTrace();
            out.close();
            out = null;
            row = null;
        }
    }

    // ------------------------------------------------------------------ parts

    static void doFog(Object er, float pt)
    {
        Minecraft mc = (Minecraft)getf(er, "mc");
        WorldClient w = mc.theWorld;
        EntityLivingBase v = mc.renderViewEntity;
        EntityClientPlayerMP p = mc.thePlayer;

        row = new JsonObject();
        row.addProperty("t", Rows.pending != null ? Rows.pending.get("t").getAsInt() : (int)Oracle.tick - 1);
        row.addProperty("pt", pt);

        // options the frame's numbers depend on
        JsonObject opt = new JsonObject();
        opt.addProperty("rd", mc.gameSettings.renderDistanceChunks);
        put(opt, "fov", mc.gameSettings.fovSetting);
        put(opt, "gamma", mc.gameSettings.gammaSetting);
        opt.addProperty("bob", mc.gameSettings.viewBobbing ? 1 : 0);
        opt.addProperty("clouds", mc.gameSettings.shouldRenderClouds() ? 1 : 0);
        opt.addProperty("tpv", mc.gameSettings.thirdPersonView);
        opt.addProperty("ana", mc.gameSettings.anaglyph ? 1 : 0);
        opt.addProperty("dbgcam", mc.gameSettings.debugCamEnable ? 1 : 0);
        opt.addProperty("fancy", mc.gameSettings.fancyGraphics ? 1 : 0);
        row.add("opt", opt);

        JsonArray disp = new JsonArray();
        disp.add(new com.google.gson.JsonPrimitive(mc.displayWidth));
        disp.add(new com.google.gson.JsonPrimitive(mc.displayHeight));
        row.add("disp", disp);

        // the entity renderer's own state, before this frame's draws consumed it
        JsonObject r = new JsonObject();
        put(r, "tfx", (Float)getf(er, "torchFlickerX"));
        put(r, "tfy", (Float)getf(er, "torchFlickerY"));
        put(r, "tfdx", ((Float)getf(er, "torchFlickerDX")).floatValue());
        put(r, "tfdy", ((Float)getf(er, "torchFlickerDY")).floatValue());
        r.addProperty("cm0", Rows.hex(tickMath));
        put(r, "fc1", (Float)getf(er, "fogColor1"));
        put(r, "fc2", (Float)getf(er, "fogColor2"));
        put(r, "fmh", (Float)getf(er, "fovModifierHand"));
        put(r, "fmhp", (Float)getf(er, "fovModifierHandPrev"));
        r.addProperty("ruc", (Integer)getf(er, "rendererUpdateCount"));
        put(r, "roll", (Float)getf(er, "camRoll"));
        put(r, "proll", (Float)getf(er, "prevCamRoll"));
        put(r, "tpd", (Float)getf(er, "thirdPersonDistance"));
        put(r, "tpdt", (Float)getf(er, "thirdPersonDistanceTemp"));
        put(r, "zoom", (Double)getf(er, "cameraZoom"));
        put(r, "cyaw", (Double)getf(er, "cameraYaw"));
        put(r, "cpit", (Double)getf(er, "cameraPitch"));
        put(r, "boss", (Float)getf(er, "bossColorModifier"));
        put(r, "bossp", (Float)getf(er, "bossColorModifierPrev"));
        r.addProperty("dvd", (Integer)getf(er, "debugViewDirection"));
        r.addProperty("clf", (Boolean)getf(er, "cloudFog") ? 1 : 0);
        row.add("er", r);

        // the render view entity's state at frame time
        JsonObject pl = new JsonObject();
        put(pl, "px", v.prevPosX);
        put(pl, "py", v.prevPosY);
        put(pl, "pz", v.prevPosZ);
        put(pl, "x", v.posX);
        put(pl, "y", v.posY);
        put(pl, "z", v.posZ);
        put(pl, "yaw", v.rotationYaw);
        put(pl, "pyaw", v.prevRotationYaw);
        put(pl, "pit", v.rotationPitch);
        put(pl, "ppit", v.prevRotationPitch);
        put(pl, "yoff", v.yOffset);
        put(pl, "dwm", v.distanceWalkedModified);
        put(pl, "pdwm", v.prevDistanceWalkedModified);
        put(pl, "cyaw", p.cameraYaw);
        put(pl, "pcyaw", p.prevCameraYaw);
        put(pl, "cpit", p.cameraPitch);
        put(pl, "pcpit", p.prevCameraPitch);
        pl.addProperty("hurt", v.hurtTime);
        pl.addProperty("mhurt", v.maxHurtTime);
        pl.addProperty("death", v.deathTime);
        put(pl, "aaty", v.attackedAtYaw);
        put(pl, "hp", v.getHealth());
        put(pl, "portal", p.timeInPortal);
        put(pl, "pportal", p.prevTimeInPortal);
        pl.addProperty("burning", p.isBurning() ? 1 : 0);
        pl.addProperty("underwater", p.isInsideOfMaterial(net.minecraft.block.material.Material.water) ? 1 : 0);
        pl.addProperty("opaque", p.isEntityInsideOpaqueBlock() ? 1 : 0);
        pl.addProperty("pumpkin", p.inventory.armorItemInSlot(3) != null &&
            p.inventory.armorItemInSlot(3).getItem() == Item.getItemFromBlock(net.minecraft.init.Blocks.pumpkin) ? 1 : 0);
        put(pl, "brightness", p.getBrightness(pt));
        pl.addProperty("sleep", v.isPlayerSleeping() ? 1 : 0);
        pl.addProperty("crea", p.capabilities.isCreativeMode ? 1 : 0);
        pl.addProperty("brf", v.getBrightnessForRender(pt));
        pl.addProperty("resp", net.minecraft.enchantment.EnchantmentHelper.getRespiration(v));
        pl.addProperty("nv", p.isPotionActive(Potion.nightVision) ? 1 : 0);
        pl.addProperty("nvd", p.isPotionActive(Potion.nightVision) ? p.getActivePotionEffect(Potion.nightVision).getDuration() : 0);
        pl.addProperty("blind", p.isPotionActive(Potion.blindness) ? 1 : 0);
        pl.addProperty("bldur", p.isPotionActive(Potion.blindness) ? p.getActivePotionEffect(Potion.blindness).getDuration() : 0);
        pl.addProperty("wb", p.isPotionActive(Potion.waterBreathing) ? 1 : 0);
        pl.addProperty("conf", p.isPotionActive(Potion.confusion) ? 1 : 0);
        row.add("pl", pl);
        try { row.add("anim", animState(mc)); animFrames(mc); } catch (Exception ex) { throw new RuntimeException(ex); }
        row.add("mobs", mobs(w, pt));
        row.add("crystals", crystals(w, pt));
        row.add("ents", EntityRenderProbe.ents(mc, w, pt));
        try { EntityRenderProbe.textures(mc); } catch (Exception ex) { throw new RuntimeException(ex); }
        try { mobTextures(mc); } catch (Exception ex) { throw new RuntimeException(ex); }
        try { row.add("hand", hand(mc, p, w, pt)); } catch (Exception ex) { throw new RuntimeException(ex); }
        try { row.add("fx", particles(mc, pt)); } catch (Exception ex) { throw new RuntimeException(ex); }
        row.add("spawns", ParticleLog.log);
        row.add("cs", ClientStateProbe.state(mc, p, er, w));
        if (ParticleLog.tick0 != null) row.add("tick0", ParticleLog.tick0);

        // The two 3D presentation passes after terrain. Read the objects the
        // renderer actually uses, including the stored damage stage.
        JsonObject overlay = new JsonObject();
        MovingObjectPosition hit = mc.objectMouseOver;
        if (!mc.gameSettings.hideGUI && hit != null &&
            hit.typeOfHit == MovingObjectPosition.MovingObjectType.BLOCK)
        {
            Block selected = w.getBlock(hit.blockX, hit.blockY, hit.blockZ);
            if (selected.getMaterial() != net.minecraft.block.material.Material.air)
            {
                selected.setBlockBoundsBasedOnState(w, hit.blockX, hit.blockY, hit.blockZ);
                AxisAlignedBB box = selected.getSelectedBoundingBoxFromPool(w, hit.blockX, hit.blockY, hit.blockZ);
                JsonArray a = new JsonArray();
                a.add(new com.google.gson.JsonPrimitive(hit.blockX));
                a.add(new com.google.gson.JsonPrimitive(hit.blockY));
                a.add(new com.google.gson.JsonPrimitive(hit.blockZ));
                a.add(new com.google.gson.JsonPrimitive(d(box.minX)));
                a.add(new com.google.gson.JsonPrimitive(d(box.minY)));
                a.add(new com.google.gson.JsonPrimitive(d(box.minZ)));
                a.add(new com.google.gson.JsonPrimitive(d(box.maxX)));
                a.add(new com.google.gson.JsonPrimitive(d(box.maxY)));
                a.add(new com.google.gson.JsonPrimitive(d(box.maxZ)));
                overlay.add("selection", a);
            }
        }
        Object progress = ((Map)getf(mc.renderGlobal, "damagedBlocks")).get(Integer.valueOf(p.getEntityId()));
        if (progress instanceof DestroyBlockProgress)
        {
            DestroyBlockProgress d = (DestroyBlockProgress)progress;
            JsonArray a = new JsonArray();
            a.add(new com.google.gson.JsonPrimitive(d.getPartialBlockX()));
            a.add(new com.google.gson.JsonPrimitive(d.getPartialBlockY()));
            a.add(new com.google.gson.JsonPrimitive(d.getPartialBlockZ()));
            a.add(new com.google.gson.JsonPrimitive(d.getPartialBlockDamage()));
            overlay.add("damage", a);
        }
        row.add("overlay", overlay);

        // the world's time and weather, the provider, the biome at the viewpoint
        JsonObject o = new JsonObject();
        o.addProperty("wt", w.getWorldInfo().getWorldTime());
        put(o, "rain", (Float)getf(w, "rainingStrength"));
        put(o, "prain", (Float)getf(w, "prevRainingStrength"));
        put(o, "thu", (Float)getf(w, "thunderingStrength"));
        put(o, "pthu", (Float)getf(w, "prevThunderingStrength"));
        r.addProperty("lbolt", w.lastLightningBolt);
        o.addProperty("cloud", (Long)getf(w, "cloudColour"));
        o.addProperty("cloudTick", (Integer)getf(mc.renderGlobal, "cloudTickCounter"));
        o.addProperty("dim", w.provider.dimensionId);
        o.addProperty("nosky", w.provider.hasNoSky ? 1 : 0);
        o.addProperty("voidp", w.provider.getWorldHasVoidParticles() ? 1 : 0);
        put(o, "voidf", w.provider.getVoidFogYFactor());
        o.addProperty("xzfog", w.provider.doesXZShowFog((int)v.posX, (int)v.posZ) ? 1 : 0);
        int bx = MathHelper.floor_double(v.posX), bz = MathHelper.floor_double(v.posZ);
        int by = MathHelper.floor_double(v.posY);
        if (by < 0) by = 0;
        BiomeGenBase b = w.getBiomeGenForCoords(bx, bz);
        o.addProperty("biome", b.biomeID);
        put(o, "temp", b.getFloatTemperature(bx, by, bz));
        o.addProperty("skytemp", b.getSkyColorByTemp(b.getFloatTemperature(bx, by, bz)));
        JsonArray lbt = new JsonArray();
        for (int i = 0; i < 16; ++i) lbt.add(new com.google.gson.JsonPrimitive(f(w.provider.lightBrightnessTable[i])));
        o.add("lbt", lbt);
        row.add("wo", o);

        // the block at the entity viewpoint, the way updateFogColor and setupFog see it
        JsonObject vp = new JsonObject();
        Block blk = net.minecraft.client.renderer.ActiveRenderInfo.getBlockAtEntityViewpoint(w, v, pt);
        vp.addProperty("bid", Block.getIdFromBlock(blk));
        vp.addProperty("mat", blk.getMaterial() == net.minecraft.block.material.Material.water ? 1
            : (blk.getMaterial() == net.minecraft.block.material.Material.lava ? 2 : 0));
        row.add("vp", vp);

        // the RENDER role's Det state
        JsonObject rs = new JsonObject();
        rs.addProperty("seed", String.format("%016x", Det.seederState(Det.RENDER)));
        rs.addProperty("math", String.format("%016x", Det.mathState(Det.RENDER)));
        rs.addProperty("split", String.format("%016x", Det.splitState(Det.RENDER)));
        rs.addProperty("nid", Det.nextId[Det.RENDER]);
        row.add("rs", rs);

        // the world getters this frame's pixels come from
        JsonObject g = new JsonObject();
        float ang = w.getCelestialAngle(pt);
        put(g, "ang", ang);
        put(g, "angr", w.getCelestialAngleRadians(pt));
        put(g, "sun", w.getSunBrightness(pt));
        put(g, "star", w.getStarBrightness(pt));
        JsonArray sky = new JsonArray();
        net.minecraft.util.Vec3 s = w.getSkyColor(v, pt);
        sky.add(new com.google.gson.JsonPrimitive(d(s.xCoord)));
        sky.add(new com.google.gson.JsonPrimitive(d(s.yCoord)));
        sky.add(new com.google.gson.JsonPrimitive(d(s.zCoord)));
        g.add("sky", sky);
        JsonArray fog = new JsonArray();
        net.minecraft.util.Vec3 fc = w.getFogColor(pt);
        fog.add(new com.google.gson.JsonPrimitive(d(fc.xCoord)));
        fog.add(new com.google.gson.JsonPrimitive(d(fc.yCoord)));
        fog.add(new com.google.gson.JsonPrimitive(d(fc.zCoord)));
        g.add("fog", fog);
        JsonArray cl = new JsonArray();
        net.minecraft.util.Vec3 cc = w.getCloudColour(pt);
        cl.add(new com.google.gson.JsonPrimitive(d(cc.xCoord)));
        cl.add(new com.google.gson.JsonPrimitive(d(cc.yCoord)));
        cl.add(new com.google.gson.JsonPrimitive(d(cc.zCoord)));
        g.add("cloud", cl);
        float[] rise = w.provider.calcSunriseSunsetColors(ang, pt);
        if (rise == null) g.add("rise", (JsonObject)null);
        else
        {
            JsonArray ra = new JsonArray();
            for (int i = 0; i < 4; ++i) ra.add(new com.google.gson.JsonPrimitive(f(rise[i])));
            g.add("rise", ra);
        }
        row.add("g", g);

        // the 2D overlay's inputs; the HUD pass draws them after the world
        try
        {
            row.add("hud", hud(mc, p, pt));
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }

        // what updateFogColor left in the fields, and the lightmap as it stands
        JsonObject outp = new JsonObject();
        put(outp, "fcr", (Float)getf(er, "fogColorRed"));
        put(outp, "fcg", (Float)getf(er, "fogColorGreen"));
        put(outp, "fcb", (Float)getf(er, "fogColorBlue"));
        put(outp, "far", (Float)getf(er, "farPlaneDistance"));
        int[] lm = (int[])getf(er, "lightmapColors");
        JsonArray la = new JsonArray();
        for (int i = 0; i < 256; ++i) la.add(new com.google.gson.JsonPrimitive(lm[i]));
        outp.add("lm", la);
        row.add("out", outp);
    }

    static void doCamera(Object er, float pt)
    {
        ClientStateProbe.renderersBefore((Minecraft)getf(er, "mc"));
        fbuf16.clear();
        GL11.glGetFloat(GL11.GL_PROJECTION_MATRIX, fbuf16);
        float[] proj = new float[16];
        for (int i = 0; i < 16; ++i) proj[i] = fbuf16.get(i);
        fbuf16.clear();
        GL11.glGetFloat(GL11.GL_MODELVIEW_MATRIX, fbuf16);
        float[] mv = new float[16];
        for (int i = 0; i < 16; ++i) mv[i] = fbuf16.get(i);

        JsonArray pa = new JsonArray();
        JsonArray ma = new JsonArray();
        for (int i = 0; i < 16; ++i)
        {
            pa.add(new com.google.gson.JsonPrimitive(f(proj[i])));
            ma.add(new com.google.gson.JsonPrimitive(f(mv[i])));
        }

        // the camera position, orientCamera's arithmetic for first person
        Minecraft mc = (Minecraft)getf(er, "mc");
        EntityLivingBase v = mc.renderViewEntity;
        float yoff = v.yOffset - 1.62F;
        JsonObject cam = new JsonObject();
        put(cam, "x", v.prevPosX + (v.posX - v.prevPosX) * (double)pt);
        put(cam, "y", v.prevPosY + (v.posY - v.prevPosY) * (double)pt - (double)yoff);
        put(cam, "z", v.prevPosZ + (v.posZ - v.prevPosZ) * (double)pt);

        JsonObject outp = row.getAsJsonObject("out");
        outp.add("proj", pa);
        outp.add("mv", ma);
        outp.add("cam", cam);
    }

    static void doFogGL()
    {
        // LWJGL 2's glGetFloat wants a 16-element buffer whatever the query returns.
        fbuf16.clear();
        GL11.glGetFloat(GL11.GL_FOG_COLOR, fbuf16);
        float[] c = new float[4];
        for (int i = 0; i < 4; ++i) c[i] = fbuf16.get(i);
        fbuf16.clear();
        GL11.glGetFloat(GL11.GL_FOG_START, fbuf16);
        float start = fbuf16.get(0);
        fbuf16.clear();
        GL11.glGetFloat(GL11.GL_FOG_END, fbuf16);
        float end = fbuf16.get(0);
        fbuf16.clear();
        GL11.glGetFloat(GL11.GL_FOG_DENSITY, fbuf16);
        float den = fbuf16.get(0);
        ibuf16.clear();
        GL11.glGetInteger(GL11.GL_FOG_MODE, ibuf16);
        int mode = ibuf16.get(0);

        JsonObject gf = new JsonObject();
        JsonArray ca = new JsonArray();
        for (int i = 0; i < 4; ++i) ca.add(new com.google.gson.JsonPrimitive(f(c[i])));
        gf.add("c", ca);
        put(gf, "s", start);
        put(gf, "e", end);
        put(gf, "d", den);
        gf.addProperty("m", mode);
        row.getAsJsonObject("out").add("glfog", gf);
        row.add("inside", ClientStateProbe.inside(Minecraft.getMinecraft()));
        try
        {
            TileEntityRenderProbe.textures(Minecraft.getMinecraft(), dir);
            row.add("tes", TileEntityRenderProbe.tes(Minecraft.getMinecraft(), row.get("pt").getAsFloat()));
        }
        catch (Exception ex) { throw new RuntimeException(ex); }

        out.println(row.toString());
        out.flush();
        ++frames;
        row = null;
    }
}
