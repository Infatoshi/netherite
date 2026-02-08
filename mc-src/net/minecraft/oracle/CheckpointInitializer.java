package net.minecraft.oracle;

import com.mojang.authlib.GameProfile;
import java.util.List;
import java.util.UUID;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.boss.EntityDragon;
import net.minecraft.entity.monster.EntityBlaze;
import net.minecraft.entity.monster.EntityEnderman;
import net.minecraft.entity.monster.EntityMob;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.network.NetHandlerPlayServer;
import net.minecraft.network.NetworkManager;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.management.ItemInWorldManager;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.DamageSource;
import net.minecraft.util.MathHelper;
import net.minecraft.world.WorldServer;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

/**
 * Sets up checkpoint test scenarios and checks success conditions.
 *
 * System properties:
 *   -Doracle.checkpoint=<name>     Checkpoint to run
 *   -Doracle.autotest=true         Auto-check success conditions and exit
 *
 * Two modes:
 *   autotest=true  -> spawn headless bot, run setup immediately, check success each tick
 *   autotest=false -> wait for human player to join, apply checkpoint to them
 */
public class CheckpointInitializer
{
    private static final Logger logger = LogManager.getLogger();
    private static CheckpointInitializer INSTANCE;

    private final TestCheckpoint checkpoint;
    private final boolean autotest;
    private final int timeout;

    private EntityPlayerMP bot; // target player (headless bot OR human player)
    private NetHandlerPlayServer handler;
    private boolean initialized;
    private boolean finished;
    private boolean waitingForSetup; // human joined, waiting for delay before setup
    private int joinTick; // tick when human player was detected
    private static final int SETUP_DELAY = 60; // ticks to wait after join (3 seconds)
    private int startTick;

    // Checkpoint-specific state
    private int lavaX, lavaY, lavaZ; // water_bucket: lava position
    private int portalX, portalY, portalZ; // nether_portal: portal interior
    private int pillarX, pillarY, pillarZ; // fall_damage: pillar top
    private int darkRoomX, darkRoomY, darkRoomZ; // mob_spawning: center

    private CheckpointInitializer()
    {
        String cpName = System.getProperty("oracle.checkpoint", "");
        this.checkpoint = TestCheckpoint.fromName(cpName);
        this.autotest = Boolean.getBoolean("oracle.autotest");
        if (this.checkpoint != null)
        {
            this.timeout = Integer.parseInt(
                System.getProperty("oracle.checkpoint.timeout",
                    String.valueOf(this.checkpoint.defaultTimeout)));
        }
        else
        {
            this.timeout = 6000;
        }
    }

    public static synchronized CheckpointInitializer get()
    {
        if (INSTANCE == null)
        {
            INSTANCE = new CheckpointInitializer();
        }
        return INSTANCE;
    }

    public boolean isActive()
    {
        return this.checkpoint != null && !this.finished;
    }

    public TestCheckpoint getCheckpoint()
    {
        return this.checkpoint;
    }

    /**
     * Called each tick from MinecraftServer.tick().
     */
    public void tick(MinecraftServer server, int currentTick)
    {
        if (this.finished || this.checkpoint == null) return;

        if (!this.initialized)
        {
            if (server.worldServers == null || server.worldServers.length == 0) return;
            WorldServer world = server.worldServerForDimension(0);
            if (world == null) return;

            if (this.autotest)
            {
                // Bot mode: create headless bot and run setup immediately
                createBot(server, world);
                initCheckpoint(server, currentTick);
            }
            else if (!this.waitingForSetup)
            {
                // Human mode: wait for a real player to join
                EntityPlayerMP human = findHumanPlayer(server);
                if (human == null) return; // no player yet, keep waiting
                this.bot = human;
                this.joinTick = currentTick;
                this.waitingForSetup = true;
                logger.info("[Checkpoint] Human player joined: " + human.getCommandSenderName()
                    + " -- waiting " + SETUP_DELAY + " ticks for world to stabilize");
                return;
            }
            else
            {
                // Waiting for delay after human join
                if (currentTick - this.joinTick < SETUP_DELAY) return;
                logger.info("[Checkpoint] Delay complete, applying checkpoint to "
                    + this.bot.getCommandSenderName());
                initCheckpoint(server, currentTick);
            }
            return;
        }

        int elapsed = currentTick - this.startTick;

        if (this.autotest)
        {
            if (checkSuccess(server))
            {
                logger.info("========================================");
                logger.info("[Checkpoint] PASS: " + this.checkpoint.name
                    + " succeeded at tick " + elapsed);
                logger.info("========================================");
                this.finished = true;
                server.initiateShutdown();
                return;
            }

            if (elapsed >= this.timeout)
            {
                logger.error("========================================");
                logger.error("[Checkpoint] FAIL: " + this.checkpoint.name
                    + " timed out after " + elapsed + " ticks");
                logger.error("========================================");
                this.finished = true;
                server.initiateShutdown();
                return;
            }

            // Progress report every 1000 ticks
            if (elapsed > 0 && elapsed % 1000 == 0)
            {
                logger.info("[Checkpoint] " + this.checkpoint.name
                    + " running... tick " + elapsed + "/" + this.timeout);
            }
        }

        // Headless bots don't receive client position packets, so server-side
        // physics (gravity, collision) don't run. Manually simulate for fall_damage.
        if (this.autotest && this.checkpoint == TestCheckpoint.FALL_DAMAGE && !this.bot.onGround)
        {
            simulateBotGravity(server);
        }
    }

    /**
     * Common initialization path for both bot and human modes.
     */
    private void initCheckpoint(MinecraftServer server, int currentTick)
    {
        this.startTick = currentTick;
        this.initialized = true;

        logger.info("[Checkpoint] Setting up: " + this.checkpoint.name
            + " -- " + this.checkpoint.description);
        logger.info("[Checkpoint] Target player: " + this.bot.getCommandSenderName()
            + " autotest=" + this.autotest);
        setupCheckpoint(server);
        logger.info("[Checkpoint] Setup complete. timeout=" + this.timeout + " ticks");
    }

    /**
     * Find first real (non-bot) player on the server.
     */
    private EntityPlayerMP findHumanPlayer(MinecraftServer server)
    {
        if (server.getConfigurationManager() == null) return null;
        List players = server.getConfigurationManager().playerEntityList;
        for (int i = 0; i < players.size(); i++)
        {
            EntityPlayerMP p = (EntityPlayerMP) players.get(i);
            // Skip our own headless bots
            if (!"CheckpointBot".equals(p.getCommandSenderName())
                && !"OracleBot".equals(p.getCommandSenderName()))
            {
                return p;
            }
        }
        return null;
    }

    /**
     * Manually apply gravity to the headless bot.
     * EntityPlayerMP.onUpdate() skips super.onUpdate() (which runs physics),
     * because MC servers rely on client-sent position packets.
     */
    private void simulateBotGravity(MinecraftServer server)
    {
        this.bot.motionY -= 0.08;
        this.bot.motionY *= 0.98;
        double newY = this.bot.posY + this.bot.motionY;

        // Ground check: stone floor at y=63, surface at y=64
        int groundY = 64;
        if (newY <= groundY)
        {
            // Landed -- apply fall damage using MC formula: ceil(fallDistance - 3)
            float dist = this.bot.fallDistance;
            this.bot.setPositionAndUpdate(this.bot.posX, groundY, this.bot.posZ);
            this.bot.onGround = true;
            this.bot.motionY = 0;
            this.bot.fallDistance = 0;
            int damage = MathHelper.ceiling_float_int(dist - 3.0f);
            if (damage > 0)
            {
                this.bot.attackEntityFrom(DamageSource.fall, (float) damage);
            }
            logger.info("[Checkpoint] Bot landed: fallDistance=" + dist
                + " damage=" + damage + " health=" + this.bot.getHealth());
        }
        else
        {
            float fell = (float)(this.bot.posY - newY);
            if (fell > 0) this.bot.fallDistance += fell;
            this.bot.setPositionAndUpdate(this.bot.posX, newY, this.bot.posZ);
        }
    }

    // ========== BOT CREATION ==========

    private void createBot(MinecraftServer server, WorldServer world)
    {
        GameProfile profile = new GameProfile(
            UUID.nameUUIDFromBytes("CheckpointBot".getBytes()), "CheckpointBot");
        ItemInWorldManager itemManager = new ItemInWorldManager(world);
        this.bot = new EntityPlayerMP(server, world, profile, itemManager);

        NetworkManager netManager = new NetworkManager(false);
        this.handler = new NetHandlerPlayServer(server, netManager, this.bot);

        server.getConfigurationManager().initializeConnectionToPlayer(
            netManager, this.bot, this.handler);

        logger.info("[Checkpoint] Bot spawned at ("
            + String.format("%.1f, %.1f, %.1f", this.bot.posX, this.bot.posY, this.bot.posZ) + ")");
    }

    // ========== CHECKPOINT SETUP ==========

    private void setupCheckpoint(MinecraftServer server)
    {
        switch (this.checkpoint)
        {
            case WATER_BUCKET:    setupWaterBucket(server); break;
            case NETHER_PORTAL:   setupNetherPortal(server); break;
            case NETHER_FORTRESS: setupNetherFortress(server); break;
            case ENDERMAN_HUNT:   setupEndermanHunt(server); break;
            case STRONGHOLD:      setupStronghold(server); break;
            case DRAGON_FULL:     setupDragonFull(server); break;
            case DRAGON_1HP:      setupDragon1HP(server); break;
            case FALL_DAMAGE:     setupFallDamage(server); break;
            case MOB_SPAWNING:    setupMobSpawning(server); break;
            case CRAFTING:        setupCrafting(server); break;
        }
    }

    // ---------- 1. WATER_BUCKET ----------

    private void setupWaterBucket(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        int bx = 0, by = 64, bz = 0;

        // Build flat stone platform
        for (int dx = -8; dx <= 8; dx++)
            for (int dz = -8; dz <= 8; dz++)
            {
                world.setBlock(bx + dx, by - 1, bz + dz, Blocks.stone, 0, 2);
                world.setBlock(bx + dx, by, bz + dz, Blocks.air, 0, 2);
                world.setBlock(bx + dx, by + 1, bz + dz, Blocks.air, 0, 2);
            }

        // Lava source in a small pit, water source on top -- gravity flow into lava
        // Water on lava source = obsidian
        // Use flag 3 (notify + client update) to trigger fluid flow
        world.setBlock(bx + 4, by - 1, bz, Blocks.lava, 0, 3);
        this.lavaX = bx + 4;
        this.lavaY = by - 1;
        this.lavaZ = bz;
        world.setBlock(bx + 4, by, bz, Blocks.water, 0, 3);

        // Inventory
        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.water_bucket),
            new ItemStack(Items.lava_bucket),
            new ItemStack(Item.getItemFromBlock(Blocks.stone), 64),
            new ItemStack(Items.bucket, 4),
        });

        teleportPlayer(bx + 0.5, by, bz + 0.5);
    }

    // ---------- 2. NETHER_PORTAL ----------

    private void setupNetherPortal(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        int bx = 0, by = 64, bz = 5;

        // Clear area
        for (int dx = -2; dx <= 5; dx++)
            for (int dy = -1; dy <= 6; dy++)
                for (int dz = -2; dz <= 2; dz++)
                    world.setBlock(bx + dx, by + dy, bz + dz, Blocks.air, 0, 2);

        // Floor
        for (int dx = -2; dx <= 5; dx++)
            for (int dz = -2; dz <= 2; dz++)
                world.setBlock(bx + dx, by - 1, bz + dz, Blocks.stone, 0, 2);

        // Build obsidian frame (4 wide x 5 tall, facing X axis at z=bz)
        for (int dx = 0; dx <= 3; dx++)
        {
            world.setBlock(bx + dx, by, bz, Blocks.obsidian, 0, 2);
            world.setBlock(bx + dx, by + 4, bz, Blocks.obsidian, 0, 2);
        }
        for (int dy = 1; dy <= 3; dy++)
        {
            world.setBlock(bx, by + dy, bz, Blocks.obsidian, 0, 2);
            world.setBlock(bx + 3, by + dy, bz, Blocks.obsidian, 0, 2);
        }

        this.portalX = bx + 1;
        this.portalY = by + 1;
        this.portalZ = bz;

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.flint_and_steel),
            new ItemStack(Items.diamond_pickaxe),
        });

        teleportPlayer(bx + 1.5, by, bz - 2.0);
    }

    // ---------- 3. NETHER_FORTRESS ----------

    private void setupNetherFortress(MinecraftServer server)
    {
        WorldServer nether = server.worldServerForDimension(-1);
        if (nether == null)
        {
            logger.error("[Checkpoint] Nether dimension not loaded!");
            return;
        }

        int nx = 0, ny = 64, nz = 0;

        // Build a small nether brick room
        for (int dx = -4; dx <= 4; dx++)
            for (int dz = -4; dz <= 4; dz++)
            {
                nether.setBlock(nx + dx, ny - 1, nz + dz, Blocks.nether_brick, 0, 2);
                nether.setBlock(nx + dx, ny + 4, nz + dz, Blocks.nether_brick, 0, 2);
                for (int dy = 0; dy <= 3; dy++)
                    nether.setBlock(nx + dx, ny + dy, nz + dz, Blocks.air, 0, 2);
            }
        // Walls
        for (int dy = 0; dy <= 3; dy++)
            for (int d = -4; d <= 4; d++)
            {
                nether.setBlock(nx - 4, ny + dy, nz + d, Blocks.nether_brick, 0, 2);
                nether.setBlock(nx + 4, ny + dy, nz + d, Blocks.nether_brick, 0, 2);
                nether.setBlock(nx + d, ny + dy, nz - 4, Blocks.nether_brick, 0, 2);
                nether.setBlock(nx + d, ny + dy, nz + 4, Blocks.nether_brick, 0, 2);
            }

        // Place blaze spawner
        nether.setBlock(nx, ny, nz + 2, Blocks.mob_spawner, 0, 2);
        TileEntity te = nether.getTileEntity(nx, ny, nz + 2);
        if (te instanceof TileEntityMobSpawner)
        {
            ((TileEntityMobSpawner) te).func_145881_a().setEntityName("Blaze");
        }

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.diamond_sword),
            new ItemStack(Items.bow),
            new ItemStack(Items.arrow, 64),
            new ItemStack(Items.cooked_beef, 64),
        });

        // Transfer player to nether
        server.getConfigurationManager().transferPlayerToDimension(
            this.bot, -1, nether.getDefaultTeleporter());
        this.bot.setPositionAndUpdate(nx + 0.5, ny, nz + 0.5);
    }

    // ---------- 4. ENDERMAN_HUNT ----------

    private void setupEndermanHunt(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        int bx = 0, by = 64, bz = 0;

        world.setWorldTime(14000L);

        // Build flat platform
        for (int dx = -10; dx <= 10; dx++)
            for (int dz = -10; dz <= 10; dz++)
            {
                world.setBlock(bx + dx, by - 1, bz + dz, Blocks.grass, 0, 2);
                world.setBlock(bx + dx, by, bz + dz, Blocks.air, 0, 2);
                world.setBlock(bx + dx, by + 1, bz + dz, Blocks.air, 0, 2);
                world.setBlock(bx + dx, by + 2, bz + dz, Blocks.air, 0, 2);
            }

        // Spawn 5 endermen nearby
        for (int i = 0; i < 5; i++)
        {
            EntityEnderman enderman = new EntityEnderman(world);
            double ex = bx + 5 + i * 3;
            double ez = bz + 3;
            enderman.setLocationAndAngles(ex, by, ez, 0.0f, 0.0f);
            world.spawnEntityInWorld(enderman);
        }

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.diamond_sword),
            new ItemStack(Items.cooked_beef, 64),
        });
        // Pumpkin as helmet (armor slot 3 = head)
        this.bot.inventory.armorInventory[3] = new ItemStack(Item.getItemFromBlock(Blocks.pumpkin));

        teleportPlayer(bx + 0.5, by, bz + 0.5);
    }

    // ---------- 5. STRONGHOLD ----------

    private void setupStronghold(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        int bx = 0, by = 30, bz = 0;

        // Build portal room: clear a 9x9x7 chamber
        for (int dx = -4; dx <= 4; dx++)
            for (int dz = -4; dz <= 4; dz++)
                for (int dy = -1; dy <= 5; dy++)
                {
                    if (dy == -1 || dy == 5)
                        world.setBlock(bx + dx, by + dy, bz + dz, Blocks.stonebrick, 0, 2);
                    else if (Math.abs(dx) == 4 || Math.abs(dz) == 4)
                        world.setBlock(bx + dx, by + dy, bz + dz, Blocks.stonebrick, 0, 2);
                    else
                        world.setBlock(bx + dx, by + dy, bz + dz, Blocks.air, 0, 2);
                }

        // End portal frame ring (3x3 ring)
        for (int dx = -1; dx <= 1; dx++)
            world.setBlock(bx + dx, by, bz - 1, Blocks.end_portal_frame, 2, 2);
        for (int dx = -1; dx <= 1; dx++)
            world.setBlock(bx + dx, by, bz + 1, Blocks.end_portal_frame, 0, 2);
        for (int dz = -1; dz <= 1; dz++)
            world.setBlock(bx + 1, by, bz + dz, Blocks.end_portal_frame, 3, 2);
        for (int dz = -1; dz <= 1; dz++)
            world.setBlock(bx - 1, by, bz + dz, Blocks.end_portal_frame, 1, 2);

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.ender_eye, 12),
        });

        teleportPlayer(bx + 0.5, by + 1, bz - 3.5);
    }

    // ---------- 6. DRAGON_FULL ----------

    private void setupDragonFull(MinecraftServer server)
    {
        WorldServer end = server.worldServerForDimension(1);
        if (end == null)
        {
            logger.error("[Checkpoint] End dimension not loaded!");
            return;
        }

        // Transfer to end
        server.getConfigurationManager().transferPlayerToDimension(
            this.bot, 1, end.getDefaultTeleporter());

        // Build obsidian platform
        for (int dx = -2; dx <= 2; dx++)
            for (int dz = -2; dz <= 2; dz++)
                end.setBlock(dx, 48, dz, Blocks.obsidian, 0, 2);

        this.bot.setPositionAndUpdate(0.5, 49, 0.5);

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.diamond_sword),
            new ItemStack(Items.bow),
            new ItemStack(Items.arrow, 64),
            new ItemStack(Items.arrow, 64),
            new ItemStack(Items.cooked_beef, 64),
            new ItemStack(Items.bed, 5),
        });
        this.bot.inventory.armorInventory[3] = new ItemStack(Items.diamond_helmet);
        this.bot.inventory.armorInventory[2] = new ItemStack(Items.diamond_chestplate);
        this.bot.inventory.armorInventory[1] = new ItemStack(Items.diamond_leggings);
        this.bot.inventory.armorInventory[0] = new ItemStack(Items.diamond_boots);
    }

    // ---------- 7. DRAGON_1HP ----------

    private void setupDragon1HP(MinecraftServer server)
    {
        WorldServer end = server.worldServerForDimension(1);
        if (end == null)
        {
            logger.error("[Checkpoint] End dimension not loaded!");
            return;
        }

        // Transfer to end
        server.getConfigurationManager().transferPlayerToDimension(
            this.bot, 1, end.getDefaultTeleporter());

        // Build platform
        for (int dx = -2; dx <= 2; dx++)
            for (int dz = -2; dz <= 2; dz++)
                end.setBlock(dx, 63, dz, Blocks.end_stone, 0, 2);

        this.bot.setPositionAndUpdate(0.5, 64, 0.5);

        // Spawn dragon at 1hp
        EntityDragon dragon = new EntityDragon(end);
        dragon.setLocationAndAngles(0, 70, 0, 0, 0);
        dragon.setHealth(1.0f);
        end.spawnEntityInWorld(dragon);

        // Destroy all ender crystals -- collect first, then kill
        // (avoid modifying entity list during iteration)
        java.util.ArrayList toKill = new java.util.ArrayList();
        for (int i = 0; i < end.loadedEntityList.size(); i++)
        {
            Entity e = (Entity) end.loadedEntityList.get(i);
            if (e.getClass().getSimpleName().equals("EntityEnderCrystal"))
            {
                toKill.add(e);
            }
        }
        for (int i = 0; i < toKill.size(); i++)
        {
            ((Entity) toKill.get(i)).setDead();
        }

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.diamond_sword),
        });
    }

    // ---------- 8. FALL_DAMAGE ----------

    private void setupFallDamage(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        int bx = 0, by = 64, bz = 0;

        // Ensure solid ground at landing zone
        for (int dx = -3; dx <= 3; dx++)
            for (int dz = -3; dz <= 3; dz++)
            {
                world.setBlock(bx + dx, by - 1, bz + dz, Blocks.stone, 0, 2);
                for (int dy = 0; dy <= 20; dy++)
                    world.setBlock(bx + dx, by + dy, bz + dz, Blocks.air, 0, 2);
            }

        this.pillarX = bx;
        this.pillarY = by + 15;
        this.pillarZ = bz;

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.water_bucket),
        });

        teleportPlayer(bx + 0.5, by + 15, bz + 0.5);
    }

    // ---------- 9. MOB_SPAWNING ----------

    private void setupMobSpawning(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        int bx = 0, by = 30, bz = 0;

        int roomW = 8;
        int roomL = 50;
        int h = 4;

        for (int dx = -roomW - 1; dx <= roomW + 1; dx++)
            for (int dz = -1; dz <= roomL + 1; dz++)
                for (int dy = -1; dy <= h + 1; dy++)
                {
                    boolean isWall = Math.abs(dx) == roomW + 1 || dz == -1 || dz == roomL + 1
                        || dy == -1 || dy == h + 1;
                    if (isWall)
                        world.setBlock(bx + dx, by + dy, bz + dz, Blocks.stone, 0, 2);
                    else
                        world.setBlock(bx + dx, by + dy, bz + dz, Blocks.air, 0, 2);
                }

        this.darkRoomX = bx;
        this.darkRoomY = by;
        this.darkRoomZ = bz + 32;

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Items.diamond_sword),
        });

        teleportPlayer(bx + 0.5, by, bz + 0.5);
    }

    // ---------- 10. CRAFTING ----------

    private void setupCrafting(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        int bx = 0, by = 64, bz = 0;

        // Clear area and place crafting table + furnace
        for (int dx = -3; dx <= 3; dx++)
            for (int dz = -3; dz <= 3; dz++)
            {
                world.setBlock(bx + dx, by - 1, bz + dz, Blocks.stone, 0, 2);
                world.setBlock(bx + dx, by, bz + dz, Blocks.air, 0, 2);
                world.setBlock(bx + dx, by + 1, bz + dz, Blocks.air, 0, 2);
            }
        world.setBlock(bx + 1, by, bz, Blocks.crafting_table, 0, 2);
        world.setBlock(bx + 2, by, bz, Blocks.furnace, 0, 2);

        clearAndSetInventory(new ItemStack[] {
            new ItemStack(Item.getItemFromBlock(Blocks.log), 64),
            new ItemStack(Item.getItemFromBlock(Blocks.cobblestone), 64),
            new ItemStack(Item.getItemFromBlock(Blocks.iron_ore), 32),
            new ItemStack(Items.diamond, 16),
            new ItemStack(Items.coal, 32),
            new ItemStack(Items.iron_ingot, 16),
            new ItemStack(Item.getItemFromBlock(Blocks.planks), 64),
            new ItemStack(Items.stick, 64),
        });

        teleportPlayer(bx + 0.5, by, bz + 0.5);
    }

    // ========== SUCCESS CHECKS ==========

    private boolean checkSuccess(MinecraftServer server)
    {
        switch (this.checkpoint)
        {
            case WATER_BUCKET:    return checkWaterBucket(server);
            case NETHER_PORTAL:   return checkNetherPortal(server);
            case NETHER_FORTRESS: return checkNetherFortress(server);
            case ENDERMAN_HUNT:   return checkEndermanHunt(server);
            case STRONGHOLD:      return checkStronghold(server);
            case DRAGON_FULL:     return checkDragonDead(server);
            case DRAGON_1HP:      return checkDragonDead(server);
            case FALL_DAMAGE:     return checkFallDamage(server);
            case MOB_SPAWNING:    return checkMobSpawning(server);
            case CRAFTING:        return checkCrafting(server);
            default: return false;
        }
    }

    private boolean checkWaterBucket(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        Block block = world.getBlock(this.lavaX, this.lavaY, this.lavaZ);
        return block == Blocks.cobblestone || block == Blocks.obsidian || block == Blocks.stone;
    }

    private boolean checkNetherPortal(MinecraftServer server)
    {
        return this.bot.dimension == -1;
    }

    private boolean checkNetherFortress(MinecraftServer server)
    {
        return inventoryContains(Items.blaze_rod);
    }

    private boolean checkEndermanHunt(MinecraftServer server)
    {
        return inventoryContains(Items.ender_pearl);
    }

    private boolean checkStronghold(MinecraftServer server)
    {
        return this.bot.dimension == 1;
    }

    private boolean checkDragonDead(MinecraftServer server)
    {
        WorldServer end = server.worldServerForDimension(1);
        if (end == null) return false;
        for (int i = 0; i < end.loadedEntityList.size(); i++)
        {
            Entity e = (Entity) end.loadedEntityList.get(i);
            if (e instanceof EntityDragon && !e.isDead)
            {
                return false;
            }
        }
        return true;
    }

    private boolean checkFallDamage(MinecraftServer server)
    {
        return this.bot.onGround && this.bot.getHealth() < 20.0f && this.bot.getHealth() > 0.0f;
    }

    private boolean checkMobSpawning(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        AxisAlignedBB box = AxisAlignedBB.getBoundingBox(
            this.darkRoomX - 8, this.darkRoomY, this.darkRoomZ - 15,
            this.darkRoomX + 8, this.darkRoomY + 4, this.darkRoomZ + 15);
        List mobs = world.getEntitiesWithinAABB(EntityMob.class, box);
        return mobs.size() > 0;
    }

    private boolean checkCrafting(MinecraftServer server)
    {
        return inventoryContains(Items.diamond_pickaxe)
            || inventoryContains(Items.diamond_sword);
    }

    // ========== UTILITIES ==========

    private void clearAndSetInventory(ItemStack[] items)
    {
        for (int i = 0; i < this.bot.inventory.mainInventory.length; i++)
            this.bot.inventory.mainInventory[i] = null;
        for (int i = 0; i < this.bot.inventory.armorInventory.length; i++)
            this.bot.inventory.armorInventory[i] = null;

        for (int i = 0; i < items.length && i < 9; i++)
            this.bot.inventory.mainInventory[i] = items[i];
    }

    private void teleportPlayer(double x, double y, double z)
    {
        this.bot.setPositionAndUpdate(x, y, z);
    }

    private boolean inventoryContains(Item item)
    {
        for (ItemStack stack : this.bot.inventory.mainInventory)
        {
            if (stack != null && stack.getItem() == item)
                return true;
        }
        return false;
    }
}
