package net.minecraft.client.settings;

import com.google.common.collect.Maps;
import com.google.gson.Gson;

import cpw.mods.fml.client.FMLClientHandler;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.lang.reflect.ParameterizedType;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.audio.SoundCategory;
import net.minecraft.client.gui.GuiNewChat;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.stream.TwitchStream;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.network.play.client.C15PacketClientSettings;
import net.minecraft.util.MathHelper;
import net.minecraft.world.EnumDifficulty;
import org.apache.commons.lang3.ArrayUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;
import org.lwjgl.opengl.Display;

@SideOnly(Side.CLIENT)
public class GameSettings
{
    private static final Logger logger = LogManager.getLogger();
    private static final Gson gson = new Gson();
    private static final ParameterizedType typeListString = new ParameterizedType()
    {
        private static final String __OBFID = "CL_00000651";
        public Type[] getActualTypeArguments()
        {
            return new Type[] {String.class};
        }
        public Type getRawType()
        {
            return List.class;
        }
        public Type getOwnerType()
        {
            return null;
        }
    };
    /** GUI scale values */
    private static final String[] GUISCALES = new String[] {"options.guiScale.auto", "options.guiScale.small", "options.guiScale.normal", "options.guiScale.large"};
    private static final String[] PARTICLES = new String[] {"options.particles.all", "options.particles.decreased", "options.particles.minimal"};
    private static final String[] AMBIENT_OCCLUSIONS = new String[] {"options.ao.off", "options.ao.min", "options.ao.max"};
    private static final String[] field_152391_aS = new String[] {"options.stream.compression.low", "options.stream.compression.medium", "options.stream.compression.high"};
    private static final String[] field_152392_aT = new String[] {"options.stream.chat.enabled.streaming", "options.stream.chat.enabled.always", "options.stream.chat.enabled.never"};
    private static final String[] field_152393_aU = new String[] {"options.stream.chat.userFilter.all", "options.stream.chat.userFilter.subs", "options.stream.chat.userFilter.mods"};
    private static final String[] field_152394_aV = new String[] {"options.stream.mic_toggle.mute", "options.stream.mic_toggle.talk"};
    public float mouseSensitivity = 0.5F;
    public boolean invertMouse;
    public int renderDistanceChunks = -1;
    public boolean viewBobbing = true;
    public boolean anaglyph;
    /** Advanced OpenGL */
    public boolean advancedOpengl;
    public boolean fboEnable = true;
    public int limitFramerate = 120;
    public boolean fancyGraphics = true;
    /** Smooth Lighting */
    public int ambientOcclusion = 2;
    /** Clouds flag */
    public boolean clouds = true;
    public List resourcePacks = new ArrayList();
    public EntityPlayer.EnumChatVisibility chatVisibility;
    public boolean chatColours;
    public boolean chatLinks;
    public boolean chatLinksPrompt;
    public float chatOpacity;
    public boolean snooperEnabled;
    public boolean fullScreen;
    public boolean enableVsync;
    public boolean hideServerAddress;
    /** Whether to show advanced information on item tooltips, toggled by F3+H */
    public boolean advancedItemTooltips;
    /** Whether to pause when the game loses focus, toggled by F3+P */
    public boolean pauseOnLostFocus;
    /** Whether to show your cape */
    public boolean showCape;
    public boolean touchscreen;
    public int overrideWidth;
    public int overrideHeight;
    public boolean heldItemTooltips;
    public float chatScale;
    public float chatWidth;
    public float chatHeightUnfocused;
    public float chatHeightFocused;
    public boolean showInventoryAchievementHint;
    public int mipmapLevels;
    public int anisotropicFiltering;
    private Map mapSoundLevels;
    public float field_152400_J;
    public float field_152401_K;
    public float field_152402_L;
    public float field_152403_M;
    public float field_152404_N;
    public int field_152405_O;
    public boolean field_152406_P;
    public String field_152407_Q;
    public int field_152408_R;
    public int field_152409_S;
    public int field_152410_T;
    public KeyBinding keyBindForward;
    public KeyBinding keyBindLeft;
    public KeyBinding keyBindBack;
    public KeyBinding keyBindRight;
    public KeyBinding keyBindJump;
    public KeyBinding keyBindSneak;
    public KeyBinding keyBindInventory;
    public KeyBinding keyBindUseItem;
    public KeyBinding keyBindDrop;
    public KeyBinding keyBindAttack;
    public KeyBinding keyBindPickBlock;
    public KeyBinding keyBindSprint;
    public KeyBinding keyBindChat;
    public KeyBinding keyBindPlayerList;
    public KeyBinding keyBindCommand;
    public KeyBinding keyBindScreenshot;
    public KeyBinding keyBindTogglePerspective;
    public KeyBinding keyBindSmoothCamera;
    public KeyBinding field_152395_am;
    public KeyBinding field_152396_an;
    public KeyBinding field_152397_ao;
    public KeyBinding field_152398_ap;
    public KeyBinding field_152399_aq;
    public KeyBinding[] keyBindsHotbar;
    public KeyBinding[] keyBindings;
    protected Minecraft mc;
    private File optionsFile;
    public EnumDifficulty difficulty;
    public boolean hideGUI;
    public int thirdPersonView;
    /** true if debug info should be displayed instead of version */
    public boolean showDebugInfo;
    public boolean showDebugProfilerChart;
    /** The lastServer string. */
    public String lastServer;
    /** No clipping for singleplayer */
    public boolean noclip;
    /** Smooth Camera Toggle */
    public boolean smoothCamera;
    public boolean debugCamEnable;
    /** No clipping movement rate */
    public float noclipRate;
    /** Change rate for debug camera */
    public float debugCamRate;
    public float fovSetting;
    public float gammaSetting;
    public float saturation;
    /** GUI scale */
    public int guiScale;
    /** Determines amount of particles. 0 = All, 1 = Decreased, 2 = Minimal */
    public int particleSetting;
    /** Game settings language */
    public String language;
    public boolean forceUnicodeFont;
    private static final String __OBFID = "CL_00000650";

    public GameSettings(Minecraft p_i1016_1_, File p_i1016_2_)
    {
        this.chatVisibility = EntityPlayer.EnumChatVisibility.FULL;
        this.chatColours = true;
        this.chatLinks = true;
        this.chatLinksPrompt = true;
        this.chatOpacity = 1.0F;
        this.snooperEnabled = true;
        this.enableVsync = true;
        this.pauseOnLostFocus = true;
        this.showCape = true;
        this.heldItemTooltips = true;
        this.chatScale = 1.0F;
        this.chatWidth = 1.0F;
        this.chatHeightUnfocused = 0.44366196F;
        this.chatHeightFocused = 1.0F;
        this.showInventoryAchievementHint = true;
        this.mipmapLevels = 4;
        this.anisotropicFiltering = 1;
        this.mapSoundLevels = Maps.newEnumMap(SoundCategory.class);
        this.field_152400_J = 0.5F;
        this.field_152401_K = 1.0F;
        this.field_152402_L = 1.0F;
        this.field_152403_M = 0.5412844F;
        this.field_152404_N = 0.31690142F;
        this.field_152405_O = 1;
        this.field_152406_P = true;
        this.field_152407_Q = "";
        this.field_152408_R = 0;
        this.field_152409_S = 0;
        this.field_152410_T = 0;
        this.keyBindForward = new KeyBinding("key.forward", 17, "key.categories.movement");
        this.keyBindLeft = new KeyBinding("key.left", 30, "key.categories.movement");
        this.keyBindBack = new KeyBinding("key.back", 31, "key.categories.movement");
        this.keyBindRight = new KeyBinding("key.right", 32, "key.categories.movement");
        this.keyBindJump = new KeyBinding("key.jump", 57, "key.categories.movement");
        this.keyBindSneak = new KeyBinding("key.sneak", 42, "key.categories.movement");
        this.keyBindInventory = new KeyBinding("key.inventory", 18, "key.categories.inventory");
        this.keyBindUseItem = new KeyBinding("key.use", -99, "key.categories.gameplay");
        this.keyBindDrop = new KeyBinding("key.drop", 16, "key.categories.gameplay");
        this.keyBindAttack = new KeyBinding("key.attack", -100, "key.categories.gameplay");
        this.keyBindPickBlock = new KeyBinding("key.pickItem", -98, "key.categories.gameplay");
        this.keyBindSprint = new KeyBinding("key.sprint", 29, "key.categories.gameplay");
        this.keyBindChat = new KeyBinding("key.chat", 20, "key.categories.multiplayer");
        this.keyBindPlayerList = new KeyBinding("key.playerlist", 15, "key.categories.multiplayer");
        this.keyBindCommand = new KeyBinding("key.command", 53, "key.categories.multiplayer");
        this.keyBindScreenshot = new KeyBinding("key.screenshot", 60, "key.categories.misc");
        this.keyBindTogglePerspective = new KeyBinding("key.togglePerspective", 63, "key.categories.misc");
        this.keyBindSmoothCamera = new KeyBinding("key.smoothCamera", 0, "key.categories.misc");
        this.field_152395_am = new KeyBinding("key.fullscreen", 87, "key.categories.misc");
        this.field_152396_an = new KeyBinding("key.streamStartStop", 64, "key.categories.stream");
        this.field_152397_ao = new KeyBinding("key.streamPauseUnpause", 65, "key.categories.stream");
        this.field_152398_ap = new KeyBinding("key.streamCommercial", 0, "key.categories.stream");
        this.field_152399_aq = new KeyBinding("key.streamToggleMic", 0, "key.categories.stream");
        this.keyBindsHotbar = new KeyBinding[] {new KeyBinding("key.hotbar.1", 2, "key.categories.inventory"), new KeyBinding("key.hotbar.2", 3, "key.categories.inventory"), new KeyBinding("key.hotbar.3", 4, "key.categories.inventory"), new KeyBinding("key.hotbar.4", 5, "key.categories.inventory"), new KeyBinding("key.hotbar.5", 6, "key.categories.inventory"), new KeyBinding("key.hotbar.6", 7, "key.categories.inventory"), new KeyBinding("key.hotbar.7", 8, "key.categories.inventory"), new KeyBinding("key.hotbar.8", 9, "key.categories.inventory"), new KeyBinding("key.hotbar.9", 10, "key.categories.inventory")};
        this.keyBindings = (KeyBinding[])ArrayUtils.addAll(new KeyBinding[] {this.keyBindAttack, this.keyBindUseItem, this.keyBindForward, this.keyBindLeft, this.keyBindBack, this.keyBindRight, this.keyBindJump, this.keyBindSneak, this.keyBindDrop, this.keyBindInventory, this.keyBindChat, this.keyBindPlayerList, this.keyBindPickBlock, this.keyBindCommand, this.keyBindScreenshot, this.keyBindTogglePerspective, this.keyBindSmoothCamera, this.keyBindSprint, this.field_152396_an, this.field_152397_ao, this.field_152398_ap, this.field_152399_aq, this.field_152395_am}, this.keyBindsHotbar);
        this.difficulty = EnumDifficulty.NORMAL;
        this.lastServer = "";
        this.noclipRate = 1.0F;
        this.debugCamRate = 1.0F;
        this.fovSetting = 70.0F;
        this.language = "en_US";
        this.forceUnicodeFont = false;
        this.mc = p_i1016_1_;
        this.optionsFile = new File(p_i1016_2_, "options.txt");
        GameSettings.Options.RENDER_DISTANCE.setValueMax(16.0F);
        this.renderDistanceChunks = p_i1016_1_.isJava64bit() ? 12 : 8;
        this.loadOptions();
    }

    public GameSettings()
    {
        this.chatVisibility = EntityPlayer.EnumChatVisibility.FULL;
        this.chatColours = true;
        this.chatLinks = true;
        this.chatLinksPrompt = true;
        this.chatOpacity = 1.0F;
        this.snooperEnabled = true;
        this.enableVsync = true;
        this.pauseOnLostFocus = true;
        this.showCape = true;
        this.heldItemTooltips = true;
        this.chatScale = 1.0F;
        this.chatWidth = 1.0F;
        this.chatHeightUnfocused = 0.44366196F;
        this.chatHeightFocused = 1.0F;
        this.showInventoryAchievementHint = true;
        this.mipmapLevels = 4;
        this.anisotropicFiltering = 1;
        this.mapSoundLevels = Maps.newEnumMap(SoundCategory.class);
        this.field_152400_J = 0.5F;
        this.field_152401_K = 1.0F;
        this.field_152402_L = 1.0F;
        this.field_152403_M = 0.5412844F;
        this.field_152404_N = 0.31690142F;
        this.field_152405_O = 1;
        this.field_152406_P = true;
        this.field_152407_Q = "";
        this.field_152408_R = 0;
        this.field_152409_S = 0;
        this.field_152410_T = 0;
        this.keyBindForward = new KeyBinding("key.forward", 17, "key.categories.movement");
        this.keyBindLeft = new KeyBinding("key.left", 30, "key.categories.movement");
        this.keyBindBack = new KeyBinding("key.back", 31, "key.categories.movement");
        this.keyBindRight = new KeyBinding("key.right", 32, "key.categories.movement");
        this.keyBindJump = new KeyBinding("key.jump", 57, "key.categories.movement");
        this.keyBindSneak = new KeyBinding("key.sneak", 42, "key.categories.movement");
        this.keyBindInventory = new KeyBinding("key.inventory", 18, "key.categories.inventory");
        this.keyBindUseItem = new KeyBinding("key.use", -99, "key.categories.gameplay");
        this.keyBindDrop = new KeyBinding("key.drop", 16, "key.categories.gameplay");
        this.keyBindAttack = new KeyBinding("key.attack", -100, "key.categories.gameplay");
        this.keyBindPickBlock = new KeyBinding("key.pickItem", -98, "key.categories.gameplay");
        this.keyBindSprint = new KeyBinding("key.sprint", 29, "key.categories.gameplay");
        this.keyBindChat = new KeyBinding("key.chat", 20, "key.categories.multiplayer");
        this.keyBindPlayerList = new KeyBinding("key.playerlist", 15, "key.categories.multiplayer");
        this.keyBindCommand = new KeyBinding("key.command", 53, "key.categories.multiplayer");
        this.keyBindScreenshot = new KeyBinding("key.screenshot", 60, "key.categories.misc");
        this.keyBindTogglePerspective = new KeyBinding("key.togglePerspective", 63, "key.categories.misc");
        this.keyBindSmoothCamera = new KeyBinding("key.smoothCamera", 0, "key.categories.misc");
        this.field_152395_am = new KeyBinding("key.fullscreen", 87, "key.categories.misc");
        this.field_152396_an = new KeyBinding("key.streamStartStop", 64, "key.categories.stream");
        this.field_152397_ao = new KeyBinding("key.streamPauseUnpause", 65, "key.categories.stream");
        this.field_152398_ap = new KeyBinding("key.streamCommercial", 0, "key.categories.stream");
        this.field_152399_aq = new KeyBinding("key.streamToggleMic", 0, "key.categories.stream");
        this.keyBindsHotbar = new KeyBinding[] {new KeyBinding("key.hotbar.1", 2, "key.categories.inventory"), new KeyBinding("key.hotbar.2", 3, "key.categories.inventory"), new KeyBinding("key.hotbar.3", 4, "key.categories.inventory"), new KeyBinding("key.hotbar.4", 5, "key.categories.inventory"), new KeyBinding("key.hotbar.5", 6, "key.categories.inventory"), new KeyBinding("key.hotbar.6", 7, "key.categories.inventory"), new KeyBinding("key.hotbar.7", 8, "key.categories.inventory"), new KeyBinding("key.hotbar.8", 9, "key.categories.inventory"), new KeyBinding("key.hotbar.9", 10, "key.categories.inventory")};
        this.keyBindings = (KeyBinding[])ArrayUtils.addAll(new KeyBinding[] {this.keyBindAttack, this.keyBindUseItem, this.keyBindForward, this.keyBindLeft, this.keyBindBack, this.keyBindRight, this.keyBindJump, this.keyBindSneak, this.keyBindDrop, this.keyBindInventory, this.keyBindChat, this.keyBindPlayerList, this.keyBindPickBlock, this.keyBindCommand, this.keyBindScreenshot, this.keyBindTogglePerspective, this.keyBindSmoothCamera, this.keyBindSprint, this.field_152396_an, this.field_152397_ao, this.field_152398_ap, this.field_152399_aq, this.field_152395_am}, this.keyBindsHotbar);
        this.difficulty = EnumDifficulty.NORMAL;
        this.lastServer = "";
        this.noclipRate = 1.0F;
        this.debugCamRate = 1.0F;
        this.fovSetting = 70.0F;
        this.language = "en_US";
        this.forceUnicodeFont = false;
    }

    /**
     * Represents a key or mouse button as a string. Args: key
     */
    public static String getKeyDisplayString(int p_74298_0_)
    {
        return p_74298_0_ < 0 ? I18n.format("key.mouseButton", new Object[] {Integer.valueOf(p_74298_0_ + 101)}): Keyboard.getKeyName(p_74298_0_);
    }

    /**
     * Returns whether the specified key binding is currently being pressed.
     */
    public static boolean isKeyDown(KeyBinding p_100015_0_)
    {
        return p_100015_0_.getKeyCode() == 0 ? false : (p_100015_0_.getKeyCode() < 0 ? Mouse.isButtonDown(p_100015_0_.getKeyCode() + 100) : Keyboard.isKeyDown(p_100015_0_.getKeyCode()));
    }

    /**
     * Sets a key binding and then saves all settings.
     */
    public void setOptionKeyBinding(KeyBinding p_151440_1_, int p_151440_2_)
    {
        p_151440_1_.setKeyCode(p_151440_2_);
        this.saveOptions();
    }

    /**
     * If the specified option is controlled by a slider (float value), this will set the float value.
     */
    public void setOptionFloatValue(GameSettings.Options p_74304_1_, float p_74304_2_)
    {
        if (p_74304_1_ == GameSettings.Options.SENSITIVITY)
        {
            this.mouseSensitivity = p_74304_2_;
        }

        if (p_74304_1_ == GameSettings.Options.FOV)
        {
            this.fovSetting = p_74304_2_;
        }

        if (p_74304_1_ == GameSettings.Options.GAMMA)
        {
            this.gammaSetting = p_74304_2_;
        }

        if (p_74304_1_ == GameSettings.Options.FRAMERATE_LIMIT)
        {
            this.limitFramerate = (int)p_74304_2_;
        }

        if (p_74304_1_ == GameSettings.Options.CHAT_OPACITY)
        {
            this.chatOpacity = p_74304_2_;
            this.mc.ingameGUI.getChatGUI().refreshChat();
        }

        if (p_74304_1_ == GameSettings.Options.CHAT_HEIGHT_FOCUSED)
        {
            this.chatHeightFocused = p_74304_2_;
            this.mc.ingameGUI.getChatGUI().refreshChat();
        }

        if (p_74304_1_ == GameSettings.Options.CHAT_HEIGHT_UNFOCUSED)
        {
            this.chatHeightUnfocused = p_74304_2_;
            this.mc.ingameGUI.getChatGUI().refreshChat();
        }

        if (p_74304_1_ == GameSettings.Options.CHAT_WIDTH)
        {
            this.chatWidth = p_74304_2_;
            this.mc.ingameGUI.getChatGUI().refreshChat();
        }

        if (p_74304_1_ == GameSettings.Options.CHAT_SCALE)
        {
            this.chatScale = p_74304_2_;
            this.mc.ingameGUI.getChatGUI().refreshChat();
        }

        int i;

        if (p_74304_1_ == GameSettings.Options.ANISOTROPIC_FILTERING)
        {
            i = this.anisotropicFiltering;
            this.anisotropicFiltering = (int)p_74304_2_;

            if ((float)i != p_74304_2_)
            {
                this.mc.getTextureMapBlocks().setAnisotropicFiltering(this.anisotropicFiltering);
                this.mc.scheduleResourcesRefresh();
            }
        }

        if (p_74304_1_ == GameSettings.Options.MIPMAP_LEVELS)
        {
            i = this.mipmapLevels;
            this.mipmapLevels = (int)p_74304_2_;

            if ((float)i != p_74304_2_)
            {
                this.mc.getTextureMapBlocks().setMipmapLevels(this.mipmapLevels);
                this.mc.scheduleResourcesRefresh();
            }
        }

        if (p_74304_1_ == GameSettings.Options.RENDER_DISTANCE)
        {
            this.renderDistanceChunks = (int)p_74304_2_;
        }

        if (p_74304_1_ == GameSettings.Options.STREAM_BYTES_PER_PIXEL)
        {
            this.field_152400_J = p_74304_2_;
        }

        if (p_74304_1_ == GameSettings.Options.STREAM_VOLUME_MIC)
        {
            this.field_152401_K = p_74304_2_;
            this.mc.func_152346_Z().func_152915_s();
        }

        if (p_74304_1_ == GameSettings.Options.STREAM_VOLUME_SYSTEM)
        {
            this.field_152402_L = p_74304_2_;
            this.mc.func_152346_Z().func_152915_s();
        }

        if (p_74304_1_ == GameSettings.Options.STREAM_KBPS)
        {
            this.field_152403_M = p_74304_2_;
        }

        if (p_74304_1_ == GameSettings.Options.STREAM_FPS)
        {
            this.field_152404_N = p_74304_2_;
        }
    }

    /**
     * For non-float options. Toggles the option on/off, or cycles through the list i.e. render distances.
     */
    public void setOptionValue(GameSettings.Options p_74306_1_, int p_74306_2_)
    {
        if (p_74306_1_ == GameSettings.Options.INVERT_MOUSE)
        {
            this.invertMouse = !this.invertMouse;
        }

        if (p_74306_1_ == GameSettings.Options.GUI_SCALE)
        {
            this.guiScale = this.guiScale + p_74306_2_ & 3;
        }

        if (p_74306_1_ == GameSettings.Options.PARTICLES)
        {
            this.particleSetting = (this.particleSetting + p_74306_2_) % 3;
        }

        if (p_74306_1_ == GameSettings.Options.VIEW_BOBBING)
        {
            this.viewBobbing = !this.viewBobbing;
        }

        if (p_74306_1_ == GameSettings.Options.RENDER_CLOUDS)
        {
            this.clouds = !this.clouds;
        }

        if (p_74306_1_ == GameSettings.Options.FORCE_UNICODE_FONT)
        {
            this.forceUnicodeFont = !this.forceUnicodeFont;
            this.mc.fontRenderer.setUnicodeFlag(this.mc.getLanguageManager().isCurrentLocaleUnicode() || this.forceUnicodeFont);
        }

        if (p_74306_1_ == GameSettings.Options.ADVANCED_OPENGL)
        {
            this.advancedOpengl = !this.advancedOpengl;
            this.mc.renderGlobal.loadRenderers();
        }

        if (p_74306_1_ == GameSettings.Options.FBO_ENABLE)
        {
            this.fboEnable = !this.fboEnable;
        }

        if (p_74306_1_ == GameSettings.Options.ANAGLYPH)
        {
            this.anaglyph = !this.anaglyph;
            this.mc.refreshResources();
        }

        if (p_74306_1_ == GameSettings.Options.DIFFICULTY)
        {
            this.difficulty = EnumDifficulty.getDifficultyEnum(this.difficulty.getDifficultyId() + p_74306_2_ & 3);
        }

        if (p_74306_1_ == GameSettings.Options.GRAPHICS)
        {
            this.fancyGraphics = !this.fancyGraphics;
            this.mc.renderGlobal.loadRenderers();
        }

        if (p_74306_1_ == GameSettings.Options.AMBIENT_OCCLUSION)
        {
            this.ambientOcclusion = (this.ambientOcclusion + p_74306_2_) % 3;
            this.mc.renderGlobal.loadRenderers();
        }

        if (p_74306_1_ == GameSettings.Options.CHAT_VISIBILITY)
        {
            this.chatVisibility = EntityPlayer.EnumChatVisibility.getEnumChatVisibility((this.chatVisibility.getChatVisibility() + p_74306_2_) % 3);
        }

        if (p_74306_1_ == GameSettings.Options.STREAM_COMPRESSION)
        {
            this.field_152405_O = (this.field_152405_O + p_74306_2_) % 3;
        }

        if (p_74306_1_ == GameSettings.Options.STREAM_SEND_METADATA)
        {
            this.field_152406_P = !this.field_152406_P;
        }

        if (p_74306_1_ == GameSettings.Options.STREAM_CHAT_ENABLED)
        {
            this.field_152408_R = (this.field_152408_R + p_74306_2_) % 3;
        }

        if (p_74306_1_ == GameSettings.Options.STREAM_CHAT_USER_FILTER)
        {
            this.field_152409_S = (this.field_152409_S + p_74306_2_) % 3;
        }

        if (p_74306_1_ == GameSettings.Options.STREAM_MIC_TOGGLE_BEHAVIOR)
        {
            this.field_152410_T = (this.field_152410_T + p_74306_2_) % 2;
        }

        if (p_74306_1_ == GameSettings.Options.CHAT_COLOR)
        {
            this.chatColours = !this.chatColours;
        }

        if (p_74306_1_ == GameSettings.Options.CHAT_LINKS)
        {
            this.chatLinks = !this.chatLinks;
        }

        if (p_74306_1_ == GameSettings.Options.CHAT_LINKS_PROMPT)
        {
            this.chatLinksPrompt = !this.chatLinksPrompt;
        }

        if (p_74306_1_ == GameSettings.Options.SNOOPER_ENABLED)
        {
            this.snooperEnabled = !this.snooperEnabled;
        }

        if (p_74306_1_ == GameSettings.Options.SHOW_CAPE)
        {
            this.showCape = !this.showCape;
        }

        if (p_74306_1_ == GameSettings.Options.TOUCHSCREEN)
        {
            this.touchscreen = !this.touchscreen;
        }

        if (p_74306_1_ == GameSettings.Options.USE_FULLSCREEN)
        {
            this.fullScreen = !this.fullScreen;

            if (this.mc.isFullScreen() != this.fullScreen)
            {
                this.mc.toggleFullscreen();
            }
        }

        if (p_74306_1_ == GameSettings.Options.ENABLE_VSYNC)
        {
            this.enableVsync = !this.enableVsync;
            Display.setVSyncEnabled(this.enableVsync);
        }

        this.saveOptions();
    }

    public float getOptionFloatValue(GameSettings.Options p_74296_1_)
    {
        return p_74296_1_ == GameSettings.Options.FOV ? this.fovSetting : (p_74296_1_ == GameSettings.Options.GAMMA ? this.gammaSetting : (p_74296_1_ == GameSettings.Options.SATURATION ? this.saturation : (p_74296_1_ == GameSettings.Options.SENSITIVITY ? this.mouseSensitivity : (p_74296_1_ == GameSettings.Options.CHAT_OPACITY ? this.chatOpacity : (p_74296_1_ == GameSettings.Options.CHAT_HEIGHT_FOCUSED ? this.chatHeightFocused : (p_74296_1_ == GameSettings.Options.CHAT_HEIGHT_UNFOCUSED ? this.chatHeightUnfocused : (p_74296_1_ == GameSettings.Options.CHAT_SCALE ? this.chatScale : (p_74296_1_ == GameSettings.Options.CHAT_WIDTH ? this.chatWidth : (p_74296_1_ == GameSettings.Options.FRAMERATE_LIMIT ? (float)this.limitFramerate : (p_74296_1_ == GameSettings.Options.ANISOTROPIC_FILTERING ? (float)this.anisotropicFiltering : (p_74296_1_ == GameSettings.Options.MIPMAP_LEVELS ? (float)this.mipmapLevels : (p_74296_1_ == GameSettings.Options.RENDER_DISTANCE ? (float)this.renderDistanceChunks : (p_74296_1_ == GameSettings.Options.STREAM_BYTES_PER_PIXEL ? this.field_152400_J : (p_74296_1_ == GameSettings.Options.STREAM_VOLUME_MIC ? this.field_152401_K : (p_74296_1_ == GameSettings.Options.STREAM_VOLUME_SYSTEM ? this.field_152402_L : (p_74296_1_ == GameSettings.Options.STREAM_KBPS ? this.field_152403_M : (p_74296_1_ == GameSettings.Options.STREAM_FPS ? this.field_152404_N : 0.0F)))))))))))))))));
    }

    public boolean getOptionOrdinalValue(GameSettings.Options p_74308_1_)
    {
        switch (GameSettings.SwitchOptions.optionIds[p_74308_1_.ordinal()])
        {
            case 1:
                return this.invertMouse;
            case 2:
                return this.viewBobbing;
            case 3:
                return this.anaglyph;
            case 4:
                return this.advancedOpengl;
            case 5:
                return this.fboEnable;
            case 6:
                return this.clouds;
            case 7:
                return this.chatColours;
            case 8:
                return this.chatLinks;
            case 9:
                return this.chatLinksPrompt;
            case 10:
                return this.snooperEnabled;
            case 11:
                return this.fullScreen;
            case 12:
                return this.enableVsync;
            case 13:
                return this.showCape;
            case 14:
                return this.touchscreen;
            case 15:
                return this.field_152406_P;
            case 16:
                return this.forceUnicodeFont;
            default:
                return false;
        }
    }

    /**
     * Returns the translation of the given index in the given String array. If the index is smaller than 0 or greater
     * than/equal to the length of the String array, it is changed to 0.
     */
    private static String getTranslation(String[] p_74299_0_, int p_74299_1_)
    {
        if (p_74299_1_ < 0 || p_74299_1_ >= p_74299_0_.length)
        {
            p_74299_1_ = 0;
        }

        return I18n.format(p_74299_0_[p_74299_1_], new Object[0]);
    }

    /**
     * Gets a key binding.
     */
    public String getKeyBinding(GameSettings.Options p_74297_1_)
    {
        String s = I18n.format(p_74297_1_.getEnumString(), new Object[0]) + ": ";

        if (p_74297_1_.getEnumFloat())
        {
            float f1 = this.getOptionFloatValue(p_74297_1_);
            float f = p_74297_1_.normalizeValue(f1);
            return p_74297_1_ == GameSettings.Options.SENSITIVITY ? (f == 0.0F ? s + I18n.format("options.sensitivity.min", new Object[0]) : (f == 1.0F ? s + I18n.format("options.sensitivity.max", new Object[0]) : s + (int)(f * 200.0F) + "%")) : (p_74297_1_ == GameSettings.Options.FOV ? (f1 == 70.0F ? s + I18n.format("options.fov.min", new Object[0]) : (f1 == 110.0F ? s + I18n.format("options.fov.max", new Object[0]) : s + (int)f1)) : (p_74297_1_ == GameSettings.Options.FRAMERATE_LIMIT ? (f1 == p_74297_1_.valueMax ? s + I18n.format("options.framerateLimit.max", new Object[0]) : s + (int)f1 + " fps") : (p_74297_1_ == GameSettings.Options.GAMMA ? (f == 0.0F ? s + I18n.format("options.gamma.min", new Object[0]) : (f == 1.0F ? s + I18n.format("options.gamma.max", new Object[0]) : s + "+" + (int)(f * 100.0F) + "%")) : (p_74297_1_ == GameSettings.Options.SATURATION ? s + (int)(f * 400.0F) + "%" : (p_74297_1_ == GameSettings.Options.CHAT_OPACITY ? s + (int)(f * 90.0F + 10.0F) + "%" : (p_74297_1_ == GameSettings.Options.CHAT_HEIGHT_UNFOCUSED ? s + GuiNewChat.func_146243_b(f) + "px" : (p_74297_1_ == GameSettings.Options.CHAT_HEIGHT_FOCUSED ? s + GuiNewChat.func_146243_b(f) + "px" : (p_74297_1_ == GameSettings.Options.CHAT_WIDTH ? s + GuiNewChat.func_146233_a(f) + "px" : (p_74297_1_ == GameSettings.Options.RENDER_DISTANCE ? s + (int)f1 + " chunks" : (p_74297_1_ == GameSettings.Options.ANISOTROPIC_FILTERING ? (f1 == 1.0F ? s + I18n.format("options.off", new Object[0]) : s + (int)f1) : (p_74297_1_ == GameSettings.Options.MIPMAP_LEVELS ? (f1 == 0.0F ? s + I18n.format("options.off", new Object[0]) : s + (int)f1) : (p_74297_1_ == GameSettings.Options.STREAM_FPS ? s + TwitchStream.func_152948_a(f) + " fps" : (p_74297_1_ == GameSettings.Options.STREAM_KBPS ? s + TwitchStream.func_152946_b(f) + " Kbps" : (p_74297_1_ == GameSettings.Options.STREAM_BYTES_PER_PIXEL ? s + String.format("%.3f bpp", new Object[] {Float.valueOf(TwitchStream.func_152947_c(f))}): (f == 0.0F ? s + I18n.format("options.off", new Object[0]) : s + (int)(f * 100.0F) + "%")))))))))))))));
        }
        else if (p_74297_1_.getEnumBoolean())
        {
            boolean flag = this.getOptionOrdinalValue(p_74297_1_);
            return flag ? s + I18n.format("options.on", new Object[0]) : s + I18n.format("options.off", new Object[0]);
        }
        else if (p_74297_1_ == GameSettings.Options.DIFFICULTY)
        {
            return s + I18n.format(this.difficulty.getDifficultyResourceKey(), new Object[0]);
        }
        else if (p_74297_1_ == GameSettings.Options.GUI_SCALE)
        {
            return s + getTranslation(GUISCALES, this.guiScale);
        }
        else if (p_74297_1_ == GameSettings.Options.CHAT_VISIBILITY)
        {
            return s + I18n.format(this.chatVisibility.getResourceKey(), new Object[0]);
        }
        else if (p_74297_1_ == GameSettings.Options.PARTICLES)
        {
            return s + getTranslation(PARTICLES, this.particleSetting);
        }
        else if (p_74297_1_ == GameSettings.Options.AMBIENT_OCCLUSION)
        {
            return s + getTranslation(AMBIENT_OCCLUSIONS, this.ambientOcclusion);
        }
        else if (p_74297_1_ == GameSettings.Options.STREAM_COMPRESSION)
        {
            return s + getTranslation(field_152391_aS, this.field_152405_O);
        }
        else if (p_74297_1_ == GameSettings.Options.STREAM_CHAT_ENABLED)
        {
            return s + getTranslation(field_152392_aT, this.field_152408_R);
        }
        else if (p_74297_1_ == GameSettings.Options.STREAM_CHAT_USER_FILTER)
        {
            return s + getTranslation(field_152393_aU, this.field_152409_S);
        }
        else if (p_74297_1_ == GameSettings.Options.STREAM_MIC_TOGGLE_BEHAVIOR)
        {
            return s + getTranslation(field_152394_aV, this.field_152410_T);
        }
        else if (p_74297_1_ == GameSettings.Options.GRAPHICS)
        {
            if (this.fancyGraphics)
            {
                return s + I18n.format("options.graphics.fancy", new Object[0]);
            }
            else
            {
                String s1 = "options.graphics.fast";
                return s + I18n.format("options.graphics.fast", new Object[0]);
            }
        }
        else
        {
            return s;
        }
    }

    /**
     * Loads the options from the options file. It appears that this has replaced the previous 'loadOptions'
     */
    public void loadOptions()
    {
        try
        {
            if (!this.optionsFile.exists())
            {
                return;
            }

            BufferedReader bufferedreader = new BufferedReader(new FileReader(this.optionsFile));
            String s = "";
            this.mapSoundLevels.clear();

            while ((s = bufferedreader.readLine()) != null)
            {
                try
                {
                    String[] astring = s.split(":");

                    if (astring[0].equals("mouseSensitivity"))
                    {
                        this.mouseSensitivity = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("invertYMouse"))
                    {
                        this.invertMouse = astring[1].equals("true");
                    }

                    if (astring[0].equals("fov"))
                    {
                        this.fovSetting = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("gamma"))
                    {
                        this.gammaSetting = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("saturation"))
                    {
                        this.saturation = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("fov"))
                    {
                        this.fovSetting = this.parseFloat(astring[1]) * 40.0F + 70.0F;
                    }

                    if (astring[0].equals("renderDistance"))
                    {
                        this.renderDistanceChunks = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("guiScale"))
                    {
                        this.guiScale = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("particles"))
                    {
                        this.particleSetting = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("bobView"))
                    {
                        this.viewBobbing = astring[1].equals("true");
                    }

                    if (astring[0].equals("anaglyph3d"))
                    {
                        this.anaglyph = astring[1].equals("true");
                    }

                    if (astring[0].equals("advancedOpengl"))
                    {
                        this.advancedOpengl = astring[1].equals("true");
                    }

                    if (astring[0].equals("maxFps"))
                    {
                        this.limitFramerate = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("fboEnable"))
                    {
                        this.fboEnable = astring[1].equals("true");
                    }

                    if (astring[0].equals("difficulty"))
                    {
                        this.difficulty = EnumDifficulty.getDifficultyEnum(Integer.parseInt(astring[1]));
                    }

                    if (astring[0].equals("fancyGraphics"))
                    {
                        this.fancyGraphics = astring[1].equals("true");
                    }

                    if (astring[0].equals("ao"))
                    {
                        if (astring[1].equals("true"))
                        {
                            this.ambientOcclusion = 2;
                        }
                        else if (astring[1].equals("false"))
                        {
                            this.ambientOcclusion = 0;
                        }
                        else
                        {
                            this.ambientOcclusion = Integer.parseInt(astring[1]);
                        }
                    }

                    if (astring[0].equals("clouds"))
                    {
                        this.clouds = astring[1].equals("true");
                    }

                    if (astring[0].equals("resourcePacks"))
                    {
                        this.resourcePacks = (List)gson.fromJson(s.substring(s.indexOf(58) + 1), typeListString);

                        if (this.resourcePacks == null)
                        {
                            this.resourcePacks = new ArrayList();
                        }
                    }

                    if (astring[0].equals("lastServer") && astring.length >= 2)
                    {
                        this.lastServer = s.substring(s.indexOf(58) + 1);
                    }

                    if (astring[0].equals("lang") && astring.length >= 2)
                    {
                        this.language = astring[1];
                    }

                    if (astring[0].equals("chatVisibility"))
                    {
                        this.chatVisibility = EntityPlayer.EnumChatVisibility.getEnumChatVisibility(Integer.parseInt(astring[1]));
                    }

                    if (astring[0].equals("chatColors"))
                    {
                        this.chatColours = astring[1].equals("true");
                    }

                    if (astring[0].equals("chatLinks"))
                    {
                        this.chatLinks = astring[1].equals("true");
                    }

                    if (astring[0].equals("chatLinksPrompt"))
                    {
                        this.chatLinksPrompt = astring[1].equals("true");
                    }

                    if (astring[0].equals("chatOpacity"))
                    {
                        this.chatOpacity = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("snooperEnabled"))
                    {
                        this.snooperEnabled = astring[1].equals("true");
                    }

                    if (astring[0].equals("fullscreen"))
                    {
                        this.fullScreen = astring[1].equals("true");
                    }

                    if (astring[0].equals("enableVsync"))
                    {
                        this.enableVsync = astring[1].equals("true");
                    }

                    if (astring[0].equals("hideServerAddress"))
                    {
                        this.hideServerAddress = astring[1].equals("true");
                    }

                    if (astring[0].equals("advancedItemTooltips"))
                    {
                        this.advancedItemTooltips = astring[1].equals("true");
                    }

                    if (astring[0].equals("pauseOnLostFocus"))
                    {
                        this.pauseOnLostFocus = astring[1].equals("true");
                    }

                    if (astring[0].equals("showCape"))
                    {
                        this.showCape = astring[1].equals("true");
                    }

                    if (astring[0].equals("touchscreen"))
                    {
                        this.touchscreen = astring[1].equals("true");
                    }

                    if (astring[0].equals("overrideHeight"))
                    {
                        this.overrideHeight = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("overrideWidth"))
                    {
                        this.overrideWidth = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("heldItemTooltips"))
                    {
                        this.heldItemTooltips = astring[1].equals("true");
                    }

                    if (astring[0].equals("chatHeightFocused"))
                    {
                        this.chatHeightFocused = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("chatHeightUnfocused"))
                    {
                        this.chatHeightUnfocused = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("chatScale"))
                    {
                        this.chatScale = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("chatWidth"))
                    {
                        this.chatWidth = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("showInventoryAchievementHint"))
                    {
                        this.showInventoryAchievementHint = astring[1].equals("true");
                    }

                    if (astring[0].equals("mipmapLevels"))
                    {
                        this.mipmapLevels = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("anisotropicFiltering"))
                    {
                        this.anisotropicFiltering = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("streamBytesPerPixel"))
                    {
                        this.field_152400_J = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("streamMicVolume"))
                    {
                        this.field_152401_K = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("streamSystemVolume"))
                    {
                        this.field_152402_L = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("streamKbps"))
                    {
                        this.field_152403_M = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("streamFps"))
                    {
                        this.field_152404_N = this.parseFloat(astring[1]);
                    }

                    if (astring[0].equals("streamCompression"))
                    {
                        this.field_152405_O = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("streamSendMetadata"))
                    {
                        this.field_152406_P = astring[1].equals("true");
                    }

                    if (astring[0].equals("streamPreferredServer") && astring.length >= 2)
                    {
                        this.field_152407_Q = s.substring(s.indexOf(58) + 1);
                    }

                    if (astring[0].equals("streamChatEnabled"))
                    {
                        this.field_152408_R = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("streamChatUserFilter"))
                    {
                        this.field_152409_S = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("streamMicToggleBehavior"))
                    {
                        this.field_152410_T = Integer.parseInt(astring[1]);
                    }

                    if (astring[0].equals("forceUnicodeFont"))
                    {
                        this.forceUnicodeFont = astring[1].equals("true");
                    }

                    KeyBinding[] akeybinding = this.keyBindings;
                    int i = akeybinding.length;
                    int j;

                    for (j = 0; j < i; ++j)
                    {
                        KeyBinding keybinding = akeybinding[j];

                        if (astring[0].equals("key_" + keybinding.getKeyDescription()))
                        {
                            keybinding.setKeyCode(Integer.parseInt(astring[1]));
                        }
                    }

                    SoundCategory[] asoundcategory = SoundCategory.values();
                    i = asoundcategory.length;

                    for (j = 0; j < i; ++j)
                    {
                        SoundCategory soundcategory = asoundcategory[j];

                        if (astring[0].equals("soundCategory_" + soundcategory.getCategoryName()))
                        {
                            this.mapSoundLevels.put(soundcategory, Float.valueOf(this.parseFloat(astring[1])));
                        }
                    }
                }
                catch (Exception exception)
                {
                    logger.warn("Skipping bad option: " + s);
                }
            }

            KeyBinding.resetKeyBindingArrayAndHash();
            bufferedreader.close();
        }
        catch (Exception exception1)
        {
            logger.error("Failed to load options", exception1);
        }
    }

    /**
     * Parses a string into a float.
     */
    private float parseFloat(String p_74305_1_)
    {
        return p_74305_1_.equals("true") ? 1.0F : (p_74305_1_.equals("false") ? 0.0F : Float.parseFloat(p_74305_1_));
    }

    /**
     * Saves the options to the options file.
     */
    public void saveOptions()
    {
        if (FMLClientHandler.instance().isLoading()) return;
        try
        {
            PrintWriter printwriter = new PrintWriter(new FileWriter(this.optionsFile));
            printwriter.println("invertYMouse:" + this.invertMouse);
            printwriter.println("mouseSensitivity:" + this.mouseSensitivity);
            printwriter.println("fov:" + (this.fovSetting - 70.0F) / 40.0F);
            printwriter.println("gamma:" + this.gammaSetting);
            printwriter.println("saturation:" + this.saturation);
            printwriter.println("renderDistance:" + this.renderDistanceChunks);
            printwriter.println("guiScale:" + this.guiScale);
            printwriter.println("particles:" + this.particleSetting);
            printwriter.println("bobView:" + this.viewBobbing);
            printwriter.println("anaglyph3d:" + this.anaglyph);
            printwriter.println("advancedOpengl:" + this.advancedOpengl);
            printwriter.println("maxFps:" + this.limitFramerate);
            printwriter.println("fboEnable:" + this.fboEnable);
            printwriter.println("difficulty:" + this.difficulty.getDifficultyId());
            printwriter.println("fancyGraphics:" + this.fancyGraphics);
            printwriter.println("ao:" + this.ambientOcclusion);
            printwriter.println("clouds:" + this.clouds);
            printwriter.println("resourcePacks:" + gson.toJson(this.resourcePacks));
            printwriter.println("lastServer:" + this.lastServer);
            printwriter.println("lang:" + this.language);
            printwriter.println("chatVisibility:" + this.chatVisibility.getChatVisibility());
            printwriter.println("chatColors:" + this.chatColours);
            printwriter.println("chatLinks:" + this.chatLinks);
            printwriter.println("chatLinksPrompt:" + this.chatLinksPrompt);
            printwriter.println("chatOpacity:" + this.chatOpacity);
            printwriter.println("snooperEnabled:" + this.snooperEnabled);
            printwriter.println("fullscreen:" + this.fullScreen);
            printwriter.println("enableVsync:" + this.enableVsync);
            printwriter.println("hideServerAddress:" + this.hideServerAddress);
            printwriter.println("advancedItemTooltips:" + this.advancedItemTooltips);
            printwriter.println("pauseOnLostFocus:" + this.pauseOnLostFocus);
            printwriter.println("showCape:" + this.showCape);
            printwriter.println("touchscreen:" + this.touchscreen);
            printwriter.println("overrideWidth:" + this.overrideWidth);
            printwriter.println("overrideHeight:" + this.overrideHeight);
            printwriter.println("heldItemTooltips:" + this.heldItemTooltips);
            printwriter.println("chatHeightFocused:" + this.chatHeightFocused);
            printwriter.println("chatHeightUnfocused:" + this.chatHeightUnfocused);
            printwriter.println("chatScale:" + this.chatScale);
            printwriter.println("chatWidth:" + this.chatWidth);
            printwriter.println("showInventoryAchievementHint:" + this.showInventoryAchievementHint);
            printwriter.println("mipmapLevels:" + this.mipmapLevels);
            printwriter.println("anisotropicFiltering:" + this.anisotropicFiltering);
            printwriter.println("streamBytesPerPixel:" + this.field_152400_J);
            printwriter.println("streamMicVolume:" + this.field_152401_K);
            printwriter.println("streamSystemVolume:" + this.field_152402_L);
            printwriter.println("streamKbps:" + this.field_152403_M);
            printwriter.println("streamFps:" + this.field_152404_N);
            printwriter.println("streamCompression:" + this.field_152405_O);
            printwriter.println("streamSendMetadata:" + this.field_152406_P);
            printwriter.println("streamPreferredServer:" + this.field_152407_Q);
            printwriter.println("streamChatEnabled:" + this.field_152408_R);
            printwriter.println("streamChatUserFilter:" + this.field_152409_S);
            printwriter.println("streamMicToggleBehavior:" + this.field_152410_T);
            printwriter.println("forceUnicodeFont:" + this.forceUnicodeFont);
            KeyBinding[] akeybinding = this.keyBindings;
            int i = akeybinding.length;
            int j;

            for (j = 0; j < i; ++j)
            {
                KeyBinding keybinding = akeybinding[j];
                printwriter.println("key_" + keybinding.getKeyDescription() + ":" + keybinding.getKeyCode());
            }

            SoundCategory[] asoundcategory = SoundCategory.values();
            i = asoundcategory.length;

            for (j = 0; j < i; ++j)
            {
                SoundCategory soundcategory = asoundcategory[j];
                printwriter.println("soundCategory_" + soundcategory.getCategoryName() + ":" + this.getSoundLevel(soundcategory));
            }

            printwriter.close();
        }
        catch (Exception exception)
        {
            logger.error("Failed to save options", exception);
        }

        this.sendSettingsToServer();
    }

    public float getSoundLevel(SoundCategory p_151438_1_)
    {
        return this.mapSoundLevels.containsKey(p_151438_1_) ? ((Float)this.mapSoundLevels.get(p_151438_1_)).floatValue() : 1.0F;
    }

    public void setSoundLevel(SoundCategory p_151439_1_, float p_151439_2_)
    {
        this.mc.getSoundHandler().setSoundLevel(p_151439_1_, p_151439_2_);
        this.mapSoundLevels.put(p_151439_1_, Float.valueOf(p_151439_2_));
    }

    /**
     * Send a client info packet with settings information to the server
     */
    public void sendSettingsToServer()
    {
        if (this.mc.thePlayer != null)
        {
            this.mc.thePlayer.sendQueue.addToSendQueue(new C15PacketClientSettings(this.language, this.renderDistanceChunks, this.chatVisibility, this.chatColours, this.difficulty, this.showCape));
        }
    }

    /**
     * Should render clouds
     */
    public boolean shouldRenderClouds()
    {
        return this.renderDistanceChunks >= 4 && this.clouds;
    }

    @SideOnly(Side.CLIENT)
    public static enum Options
    {
        INVERT_MOUSE("options.invertMouse", false, true),
        SENSITIVITY("options.sensitivity", true, false),
        FOV("options.fov", true, false, 30.0F, 110.0F, 1.0F),
        GAMMA("options.gamma", true, false),
        SATURATION("options.saturation", true, false),
        RENDER_DISTANCE("options.renderDistance", true, false, 2.0F, 16.0F, 1.0F),
        VIEW_BOBBING("options.viewBobbing", false, true),
        ANAGLYPH("options.anaglyph", false, true),
        ADVANCED_OPENGL("options.advancedOpengl", false, true),
        FRAMERATE_LIMIT("options.framerateLimit", true, false, 10.0F, 260.0F, 10.0F),
        FBO_ENABLE("options.fboEnable", false, true),
        DIFFICULTY("options.difficulty", false, false),
        GRAPHICS("options.graphics", false, false),
        AMBIENT_OCCLUSION("options.ao", false, false),
        GUI_SCALE("options.guiScale", false, false),
        RENDER_CLOUDS("options.renderClouds", false, true),
        PARTICLES("options.particles", false, false),
        CHAT_VISIBILITY("options.chat.visibility", false, false),
        CHAT_COLOR("options.chat.color", false, true),
        CHAT_LINKS("options.chat.links", false, true),
        CHAT_OPACITY("options.chat.opacity", true, false),
        CHAT_LINKS_PROMPT("options.chat.links.prompt", false, true),
        SNOOPER_ENABLED("options.snooper", false, true),
        USE_FULLSCREEN("options.fullscreen", false, true),
        ENABLE_VSYNC("options.vsync", false, true),
        SHOW_CAPE("options.showCape", false, true),
        TOUCHSCREEN("options.touchscreen", false, true),
        CHAT_SCALE("options.chat.scale", true, false),
        CHAT_WIDTH("options.chat.width", true, false),
        CHAT_HEIGHT_FOCUSED("options.chat.height.focused", true, false),
        CHAT_HEIGHT_UNFOCUSED("options.chat.height.unfocused", true, false),
        MIPMAP_LEVELS("options.mipmapLevels", true, false, 0.0F, 4.0F, 1.0F),
        ANISOTROPIC_FILTERING("options.anisotropicFiltering", true, false, 1.0F, 16.0F, 0.0F)
        {
            private static final String __OBFID = "CL_00000654";
            protected float snapToStep(float p_148264_1_)
            {
                return (float)MathHelper.roundUpToPowerOfTwo((int)p_148264_1_);
            }
        },
        FORCE_UNICODE_FONT("options.forceUnicodeFont", false, true),
        STREAM_BYTES_PER_PIXEL("options.stream.bytesPerPixel", true, false),
        STREAM_VOLUME_MIC("options.stream.micVolumne", true, false),
        STREAM_VOLUME_SYSTEM("options.stream.systemVolume", true, false),
        STREAM_KBPS("options.stream.kbps", true, false),
        STREAM_FPS("options.stream.fps", true, false),
        STREAM_COMPRESSION("options.stream.compression", false, false),
        STREAM_SEND_METADATA("options.stream.sendMetadata", false, true),
        STREAM_CHAT_ENABLED("options.stream.chat.enabled", false, false),
        STREAM_CHAT_USER_FILTER("options.stream.chat.userFilter", false, false),
        STREAM_MIC_TOGGLE_BEHAVIOR("options.stream.micToggleBehavior", false, false);
        private final boolean enumFloat;
        private final boolean enumBoolean;
        private final String enumString;
        private final float valueStep;
        private float valueMin;
        private float valueMax;

        private static final String __OBFID = "CL_00000653";

        public static GameSettings.Options getEnumOptions(int p_74379_0_)
        {
            GameSettings.Options[] aoptions = values();
            int j = aoptions.length;

            for (int k = 0; k < j; ++k)
            {
                GameSettings.Options options = aoptions[k];

                if (options.returnEnumOrdinal() == p_74379_0_)
                {
                    return options;
                }
            }

            return null;
        }

        private Options(String p_i1015_3_, boolean p_i1015_4_, boolean p_i1015_5_)
        {
            this(p_i1015_3_, p_i1015_4_, p_i1015_5_, 0.0F, 1.0F, 0.0F);
        }

        private Options(String p_i45004_3_, boolean p_i45004_4_, boolean p_i45004_5_, float p_i45004_6_, float p_i45004_7_, float p_i45004_8_)
        {
            this.enumString = p_i45004_3_;
            this.enumFloat = p_i45004_4_;
            this.enumBoolean = p_i45004_5_;
            this.valueMin = p_i45004_6_;
            this.valueMax = p_i45004_7_;
            this.valueStep = p_i45004_8_;
        }

        public boolean getEnumFloat()
        {
            return this.enumFloat;
        }

        public boolean getEnumBoolean()
        {
            return this.enumBoolean;
        }

        public int returnEnumOrdinal()
        {
            return this.ordinal();
        }

        public String getEnumString()
        {
            return this.enumString;
        }

        public float getValueMax()
        {
            return this.valueMax;
        }

        public void setValueMax(float p_148263_1_)
        {
            this.valueMax = p_148263_1_;
        }

        public float normalizeValue(float p_148266_1_)
        {
            return MathHelper.clamp_float((this.snapToStepClamp(p_148266_1_) - this.valueMin) / (this.valueMax - this.valueMin), 0.0F, 1.0F);
        }

        public float denormalizeValue(float p_148262_1_)
        {
            return this.snapToStepClamp(this.valueMin + (this.valueMax - this.valueMin) * MathHelper.clamp_float(p_148262_1_, 0.0F, 1.0F));
        }

        public float snapToStepClamp(float p_148268_1_)
        {
            p_148268_1_ = this.snapToStep(p_148268_1_);
            return MathHelper.clamp_float(p_148268_1_, this.valueMin, this.valueMax);
        }

        protected float snapToStep(float p_148264_1_)
        {
            if (this.valueStep > 0.0F)
            {
                p_148264_1_ = this.valueStep * (float)Math.round(p_148264_1_ / this.valueStep);
            }

            return p_148264_1_;
        }

        Options(String p_i45005_3_, boolean p_i45005_4_, boolean p_i45005_5_, float p_i45005_6_, float p_i45005_7_, float p_i45005_8_, Object p_i45005_9_)
        {
            this(p_i45005_3_, p_i45005_4_, p_i45005_5_, p_i45005_6_, p_i45005_7_, p_i45005_8_);
        }
    }

    @SideOnly(Side.CLIENT)

    static final class SwitchOptions
        {
            static final int[] optionIds = new int[GameSettings.Options.values().length];
            private static final String __OBFID = "CL_00000652";

            static
            {
                try
                {
                    optionIds[GameSettings.Options.INVERT_MOUSE.ordinal()] = 1;
                }
                catch (NoSuchFieldError var16)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.VIEW_BOBBING.ordinal()] = 2;
                }
                catch (NoSuchFieldError var15)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.ANAGLYPH.ordinal()] = 3;
                }
                catch (NoSuchFieldError var14)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.ADVANCED_OPENGL.ordinal()] = 4;
                }
                catch (NoSuchFieldError var13)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.FBO_ENABLE.ordinal()] = 5;
                }
                catch (NoSuchFieldError var12)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.RENDER_CLOUDS.ordinal()] = 6;
                }
                catch (NoSuchFieldError var11)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.CHAT_COLOR.ordinal()] = 7;
                }
                catch (NoSuchFieldError var10)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.CHAT_LINKS.ordinal()] = 8;
                }
                catch (NoSuchFieldError var9)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.CHAT_LINKS_PROMPT.ordinal()] = 9;
                }
                catch (NoSuchFieldError var8)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.SNOOPER_ENABLED.ordinal()] = 10;
                }
                catch (NoSuchFieldError var7)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.USE_FULLSCREEN.ordinal()] = 11;
                }
                catch (NoSuchFieldError var6)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.ENABLE_VSYNC.ordinal()] = 12;
                }
                catch (NoSuchFieldError var5)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.SHOW_CAPE.ordinal()] = 13;
                }
                catch (NoSuchFieldError var4)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.TOUCHSCREEN.ordinal()] = 14;
                }
                catch (NoSuchFieldError var3)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.STREAM_SEND_METADATA.ordinal()] = 15;
                }
                catch (NoSuchFieldError var2)
                {
                    ;
                }

                try
                {
                    optionIds[GameSettings.Options.FORCE_UNICODE_FONT.ordinal()] = 16;
                }
                catch (NoSuchFieldError var1)
                {
                    ;
                }
            }
        }
}