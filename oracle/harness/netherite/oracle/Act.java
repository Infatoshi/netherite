package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGameOver;
import net.minecraft.client.gui.GuiMerchant;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.GuiSleepMP;
import net.minecraft.client.gui.GuiWinGame;
import net.minecraft.client.gui.inventory.GuiContainer;
import net.minecraft.client.settings.KeyBinding;
import net.minecraft.inventory.ContainerMerchant;
import net.minecraft.network.play.client.C17PacketCustomPayload;

/**
 * One tick of input, in two forms.
 *
 * Tape form (what every row stores, and what replay applies exactly):
 *   look  [yaw, pitch, prevYaw, prevPitch] at tick start
 *   gui   ordered GUI effects: ["click", window, slot, button, mode], ["close"], ["respawn"], ["wake"],
 *         ["chat", mods, px, py, clip, events] (the chat screen's input, ChatInput)
 *   in    post-input-phase snapshot: keys {desc: [held, presses]}, hb, focus, lcc, ctrl
 *   opts  render options that changed this tick
 *
 * Agent form (what a controller sends): hold / press short key names, look or
 * dlook, hotbar, gui. It is resolved into tape form at the input point.
 *
 * Agent-input semantics (what an agent can do is what a person at the
 * vanilla client can do):
 *   - under a screen that lets the input block run (GuiInventory) the act's
 *     holds, presses and hotbar are ignored: the screen's handleInput drains
 *     the keyboard and mouse, and opening it unpressed every binding;
 *   - a gui op runs only under its screen: "respawn" on the game over or
 *     credits screen, "wake" on the sleep screen, "trsel" on its merchant
 *     window, "click" on the open window with a slot the window has (or
 *     -999 or -1 where Container.slotClick takes them); any other op, and
 *     every op of a tick with no screen, is refused (ORACLE REFUSE, not
 *     recorded); after a "close" the closed screen still takes the tick's
 *     later keys (GuiScreen.handleInput's loop goes on): another "close",
 *     and a hotbar key's swap (mode 2) or Q's drop (mode 4) on the old
 *     window's slot, which PlayerControllerMP.windowClick predicts on the
 *     inventory container (the lane/rawfuzz sessions: E and a number key
 *     in one tick);
 *   - a step whose hotbar is outside 0..8 or whose look pitch is outside
 *     -90..90 is refused whole (ORACLE REFUSE, an error reply, no tick);
 *     a tape row with such values ends the replay (native: a bad row).
 */
final class Act
{
    float[] look;
    JsonArray gui;
    /** gui holds the agent's ops, not yet run under a screen (applyGui
     * resolves them); what is still raw at the row's end is refused */
    boolean guiRaw;
    // snapshot
    boolean snap;
    Map<String, int[]> keys;
    int hb = -1, focus = -1, lcc = -1, ctrl;
    JsonObject opts;
    // agent form
    Set<String> hold;
    Map<String, Integer> press;
    float[] dlook;
    /** Agent form "aim": {"cls": simple class names joined by |, "range": blocks, "at": height fraction}: look at
     * the nearest such client entity's body centre (the resolved look is what
     * the tape keeps). */
    String aimClass;
    double aimRange;
    /** "at": the aimed height as a fraction of the target's height (0.5, the
     * box centre, by default; a person hitting an enderman looks at its legs) */
    double aimAt = 0.5D;

    static final Map<String, String> SHORT = new HashMap<String, String>();

    static
    {
        String[][] m = {
            {"forward", "key.forward"}, {"back", "key.back"}, {"left", "key.left"}, {"right", "key.right"},
            {"jump", "key.jump"}, {"sneak", "key.sneak"}, {"sprint", "key.sprint"}, {"attack", "key.attack"},
            {"use", "key.use"}, {"drop", "key.drop"}, {"inventory", "key.inventory"}, {"pick", "key.pickItem"}
        };
        for (String[] p : m) SHORT.put(p[0], p[1]);
        for (int i = 1; i <= 9; ++i) SHORT.put("hotbar." + i, "key.hotbar." + i);
    }

    /** The agent has no perspective key: the native client draws only the
     *  first-person view (thirdPersonView 1 and 2 are not ported, N-06), so a
     *  press would split the observations (RL-08). A human's F5 stays in the
     *  tape form. */
    static String keyName(String s)
    {
        String k = SHORT.get(s);
        if (k == null) k = s;
        if ("perspective".equals(s) || "key.togglePerspective".equals(k))
            throw new IllegalArgumentException("oracle: the agent has no perspective key (third-person views are not ported)");
        return k;
    }

    boolean agentForm()
    {
        return !snap;
    }

    /** Fresh per-tick copy of an agent action; resolving it must not consume the step action. */
    Act copyAgent()
    {
        Act a = new Act();
        a.look = look;
        a.dlook = dlook;
        a.gui = gui;
        a.guiRaw = guiRaw;
        a.opts = opts;
        a.hb = hb;
        a.ctrl = ctrl;
        a.hold = hold;
        a.press = press;
        a.aimClass = aimClass;
        a.aimRange = aimRange;
        a.aimAt = aimAt;
        return a;
    }

    /** Copy for ticks 2..n of a multi-tick step: keys stay held, nothing else repeats. */
    Act holdOnly()
    {
        Act a = new Act();
        a.hold = hold;
        a.press = null;
        /* an aim keeps tracking its target on every tick of the step */
        a.aimClass = aimClass;
        a.aimRange = aimRange;
        a.aimAt = aimAt;
        return a;
    }

    // ---------------------------------------------------------------- parse

    static Act parseAgent(JsonObject o)
    {
        Act a = new Act();
        if (o == null) return a;
        if (o.has("look")) a.look = floats(o.getAsJsonArray("look"));
        if (o.has("dlook")) a.dlook = floats(o.getAsJsonArray("dlook"));
        if (o.has("aim"))
        {
            JsonObject aim = o.getAsJsonObject("aim");
            a.aimClass = aim.get("cls").getAsString();
            a.aimRange = aim.has("range") ? aim.get("range").getAsDouble() : 16.0D;
            if (aim.has("at")) a.aimAt = aim.get("at").getAsDouble();
        }
        if (o.has("gui")) { a.gui = o.getAsJsonArray("gui"); a.guiRaw = true; }
        if (o.has("opts")) a.opts = o.getAsJsonObject("opts");
        if (o.has("hotbar")) a.hb = o.get("hotbar").getAsInt();
        if (o.has("ctrl")) a.ctrl = o.get("ctrl").getAsInt();
        /* the vanilla client selects 0..8 and clamps the pitch (setAngles) */
        if (o.has("hotbar")) checkHotbar(a.hb);
        if (a.look != null) checkPitch(a.look);
        a.hold = new HashSet<String>();
        if (o.has("hold")) for (JsonElement e : o.getAsJsonArray("hold")) a.hold.add(keyName(e.getAsString()));
        a.press = new HashMap<String, Integer>();
        if (o.has("press"))
        {
            for (JsonElement e : o.getAsJsonArray("press"))
            {
                String k = keyName(e.getAsString());
                Integer c = a.press.get(k);
                a.press.put(k, c == null ? 1 : c + 1);
            }
        }
        return a;
    }

    static Act parseTape(JsonObject o)
    {
        Act a = new Act();
        if (o == null) return null;
        /* only the pitch: setAngles does not clamp the previous pitch, and
         * adding the clamped pitch's float step can land it an ulp past
         * the limit (-90.00001), so a tape carries such values */
        if (o.has("look")) checkTapePitch(a.look = floats(o.getAsJsonArray("look")));
        /* a tape op no screen took stays as the tape has it (a harness
         * before lane/agentscreens wrote the agent's ops raw on such ticks) */
        if (o.has("gui")) a.gui = o.getAsJsonArray("gui");
        if (o.has("opts")) a.opts = o.getAsJsonObject("opts");
        if (o.has("in"))
        {
            JsonObject in = o.getAsJsonObject("in");
            a.snap = true;
            a.keys = new HashMap<String, int[]>();
            if (in.has("keys"))
            {
                for (Map.Entry<String, JsonElement> e : in.getAsJsonObject("keys").entrySet())
                {
                    JsonArray v = e.getValue().getAsJsonArray();
                    a.keys.put(e.getKey(), new int[] {v.get(0).getAsInt(), v.get(1).getAsInt()});
                }
            }
            a.hb = in.get("hb").getAsInt();
            if (a.hb != -1) checkHotbar(a.hb);
            a.focus = in.get("focus").getAsInt();
            a.lcc = in.get("lcc").getAsInt();
            a.ctrl = in.has("ctrl") ? in.get("ctrl").getAsInt() : 0;
        }
        else
        {
            a.snap = true; // tape rows without "in": the input block did not run that tick
            a.keys = null;
        }
        return a;
    }

    /* The refusals' texts are native session_parse_act's too. */
    static void checkHotbar(int hb)
    {
        if (hb < 0 || hb > 8) throw new IllegalArgumentException("hotbar " + hb + " outside 0..8");
    }

    static void checkPitch(float[] look)
    {
        for (int i = 1; i < look.length; i += 2)
            if (!(look[i] >= -90.0F && look[i] <= 90.0F))
                throw new IllegalArgumentException("pitch " + look[i] + " outside -90..90");
    }

    static void refuse(String what)
    {
        System.out.println("ORACLE REFUSE t=" + Oracle.tick + " " + what);
    }

    static void checkTapePitch(float[] look)
    {
        if (look.length > 1 && !(look[1] >= -90.0F && look[1] <= 90.0F))
            throw new IllegalArgumentException("pitch " + look[1] + " outside -90..90");
    }

    static float[] floats(JsonArray a)
    {
        float[] f = new float[a.size()];
        for (int i = 0; i < f.length; ++i) f[i] = Float.parseFloat(a.get(i).getAsString());
        return f;
    }

    // ---------------------------------------------------------------- tape json

    JsonObject toJson()
    {
        JsonObject o = new JsonObject();
        if (look != null)
        {
            JsonArray l = new JsonArray();
            for (float f : look) l.add(new JsonPrimitive(f));
            o.add("look", l);
        }
        if (gui != null && gui.size() > 0)
        {
            /* no screen took the tick's gui input: its ops did not run */
            if (guiRaw) refuse("gui " + gui + " without a screen");
            else o.add("gui", gui);
        }
        if (keys != null)
        {
            JsonObject in = new JsonObject();
            JsonObject k = new JsonObject();
            for (Map.Entry<String, int[]> e : keys.entrySet())
            {
                JsonArray v = new JsonArray();
                v.add(new JsonPrimitive(e.getValue()[0]));
                v.add(new JsonPrimitive(e.getValue()[1]));
                k.add(e.getKey(), v);
            }
            in.add("keys", k);
            in.addProperty("hb", hb);
            in.addProperty("focus", focus);
            in.addProperty("lcc", lcc);
            if (ctrl != 0) in.addProperty("ctrl", ctrl);
            o.add("in", in);
        }
        if (opts != null) o.add("opts", opts);
        return o;
    }

    // ---------------------------------------------------------------- capture (play)

    static Act captureLook(Minecraft mc)
    {
        Act a = new Act();
        if (mc.thePlayer != null)
        {
            a.look = new float[] {mc.thePlayer.rotationYaw, mc.thePlayer.rotationPitch, mc.thePlayer.prevRotationYaw, mc.thePlayer.prevRotationPitch};
        }
        return a;
    }

    /** Snapshot of client input state right after the vanilla mouse and keyboard loops. */
    void captureSnapshot(Minecraft mc)
    {
        keys = new HashMap<String, int[]>();
        for (KeyBinding kb : mc.gameSettings.keyBindings)
        {
            boolean held = kb.getIsKeyPressed();
            int presses = kb.oraclePresses();
            if (held || presses != 0) keys.put(kb.getKeyDescription(), new int[] {held ? 1 : 0, presses});
        }
        hb = mc.thePlayer != null ? mc.thePlayer.inventory.currentItem : -1;
        focus = mc.inGameHasFocus ? 1 : 0;
        lcc = mc.leftClickCounter;
        ctrl = GuiScreen.isCtrlKeyDown() ? 1 : 0;
        snap = true;
    }

    // ---------------------------------------------------------------- apply (agent / replay)

    void applyLook(Minecraft mc)
    {
        if (mc.thePlayer == null) return;
        if (aimClass != null) aim(mc);
        if (look != null)
        {
            mc.thePlayer.rotationYaw = look[0];
            mc.thePlayer.rotationPitch = look[1];
            if (look.length >= 4)
            {
                mc.thePlayer.prevRotationYaw = look[2];
                mc.thePlayer.prevRotationPitch = look[3];
            }
        }
        else if (dlook != null)
        {
            // Same arithmetic as Entity.setAngles with the 0.15 factor already applied by the caller.
            float py = mc.thePlayer.rotationPitch;
            float yy = mc.thePlayer.rotationYaw;
            mc.thePlayer.rotationYaw = yy + dlook[0];
            mc.thePlayer.rotationPitch = py + dlook[1];
            if (mc.thePlayer.rotationPitch < -90.0F) mc.thePlayer.rotationPitch = -90.0F;
            if (mc.thePlayer.rotationPitch > 90.0F) mc.thePlayer.rotationPitch = 90.0F;
            mc.thePlayer.prevRotationPitch += mc.thePlayer.rotationPitch - py;
            mc.thePlayer.prevRotationYaw += mc.thePlayer.rotationYaw - yy;
        }
        // record the resolved absolute look
        look = new float[] {mc.thePlayer.rotationYaw, mc.thePlayer.rotationPitch, mc.thePlayer.prevRotationYaw, mc.thePlayer.prevRotationPitch};
        dlook = null;
    }

    /** Resolves "aim" into an absolute look: the nearest living client
     * entity of the class within range, from the player's eye (the client
     * player's posY) to its box centre ("at" moves the height). No target
     * leaves the look alone. */
    private void aim(Minecraft mc)
    {
        net.minecraft.entity.Entity best = null;
        double bestD = aimRange * aimRange;
        for (Object o : mc.theWorld.loadedEntityList)
        {
            net.minecraft.entity.Entity e = (net.minecraft.entity.Entity)o;
            if (e == mc.thePlayer || e.isDead || !("|" + aimClass + "|").contains("|" + e.getClass().getSimpleName() + "|")) continue;
            if (e instanceof net.minecraft.entity.EntityLivingBase && ((net.minecraft.entity.EntityLivingBase)e).getHealth() <= 0.0F) continue;
            double d = mc.thePlayer.getDistanceSqToEntity(e);
            if (d < bestD) { bestD = d; best = e; }
        }
        if (best == null) return;
        double dx = best.posX - mc.thePlayer.posX;
        double dy = best.boundingBox.minY + (double)best.height * aimAt - mc.thePlayer.posY;
        double dz = best.posZ - mc.thePlayer.posZ;
        float yaw = (float)(Math.atan2(-dx, dz) * 180.0D / Math.PI);
        float pitch = (float)(-Math.atan2(dy, Math.sqrt(dx * dx + dz * dz)) * 180.0D / Math.PI);
        look = new float[] {yaw, pitch, yaw, pitch};
        dlook = null;
    }

    void applyGui(Minecraft mc)
    {
        if (gui == null || mc.thePlayer == null) return;
        JsonArray resolved = new JsonArray();
        /* the chat screen's agent ops: one tape op for the tick (ChatInput) */
        JsonArray chat = new JsonArray();
        for (JsonElement e : gui)
            if (ChatInput.isAgentOp(e.getAsJsonArray().get(0).getAsString())) chat.add(e);
        if (chat.size() > 0)
        {
            JsonArray rest = new JsonArray();
            for (JsonElement e : gui)
                if (!ChatInput.isAgentOp(e.getAsJsonArray().get(0).getAsString())) rest.add(e);
            rest.add(ChatInput.resolve(mc, chat));
            gui = rest;
        }
        GuiContainer closed = null;   // the container screen a close op of this tick closed
        for (JsonElement e : gui)
        {
            JsonArray op = e.getAsJsonArray();
            String kind = op.get(0).getAsString();
            GuiScreen screen = mc.currentScreen;
            if (closed != null && !(screen instanceof GuiContainer) && ("close".equals(kind) || "click".equals(kind)))
            {
                /* the closed screen's later keys (its keyTyped) */
                if ("close".equals(kind))
                {
                    mc.thePlayer.closeScreen();
                    resolved.add(Oracle.op("close"));
                    continue;
                }
                int w = op.get(1).getAsInt();
                int slot = op.get(2).getAsInt(), button = op.get(3).getAsInt(), mode = op.get(4).getAsInt();
                String bad = w != closed.field_147002_h.windowId ? "window " + w + " is not the closed window " + closed.field_147002_h.windowId
                    : mode != 2 && mode != 4 ? "mode " + mode + " is not a key's"
                    : slot < 0 || slot >= mc.thePlayer.openContainer.inventorySlots.size() ? "slot " + slot + " outside the inventory container"
                    : null;
                if (bad != null) { refuse("click " + op + " after the close: " + bad); continue; }
                mc.playerController.windowClick(w, slot, button, mode, mc.thePlayer);
                resolved.add(Oracle.op("click", w, slot, button, mode));
                continue;
            }
            if ("chat".equals(kind))
            {
                ChatInput.apply(mc, op);
                resolved.add(op);
            }
            else if ("click".equals(kind))
            {
                int w = op.get(1).getAsInt();
                if (w < 0) w = mc.thePlayer.openContainer.windowId;
                int slot = op.get(2).getAsInt(), button = op.get(3).getAsInt(), mode = op.get(4).getAsInt();
                String bad = badClick(mc, screen, w, slot, button, mode);
                if (bad != null) { refuse("click " + op + ": " + bad); continue; }
                mc.playerController.windowClick(w, slot, button, mode, mc.thePlayer);
                resolved.add(Oracle.op("click", w, slot, button, mode));
            }
            else if ("close".equals(kind))
            {
                /* GuiContainer.keyTyped's Escape or inventory key */
                if (!(screen instanceof GuiContainer)) { refuse("close without a container screen"); continue; }
                closed = (GuiContainer)screen;
                mc.thePlayer.closeScreen();
                resolved.add(Oracle.op("close"));
            }
            else if ("respawn".equals(kind))
            {
                /* GuiGameOver's respawn button, GuiWinGame's Escape */
                if (!(screen instanceof GuiGameOver) && !(screen instanceof GuiWinGame))
                {
                    refuse("respawn without the game over or credits screen");
                    continue;
                }
                /* the screen's own handler, as a person's click or key reaches it */
                if (screen instanceof GuiGameOver) invokeGui(screen, "actionPerformed", new Class<?>[] {net.minecraft.client.gui.GuiButton.class}, button(screen, 0));
                else invokeGui(screen, "keyTyped", new Class<?>[] {char.class, int.class}, (char)0, 1);
                resolved.add(Oracle.op("respawn"));
            }
            else if ("wake".equals(kind))
            {
                /* GuiSleepMP's Leave Bed button: actionPerformed(button 1),
                 * func_146418_g's C0BPacketEntityAction(player, 3) */
                if (!(screen instanceof GuiSleepMP)) { refuse("wake without the sleep screen"); continue; }
                try
                {
                    java.lang.reflect.Method m = GuiSleepMP.class.getDeclaredMethod("func_146418_g");
                    m.setAccessible(true);
                    m.invoke(screen);
                }
                catch (Exception ex) { throw new RuntimeException(ex); }
                resolved.add(Oracle.op("wake"));
            }
            else if ("trsel".equals(kind))
            {
                /* GuiMerchant.actionPerformed's arrow: the client's own index
                 * and container change, then the C17 MC|TrSel */
                int w = op.get(1).getAsInt();
                int idx = op.get(2).getAsInt();
                if (!(screen instanceof GuiMerchant) || mc.thePlayer.openContainer.windowId != w)
                {
                    refuse("trsel without the merchant window " + w);
                    continue;
                }
                GuiMerchant merch = (GuiMerchant)screen;
                /* one step is a click on an enabled arrow: GuiMerchant.actionPerformed */
                net.minecraft.client.gui.GuiButton arrow = merchantArrow(merch, idx);
                if (arrow != null)
                {
                    invokeGui(screen, "actionPerformed", new Class<?>[] {net.minecraft.client.gui.GuiButton.class}, arrow);
                    resolved.add(Oracle.op("trsel", w, idx));
                    continue;
                }
                try
                {
                    java.lang.reflect.Field f = GuiMerchant.class.getDeclaredField("field_147041_z");
                    f.setAccessible(true);
                    f.setInt(merch, idx);
                }
                catch (Exception ex) { throw new RuntimeException(ex); }
                ((ContainerMerchant)merch.field_147002_h).setCurrentRecipeIndex(idx);
                /* GuiMerchant's own packet: the (String, ByteBuf) ctor, whose
                 * payload is the buffer's whole backing array */
                io.netty.buffer.ByteBuf data = io.netty.buffer.Unpooled.buffer();
                try
                {
                    data.writeInt(idx);
                    mc.thePlayer.sendQueue.addToSendQueue(new C17PacketCustomPayload("MC|TrSel", data));
                }
                finally
                {
                    data.release();
                }
                resolved.add(Oracle.op("trsel", w, idx));
            }
            else
            {
                throw new IllegalArgumentException("oracle: unknown gui op " + kind);
            }
        }
        gui = resolved;
        guiRaw = false;
    }

    /** Why a click is not one the vanilla client's container screen makes, or
     * null: GuiContainer clicks its own window, on one of its slots, outside
     * the window (-999) or on no slot (-1); Container.slotClick throws on a
     * slot it does not have (a hotbar swap or a drag's slot step always
     * reads one). Native surv_client_click refuses the same clicks. */
    static String badClick(Minecraft mc, GuiScreen screen, int w, int slot, int button, int mode)
    {
        if (!(screen instanceof GuiContainer)) return "no container screen";
        int n = mc.thePlayer.openContainer.inventorySlots.size();
        if (w != mc.thePlayer.openContainer.windowId) return "window " + w + " is not the open window " + mc.thePlayer.openContainer.windowId;
        if (slot >= n || (slot < 0 && slot != -1 && slot != -999)) return "slot " + slot + " outside window " + w + "'s " + n + " slots";
        if (slot < 0 && (mode == 2 || (mode == 5 && (button & 3) == 1))) return "slot " + slot + " in mode " + mode;
        return null;
    }

    /** A GuiScreen method a person's click or key runs (protected in vanilla). */
    static void invokeGui(GuiScreen screen, String name, Class<?>[] types, Object... args)
    {
        try
        {
            for (Class<?> c = screen.getClass(); c != null; c = c.getSuperclass())
            {
                java.lang.reflect.Method m;
                try { m = c.getDeclaredMethod(name, types); }
                catch (NoSuchMethodException e) { continue; }
                m.setAccessible(true);
                m.invoke(screen, args);
                return;
            }
            throw new IllegalStateException("oracle: no " + name + " on " + screen.getClass().getName());
        }
        catch (java.lang.reflect.InvocationTargetException e)
        {
            Throwable t = e.getCause();
            if (t instanceof RuntimeException) throw (RuntimeException)t;
            throw new RuntimeException(t);
        }
        catch (IllegalAccessException e) { throw new RuntimeException(e); }
    }

    /** The screen's button with this id (GuiGameOver's respawn is 0). */
    static net.minecraft.client.gui.GuiButton button(GuiScreen screen, int id)
    {
        try
        {
            java.lang.reflect.Field f = GuiScreen.class.getDeclaredField("buttonList");
            f.setAccessible(true);
            for (Object o : (java.util.List<?>)f.get(screen))
                if (((net.minecraft.client.gui.GuiButton)o).id == id) return (net.minecraft.client.gui.GuiButton)o;
        }
        catch (Exception e) { throw new RuntimeException(e); }
        throw new IllegalStateException("oracle: no button " + id + " on " + screen.getClass().getName());
    }

    /** The merchant's enabled arrow one step from its index to idx, else null. */
    static net.minecraft.client.gui.GuiButton merchantArrow(GuiMerchant merch, int idx)
    {
        try
        {
            java.lang.reflect.Field cur = GuiMerchant.class.getDeclaredField("field_147041_z");
            cur.setAccessible(true);
            int d = idx - cur.getInt(merch);
            if (d != 1 && d != -1) return null;
            java.lang.reflect.Field f = GuiMerchant.class.getDeclaredField(d == 1 ? "field_147043_x" : "field_147042_y");
            f.setAccessible(true);
            net.minecraft.client.gui.GuiButton b = (net.minecraft.client.gui.GuiButton)f.get(merch);
            return b != null && b.enabled ? b : null;
        }
        catch (Exception e) { throw new RuntimeException(e); }
    }

    /** Replaces the vanilla mouse loop, leftClickCounter decrement and keyboard loop. */
    void applyInput(Minecraft mc)
    {
        if (agentForm())
        {
            if (mc.leftClickCounter > 0) --mc.leftClickCounter;
            /* a screen that lets the input block run (GuiInventory) took the
             * keyboard and mouse in its handleInput: no binding and no hotbar
             * changes under it, as for a person */
            boolean screen = mc.currentScreen != null;
            keys = new HashMap<String, int[]>();
            for (KeyBinding kb : mc.gameSettings.keyBindings)
            {
                String d = kb.getKeyDescription();
                boolean held;
                int presses;
                if (screen)
                {
                    held = kb.getIsKeyPressed();
                    presses = kb.oraclePresses();
                }
                else
                {
                    held = hold != null && hold.contains(d);
                    Integer p = press != null ? press.get(d) : null;
                    presses = kb.oraclePresses() + (p == null ? 0 : p);
                    kb.oracleSet(held, presses);
                }
                if (held || presses != 0) keys.put(d, new int[] {held ? 1 : 0, presses});
            }
            if (!screen && hb >= 0 && mc.thePlayer != null) mc.thePlayer.inventory.currentItem = hb;
            hb = mc.thePlayer != null ? mc.thePlayer.inventory.currentItem : -1;
            focus = mc.inGameHasFocus ? 1 : 0;
            lcc = mc.leftClickCounter;
            ctrl = this.ctrl;
            snap = true;
            hold = null;
            press = null;
            return;
        }
        if (keys == null) return; // tape row without a snapshot: nothing to apply
        for (KeyBinding kb : mc.gameSettings.keyBindings)
        {
            int[] v = keys.get(kb.getKeyDescription());
            kb.oracleSet(v != null && v[0] != 0, v != null ? v[1] : 0);
        }
        if (hb >= 0 && mc.thePlayer != null) mc.thePlayer.inventory.currentItem = hb;
        mc.inGameHasFocus = focus != 0;
        mc.leftClickCounter = lcc;
    }

    void applyOpts(Minecraft mc)
    {
        if (opts == null) return;
        if (opts.has("tpv")) mc.gameSettings.thirdPersonView = opts.get("tpv").getAsInt();
        if (opts.has("hide")) mc.gameSettings.hideGUI = opts.get("hide").getAsInt() != 0;
        if (opts.has("smooth")) mc.gameSettings.smoothCamera = opts.get("smooth").getAsInt() != 0;
        if (opts.has("rd")) mc.gameSettings.renderDistanceChunks = opts.get("rd").getAsInt();
        if (opts.has("dbg")) mc.gameSettings.showDebugInfo = opts.get("dbg").getAsInt() != 0;
    }
}
