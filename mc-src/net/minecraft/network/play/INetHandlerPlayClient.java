package net.minecraft.network.play;

import net.minecraft.network.INetHandler;
import net.minecraft.network.play.server.S00PacketKeepAlive;
import net.minecraft.network.play.server.S01PacketJoinGame;
import net.minecraft.network.play.server.S02PacketChat;
import net.minecraft.network.play.server.S03PacketTimeUpdate;
import net.minecraft.network.play.server.S04PacketEntityEquipment;
import net.minecraft.network.play.server.S05PacketSpawnPosition;
import net.minecraft.network.play.server.S06PacketUpdateHealth;
import net.minecraft.network.play.server.S07PacketRespawn;
import net.minecraft.network.play.server.S08PacketPlayerPosLook;
import net.minecraft.network.play.server.S09PacketHeldItemChange;
import net.minecraft.network.play.server.S0APacketUseBed;
import net.minecraft.network.play.server.S0BPacketAnimation;
import net.minecraft.network.play.server.S0CPacketSpawnPlayer;
import net.minecraft.network.play.server.S0DPacketCollectItem;
import net.minecraft.network.play.server.S0EPacketSpawnObject;
import net.minecraft.network.play.server.S0FPacketSpawnMob;
import net.minecraft.network.play.server.S10PacketSpawnPainting;
import net.minecraft.network.play.server.S11PacketSpawnExperienceOrb;
import net.minecraft.network.play.server.S12PacketEntityVelocity;
import net.minecraft.network.play.server.S13PacketDestroyEntities;
import net.minecraft.network.play.server.S14PacketEntity;
import net.minecraft.network.play.server.S18PacketEntityTeleport;
import net.minecraft.network.play.server.S19PacketEntityHeadLook;
import net.minecraft.network.play.server.S19PacketEntityStatus;
import net.minecraft.network.play.server.S1BPacketEntityAttach;
import net.minecraft.network.play.server.S1CPacketEntityMetadata;
import net.minecraft.network.play.server.S1DPacketEntityEffect;
import net.minecraft.network.play.server.S1EPacketRemoveEntityEffect;
import net.minecraft.network.play.server.S1FPacketSetExperience;
import net.minecraft.network.play.server.S20PacketEntityProperties;
import net.minecraft.network.play.server.S21PacketChunkData;
import net.minecraft.network.play.server.S22PacketMultiBlockChange;
import net.minecraft.network.play.server.S23PacketBlockChange;
import net.minecraft.network.play.server.S24PacketBlockAction;
import net.minecraft.network.play.server.S25PacketBlockBreakAnim;
import net.minecraft.network.play.server.S26PacketMapChunkBulk;
import net.minecraft.network.play.server.S27PacketExplosion;
import net.minecraft.network.play.server.S28PacketEffect;
import net.minecraft.network.play.server.S29PacketSoundEffect;
import net.minecraft.network.play.server.S2APacketParticles;
import net.minecraft.network.play.server.S2BPacketChangeGameState;
import net.minecraft.network.play.server.S2CPacketSpawnGlobalEntity;
import net.minecraft.network.play.server.S2DPacketOpenWindow;
import net.minecraft.network.play.server.S2EPacketCloseWindow;
import net.minecraft.network.play.server.S2FPacketSetSlot;
import net.minecraft.network.play.server.S30PacketWindowItems;
import net.minecraft.network.play.server.S31PacketWindowProperty;
import net.minecraft.network.play.server.S32PacketConfirmTransaction;
import net.minecraft.network.play.server.S33PacketUpdateSign;
import net.minecraft.network.play.server.S34PacketMaps;
import net.minecraft.network.play.server.S35PacketUpdateTileEntity;
import net.minecraft.network.play.server.S36PacketSignEditorOpen;
import net.minecraft.network.play.server.S37PacketStatistics;
import net.minecraft.network.play.server.S38PacketPlayerListItem;
import net.minecraft.network.play.server.S39PacketPlayerAbilities;
import net.minecraft.network.play.server.S3APacketTabComplete;
import net.minecraft.network.play.server.S3BPacketScoreboardObjective;
import net.minecraft.network.play.server.S3CPacketUpdateScore;
import net.minecraft.network.play.server.S3DPacketDisplayScoreboard;
import net.minecraft.network.play.server.S3EPacketTeams;
import net.minecraft.network.play.server.S3FPacketCustomPayload;
import net.minecraft.network.play.server.S40PacketDisconnect;

public interface INetHandlerPlayClient extends INetHandler
{
    /**
     * Spawns an instance of the objecttype indicated by the packet and sets its position and momentum
     */
    void handleSpawnObject(S0EPacketSpawnObject p_147235_1_);

    /**
     * Spawns an experience orb and sets its value (amount of XP)
     */
    void handleSpawnExperienceOrb(S11PacketSpawnExperienceOrb p_147286_1_);

    /**
     * Handles globally visible entities. Used in vanilla for lightning bolts
     */
    void handleSpawnGlobalEntity(S2CPacketSpawnGlobalEntity p_147292_1_);

    /**
     * Spawns the mob entity at the specified location, with the specified rotation, momentum and type. Updates the
     * entities Datawatchers with the entity metadata specified in the packet
     */
    void handleSpawnMob(S0FPacketSpawnMob p_147281_1_);

    /**
     * May create a scoreboard objective, remove an objective from the scoreboard or update an objectives' displayname
     */
    void handleScoreboardObjective(S3BPacketScoreboardObjective p_147291_1_);

    /**
     * Handles the spawning of a painting object
     */
    void handleSpawnPainting(S10PacketSpawnPainting p_147288_1_);

    /**
     * Handles the creation of a nearby player entity, sets the position and held item
     */
    void handleSpawnPlayer(S0CPacketSpawnPlayer p_147237_1_);

    /**
     * Renders a specified animation: Waking up a player, a living entity swinging its currently held item, being hurt
     * or receiving a critical hit by normal or magical means
     */
    void handleAnimation(S0BPacketAnimation p_147279_1_);

    /**
     * Updates the players statistics or achievements
     */
    void handleStatistics(S37PacketStatistics p_147293_1_);

    /**
     * Updates all registered IWorldAccess instances with destroyBlockInWorldPartially
     */
    void handleBlockBreakAnim(S25PacketBlockBreakAnim p_147294_1_);

    /**
     * Creates a sign in the specified location if it didn't exist and opens the GUI to edit its text
     */
    void handleSignEditorOpen(S36PacketSignEditorOpen p_147268_1_);

    /**
     * Updates the NBTTagCompound metadata of instances of the following entitytypes: Mob spawners, command blocks,
     * beacons, skulls, flowerpot
     */
    void handleUpdateTileEntity(S35PacketUpdateTileEntity p_147273_1_);

    /**
     * Triggers Block.onBlockEventReceived, which is implemented in BlockPistonBase for extension/retraction, BlockNote
     * for setting the instrument (including audiovisual feedback) and in BlockContainer to set the number of players
     * accessing a (Ender)Chest
     */
    void handleBlockAction(S24PacketBlockAction p_147261_1_);

    /**
     * Updates the block and metadata and generates a blockupdate (and notify the clients)
     */
    void handleBlockChange(S23PacketBlockChange p_147234_1_);

    /**
     * Prints a chatmessage in the chat GUI
     */
    void handleChat(S02PacketChat p_147251_1_);

    /**
     * Displays the available command-completion options the server knows of
     */
    void handleTabComplete(S3APacketTabComplete p_147274_1_);

    /**
     * Received from the servers PlayerManager if between 1 and 64 blocks in a chunk are changed. If only one block
     * requires an update, the server sends S23PacketBlockChange and if 64 or more blocks are changed, the server sends
     * S21PacketChunkData
     */
    void handleMultiBlockChange(S22PacketMultiBlockChange p_147287_1_);

    /**
     * Updates the worlds MapStorage with the specified MapData for the specified map-identifier and invokes a
     * MapItemRenderer for it
     */
    void handleMaps(S34PacketMaps p_147264_1_);

    /**
     * Verifies that the server and client are synchronized with respect to the inventory/container opened by the player
     * and confirms if it is the case.
     */
    void handleConfirmTransaction(S32PacketConfirmTransaction p_147239_1_);

    /**
     * Resets the ItemStack held in hand and closes the window that is opened
     */
    void handleCloseWindow(S2EPacketCloseWindow p_147276_1_);

    /**
     * Handles the placement of a specified ItemStack in a specified container/inventory slot
     */
    void handleWindowItems(S30PacketWindowItems p_147241_1_);

    /**
     * Displays a GUI by ID. In order starting from id 0: Chest, Workbench, Furnace, Dispenser, Enchanting table,
     * Brewing stand, Villager merchant, Beacon, Anvil, Hopper, Dropper, Horse
     */
    void handleOpenWindow(S2DPacketOpenWindow p_147265_1_);

    /**
     * Sets the progressbar of the opened window to the specified value
     */
    void handleWindowProperty(S31PacketWindowProperty p_147245_1_);

    /**
     * Handles pickin up an ItemStack or dropping one in your inventory or an open (non-creative) container
     */
    void handleSetSlot(S2FPacketSetSlot p_147266_1_);

    /**
     * Handles packets that have room for a channel specification. Vanilla implemented channels are "MC|TrList" to
     * acquire a MerchantRecipeList trades for a villager merchant, "MC|Brand" which sets the server brand? on the
     * player instance and finally "MC|RPack" which the server uses to communicate the identifier of the default server
     * resourcepack for the client to load.
     */
    void handleCustomPayload(S3FPacketCustomPayload p_147240_1_);

    /**
     * Closes the network channel
     */
    void handleDisconnect(S40PacketDisconnect p_147253_1_);

    /**
     * Retrieves the player identified by the packet, puts him to sleep if possible (and flags whether all players are
     * asleep)
     */
    void handleUseBed(S0APacketUseBed p_147278_1_);

    /**
     * Invokes the entities' handleUpdateHealth method which is implemented in LivingBase (hurt/death),
     * MinecartMobSpawner (spawn delay), FireworkRocket & MinecartTNT (explosion), IronGolem (throwing,...), Witch
     * (spawn particles), Zombie (villager transformation), Animal (breeding mode particles), Horse (breeding/smoke
     * particles), Sheep (...), Tameable (...), Villager (particles for breeding mode, angry and happy), Wolf (...)
     */
    void handleEntityStatus(S19PacketEntityStatus p_147236_1_);

    void handleEntityAttach(S1BPacketEntityAttach p_147243_1_);

    /**
     * Initiates a new explosion (sound, particles, drop spawn) for the affected blocks indicated by the packet.
     */
    void handleExplosion(S27PacketExplosion p_147283_1_);

    void handleChangeGameState(S2BPacketChangeGameState p_147252_1_);

    void handleKeepAlive(S00PacketKeepAlive p_147272_1_);

    /**
     * Updates the specified chunk with the supplied data, marks it for re-rendering and lighting recalculation
     */
    void handleChunkData(S21PacketChunkData p_147263_1_);

    void handleMapChunkBulk(S26PacketMapChunkBulk p_147269_1_);

    void handleEffect(S28PacketEffect p_147277_1_);

    /**
     * Registers some server properties (gametype,hardcore-mode,terraintype,difficulty,player limit), creates a new
     * WorldClient and sets the player initial dimension
     */
    void handleJoinGame(S01PacketJoinGame p_147282_1_);

    /**
     * Updates the specified entity's position by the specified relative moment and absolute rotation. Note that
     * subclassing of the packet allows for the specification of a subset of this data (e.g. only rel. position, abs.
     * rotation or both).
     */
    void handleEntityMovement(S14PacketEntity p_147259_1_);

    /**
     * Handles changes in player positioning and rotation such as when travelling to a new dimension, (re)spawning,
     * mounting horses etc. Seems to immediately reply to the server with the clients post-processing perspective on the
     * player positioning
     */
    void handlePlayerPosLook(S08PacketPlayerPosLook p_147258_1_);

    /**
     * Spawns a specified number of particles at the specified location with a randomized displacement according to
     * specified bounds
     */
    void handleParticles(S2APacketParticles p_147289_1_);

    void handlePlayerAbilities(S39PacketPlayerAbilities p_147270_1_);

    void handlePlayerListItem(S38PacketPlayerListItem p_147256_1_);

    /**
     * Locally eliminates the entities. Invoked by the server when the items are in fact destroyed, or the player is no
     * longer registered as required to monitor them. The latter  happens when distance between the player and item
     * increases beyond a certain treshold (typically the viewing distance)
     */
    void handleDestroyEntities(S13PacketDestroyEntities p_147238_1_);

    /**
     * Handle a remove entity effect packet.
     */
    void handleRemoveEntityEffect(S1EPacketRemoveEntityEffect p_147262_1_);

    /**
     * respawns the player
     */
    void handleRespawn(S07PacketRespawn p_147280_1_);

    /**
     * Updates the direction in which the specified entity is looking, normally this head rotation is independent of the
     * rotation of the entity itself
     */
    void handleEntityHeadLook(S19PacketEntityHeadLook p_147267_1_);

    /**
     * Updates which hotbar slot of the player is currently selected
     */
    void handleHeldItemChange(S09PacketHeldItemChange p_147257_1_);

    /**
     * Removes or sets the ScoreObjective to be displayed at a particular scoreboard position (list, sidebar, below
     * name)
     */
    void handleDisplayScoreboard(S3DPacketDisplayScoreboard p_147254_1_);

    /**
     * Invoked when the server registers new proximate objects in your watchlist or when objects in your watchlist have
     * changed -> Registers any changes locally
     */
    void handleEntityMetadata(S1CPacketEntityMetadata p_147284_1_);

    /**
     * Sets the velocity of the specified entity to the specified value
     */
    void handleEntityVelocity(S12PacketEntityVelocity p_147244_1_);

    void handleEntityEquipment(S04PacketEntityEquipment p_147242_1_);

    void handleSetExperience(S1FPacketSetExperience p_147295_1_);

    /**
     * Recieves player health from the server and then proceeds to set it locally on the client.
     */
    void handleUpdateHealth(S06PacketUpdateHealth p_147249_1_);

    /**
     * Updates a team managed by the scoreboard: Create/Remove the team registration, Register/Remove the player-team-
     * memberships, Set team displayname/prefix/suffix and/or whether friendly fire is enabled
     */
    void handleTeams(S3EPacketTeams p_147247_1_);

    /**
     * Either updates the score with a specified value or removes the score for an objective
     */
    void handleUpdateScore(S3CPacketUpdateScore p_147250_1_);

    void handleSpawnPosition(S05PacketSpawnPosition p_147271_1_);

    void handleTimeUpdate(S03PacketTimeUpdate p_147285_1_);

    /**
     * Updates a specified sign with the specified text lines
     */
    void handleUpdateSign(S33PacketUpdateSign p_147248_1_);

    void handleSoundEffect(S29PacketSoundEffect p_147255_1_);

    void handleCollectItem(S0DPacketCollectItem p_147246_1_);

    /**
     * Updates an entity's position and rotation as specified by the packet
     */
    void handleEntityTeleport(S18PacketEntityTeleport p_147275_1_);

    /**
     * Updates en entity's attributes and their respective modifiers, which are used for speed bonusses (player
     * sprinting, animals fleeing, baby speed), weapon/tool attackDamage, hostiles followRange randomization, zombie
     * maxHealth and knockback resistance as well as reinforcement spawning chance.
     */
    void handleEntityProperties(S20PacketEntityProperties p_147290_1_);

    void handleEntityEffect(S1DPacketEntityEffect p_147260_1_);
}