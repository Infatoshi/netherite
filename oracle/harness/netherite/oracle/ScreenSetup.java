package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGameOver;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.ScaledResolution;
import net.minecraft.client.gui.inventory.GuiChest;
import net.minecraft.client.gui.inventory.GuiContainer;
import net.minecraft.client.gui.inventory.GuiCrafting;
import net.minecraft.client.gui.inventory.GuiFurnace;
import net.minecraft.client.gui.inventory.GuiInventory;
import net.minecraft.client.gui.GuiMerchant;
import net.minecraft.entity.IMerchant;
import net.minecraft.entity.NpcMerchant;
import net.minecraft.entity.player.InventoryPlayer;
import net.minecraft.inventory.ContainerMerchant;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.inventory.Slot;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.util.StatCollector;
import net.minecraft.village.MerchantRecipe;
import net.minecraft.village.MerchantRecipeList;
import org.lwjgl.input.Mouse;

/** Stages live GuiScreen objects for render goldens without changing the world. */
public final class ScreenSetup
{
    /* it opens a screen and fills its slots on the client, which the frames
     * show: a replay re-runs it at its tick (Oracle.mutates) */
    public static final boolean MUTATES = true;
    private ScreenSetup() {}

    private static ItemStack stack(JsonObject s)
    {
        if (s == null) return null;
        Item item = Item.getItemById(s.get("id").getAsInt());
        if (item == null) throw new IllegalArgumentException("ScreenSetup item " + s.get("id"));
        return new ItemStack(item, s.has("count") ? s.get("count").getAsInt() : 1,
            s.has("meta") ? s.get("meta").getAsInt() : 0);
    }

    public static JsonObject run(IntegratedServer server, JsonObject cmd)
    {
        Minecraft mc = Minecraft.getMinecraft();
        String kind = cmd.get("screen").getAsString();
        if (cmd.has("inventory"))
            for (JsonElement e : cmd.getAsJsonArray("inventory"))
            {
                JsonObject s = e.getAsJsonObject();
                mc.thePlayer.inventory.mainInventory[s.get("slot").getAsInt()] = stack(s);
            }

        GuiScreen screen;
        if ("inventory".equals(kind)) screen = new GuiInventory(mc.thePlayer);
        else if ("crafting".equals(kind))
            screen = new GuiCrafting(mc.thePlayer.inventory, mc.theWorld,
                (int)Math.floor(mc.thePlayer.posX), (int)Math.floor(mc.thePlayer.posY),
                (int)Math.floor(mc.thePlayer.posZ));
        else if ("furnace".equals(kind))
        {
            TileEntityFurnace furnace = new TileEntityFurnace();
            furnace.field_145956_a = cmd.has("burn") ? cmd.get("burn").getAsInt() : 0;
            furnace.field_145963_i = cmd.has("fuel") ? cmd.get("fuel").getAsInt() : 200;
            furnace.field_145961_j = cmd.has("cook") ? cmd.get("cook").getAsInt() : 0;
            screen = new GuiFurnace(mc.thePlayer.inventory, furnace);
        }
        else if ("chest".equals(kind) || "double_chest".equals(kind))
        {
            int size = "chest".equals(kind) ? 27 : 54;
            screen = new GuiChest(mc.thePlayer.inventory,
                new InventoryBasic(StatCollector.translateToLocal(size == 27 ?
                    "container.chest" : "container.chestDouble"), true, size));
        }
        else if ("merchant".equals(kind))
        {
            /* NpcMerchant over the player, the offers from the cmd's
             * "recipes" (each {buy:{id,count,meta},sell:..,buyB?,uses?,
             * maxUses?,disabled?}), the field_147041_z from "trsel". The
             * client's list is what the screen reads; useRecipe's server
             * half never runs here. */
            NpcMerchant merchant = new NpcMerchant(mc.thePlayer);
            MerchantRecipeList list = new MerchantRecipeList();
            if (cmd.has("recipes"))
                for (JsonElement e : cmd.getAsJsonArray("recipes"))
                {
                    JsonObject r = e.getAsJsonObject();
                    ItemStack buy = stack(r.getAsJsonObject("buy"));
                    ItemStack sell = stack(r.getAsJsonObject("sell"));
                    ItemStack buyB = r.has("buyB") ? stack(r.getAsJsonObject("buyB")) : null;
                    MerchantRecipe m = new MerchantRecipe(buy, buyB, sell);
                    if (r.has("uses") && r.has("maxUses"))
                    {
                        /* no public setters: pin the pair through func_82785_h
                         * (uses = maxUses) and the remaining uses on top */
                        m.func_82785_h();
                        int extra = r.get("maxUses").getAsInt() - r.get("uses").getAsInt();
                        if (extra > 0) m.func_82783_a(extra);
                    }
                    if (r.has("disabled") && r.get("disabled").getAsBoolean()) m.func_82785_h();
                    list.add(m);
                }
            merchant.setRecipes(list);
            /* the S2D title a named villager sends: GuiMerchant draws it
             * instead of "Villager" (field_147040_A) */
            String name = cmd.has("name") ? cmd.get("name").getAsString() : null;
            screen = new GuiMerchant(mc.thePlayer.inventory, merchant, mc.theWorld, name);
            mc.displayGuiScreen(screen);
            if (cmd.has("trsel"))
            {
                try
                {
                    java.lang.reflect.Field f = GuiMerchant.class.getDeclaredField("field_147041_z");
                    f.setAccessible(true);
                    f.setInt(screen, cmd.get("trsel").getAsInt());
                }
                catch (Exception ex) { throw new RuntimeException(ex); }
                ((ContainerMerchant)((GuiContainer)screen).field_147002_h).setCurrentRecipeIndex(cmd.get("trsel").getAsInt());
            }
        }
        else if ("death".equals(kind)) screen = new GuiGameOver();
        else if ("credits".equals(kind)) screen = new net.minecraft.client.gui.GuiWinGame();
        else throw new IllegalArgumentException("ScreenSetup screen " + kind);

        mc.displayGuiScreen(screen);
        if (screen instanceof GuiContainer)
        {
            GuiContainer gui = (GuiContainer)screen;
            if (cmd.has("slots"))
                for (JsonElement e : cmd.getAsJsonArray("slots"))
                {
                    JsonObject s = e.getAsJsonObject();
                    ((Slot)gui.field_147002_h.inventorySlots.get(s.get("slot").getAsInt())).putStack(stack(s));
                }
            if (cmd.has("cursor")) mc.thePlayer.inventory.setItemStack(stack(cmd.getAsJsonObject("cursor")));
        }
        ScaledResolution scaled = new ScaledResolution(mc, mc.displayWidth, mc.displayHeight);
        int mx = cmd.has("mouseX") ? cmd.get("mouseX").getAsInt() : 0;
        int my = cmd.has("mouseY") ? cmd.get("mouseY").getAsInt() : 0;
        if (cmd.has("hover") && screen instanceof GuiContainer)
        {
            GuiContainer gui = (GuiContainer)screen;
            Slot slot = (Slot)gui.field_147002_h.inventorySlots.get(cmd.get("hover").getAsInt());
            mx = (scaled.getScaledWidth() - 176) / 2 + slot.xDisplayPosition + 8;
            int height = "chest".equals(kind) ? 168 : "double_chest".equals(kind) ? 222 : 166;
            my = (scaled.getScaledHeight() - height) / 2 + slot.yDisplayPosition + 8;
        }
        Mouse.setCursorPosition(mx * mc.displayWidth / scaled.getScaledWidth() + 1,
            mc.displayHeight - my * mc.displayHeight / scaled.getScaledHeight() - 1);
        JsonObject result = new JsonObject();
        result.addProperty("screen", kind);
        result.addProperty("mouseX", mx);
        result.addProperty("mouseY", my);
        return result;
    }

    /** What GuiInventory.func_147046_a's RenderPlayer reads of the player besides
     * the rotations the preview sets itself. */
    static JsonObject preview(net.minecraft.client.entity.EntityClientPlayerMP p, float pt)
    {
        JsonObject o = new JsonObject();
        RenderStateProbe.put(o, "limb", p.limbSwing);
        RenderStateProbe.put(o, "limba", p.limbSwingAmount);
        RenderStateProbe.put(o, "plimba", p.prevLimbSwingAmount);
        RenderStateProbe.put(o, "swing", p.getSwingProgress(pt));
        RenderStateProbe.put(o, "yoff", p.yOffset);
        RenderStateProbe.put(o, "bright", p.getBrightness(pt));
        // RendererLivingEntity interpolates from these at partial tick 1 while
        // the preview's own values replace the current ones
        RenderStateProbe.put(o, "pbyaw", p.prevRenderYawOffset);
        RenderStateProbe.put(o, "ppitch", p.prevRotationPitch);
        o.addProperty("age", p.ticksExisted);
        o.addProperty("hurt", p.hurtTime);
        o.addProperty("death", p.deathTime);
        o.addProperty("sneak", p.isSneaking() ? 1 : 0);
        o.addProperty("invis", p.isInvisible() ? 1 : 0);
        o.addProperty("ride", p.isRiding() ? 1 : 0);
        o.addProperty("arrows", p.getArrowCountInEntity());
        o.addProperty("fire", p.isBurning() ? 1 : 0);
        o.addProperty("use", p.getItemInUseCount());
        o.add("held", RenderStateProbe.item(p.getHeldItem()));
        o.add("armor", RenderStateProbe.inventory(p.inventory.armorInventory, 4));
        return o;
    }

    static JsonObject capture(Minecraft mc, float pt)
    {
        GuiScreen screen = mc.currentScreen;
        if (screen == null) return null;
        JsonObject out = new JsonObject();
        String kind;
        if (screen instanceof GuiInventory) kind = "inventory";
        else if (screen instanceof GuiCrafting) kind = "crafting";
        else if (screen instanceof GuiFurnace) kind = "furnace";
        else if (screen instanceof GuiChest)
        {
            GuiContainer gui = (GuiContainer)screen;
            kind = gui.field_147002_h.inventorySlots.size() > 63 ? "double_chest" : "chest";
        }
        else if (screen instanceof GuiMerchant) kind = "merchant";
        else if (screen instanceof GuiGameOver) kind = "death";
        else if (screen instanceof net.minecraft.client.gui.GuiWinGame) kind = "credits";
        else return null;
        out.addProperty("kind", kind);
        ScaledResolution scaled = new ScaledResolution(mc, mc.displayWidth, mc.displayHeight);
        out.addProperty("mouseX", Mouse.getX() * scaled.getScaledWidth() / mc.displayWidth);
        out.addProperty("mouseY", scaled.getScaledHeight() - Mouse.getY() * scaled.getScaledHeight() / mc.displayHeight - 1);
        if (screen instanceof GuiContainer)
        {
            GuiContainer gui = (GuiContainer)screen;
            JsonArray slots = new JsonArray();
            for (Object s : gui.field_147002_h.inventorySlots)
                slots.add(Rows.stackJson(((Slot)s).getStack()));
            out.add("slots", slots);
            out.add("cursor", Rows.stackJson(mc.thePlayer.inventory.getItemStack()));
            if (screen instanceof GuiFurnace)
            {
                try
                {
                    TileEntityFurnace furnace = (TileEntityFurnace)RenderStateProbe.fld(screen, "field_147086_v").get(screen);
                    out.addProperty("burn", furnace.field_145956_a);
                    out.addProperty("fuel", furnace.field_145963_i);
                    out.addProperty("cook", furnace.field_145961_j);
                }
                catch (Exception e) { throw new RuntimeException(e); }
            }
            try
            {
                /* GuiContainer's origin: InventoryEffectRenderer moves it left
                 * when the player has an effect */
                out.addProperty("left", RenderStateProbe.fld(screen, "field_147003_i").getInt(screen));
                out.addProperty("top", RenderStateProbe.fld(screen, "field_147009_r").getInt(screen));
            }
            catch (Exception e) { throw new RuntimeException(e); }
            if (screen instanceof GuiInventory)
            {
                /* InventoryEffectRenderer's column, in getActivePotionEffects'
                 * own (HashMap) order: id, amplifier, duration, max flag */
                JsonArray effects = new JsonArray();
                for (Object eo : mc.thePlayer.getActivePotionEffects())
                {
                    net.minecraft.potion.PotionEffect pe = (net.minecraft.potion.PotionEffect)eo;
                    JsonArray e = new JsonArray();
                    e.add(new com.google.gson.JsonPrimitive(pe.getPotionID()));
                    e.add(new com.google.gson.JsonPrimitive(pe.getAmplifier()));
                    e.add(new com.google.gson.JsonPrimitive(pe.getDuration()));
                    e.add(new com.google.gson.JsonPrimitive(pe.getIsPotionDurationMax() ? 1 : 0));
                    effects.add(e);
                }
                out.add("effects", effects);
                out.add("preview", preview(mc.thePlayer, pt));
                /* the mouse func_146976_a turns the preview toward: the one the
                 * last drawScreen stored (0, 0 before the first) */
                try
                {
                    RenderStateProbe.put(out, "pmx", RenderStateProbe.fld(screen, "field_147048_u").getFloat(screen));
                    RenderStateProbe.put(out, "pmy", RenderStateProbe.fld(screen, "field_147047_v").getFloat(screen));
                }
                catch (Exception e) { throw new RuntimeException(e); }
            }
            if (screen instanceof GuiMerchant)
            {
                try
                {
                    GuiMerchant merch = (GuiMerchant)screen;
                    out.addProperty("name", (String)RenderStateProbe.fld(screen, "field_147040_A").get(screen));
                    out.addProperty("trsel", RenderStateProbe.fld(screen, "field_147041_z").getInt(screen));
                    MerchantRecipeList list = merch.func_147035_g().getRecipes(mc.thePlayer);
                    JsonArray recipes = new JsonArray();
                    if (list != null)
                        for (Object ro : list)
                        {
                            MerchantRecipe r = (MerchantRecipe)ro;
                            JsonObject rj = new JsonObject();
                            rj.add("buy", Rows.stackJson(r.getItemToBuy()));
                            if (r.hasSecondItemToBuy()) rj.add("buyB", Rows.stackJson(r.getSecondItemToBuy()));
                            rj.add("sell", Rows.stackJson(r.getItemToSell()));
                            rj.addProperty("uses", RenderStateProbe.fld(r, "toolUses").getInt(r));
                            rj.addProperty("maxUses", RenderStateProbe.fld(r, "maxTradeUses").getInt(r));
                            recipes.add(rj);
                        }
                    out.add("recipes", recipes);
                }
                catch (Exception e) { throw new RuntimeException(e); }
            }
        }
        else if (screen instanceof net.minecraft.client.gui.GuiWinGame)
        {
            /* GuiWinGame's clock, speed and text: the lines initGui built (end.txt
             * with the Random(8124371L) obfuscation, credits.txt, both wrapped to
             * 274), which the native build is checked against */
            try
            {
                out.addProperty("t", RenderStateProbe.fld(screen, "field_146581_h").getInt(screen));
                RenderStateProbe.put(out, "speed", RenderStateProbe.fld(screen, "field_146578_s").getFloat(screen));
                out.addProperty("total", RenderStateProbe.fld(screen, "field_146579_r").getInt(screen));
                out.addProperty("user", mc.getSession().getUsername());
                out.addProperty("w", screen.width);
                out.addProperty("h", screen.height);
                JsonArray lines = new JsonArray();
                for (Object lo : (java.util.List)RenderStateProbe.fld(screen, "field_146582_i").get(screen))
                    lines.add(new com.google.gson.JsonPrimitive((String)lo));
                out.add("lines", lines);
            }
            catch (Exception e) { throw new RuntimeException(e); }
        }
        else if (screen instanceof GuiGameOver)
        {
            out.addProperty("score", mc.thePlayer.getScore());
            try { out.addProperty("deathTicks", RenderStateProbe.fld(screen, "field_146347_a").getInt(screen)); }
            catch (Exception e) { throw new RuntimeException(e); }
        }
        return out;
    }
}
