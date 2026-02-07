"""Fix all cross-references to deleted entity classes."""
import re
import os

def strip_import(content, class_name):
    """Comment out an import line for a given class."""
    pattern = r'^(import .+\.' + re.escape(class_name) + r';)$'
    return re.sub(pattern, r'// stripped: \1', content, flags=re.MULTILINE)

def strip_line_containing(content, needle, comment="stripped"):
    """Comment out lines containing the needle."""
    lines = content.split('\n')
    result = []
    for line in lines:
        if needle in line and not line.strip().startswith('//'):
            result.append('        // ' + comment + ': ' + line.strip())
        else:
            result.append(line)
    return '\n'.join(result)

def remove_line_containing(content, needle):
    """Remove lines containing the needle entirely."""
    lines = content.split('\n')
    result = [l for l in lines if needle not in l or l.strip().startswith('//')]
    return '\n'.join(result)

def fix_file(filepath, fixfunc):
    """Apply a fix function to a file."""
    with open(filepath, 'r') as f:
        content = f.read()
    content = fixfunc(content)
    with open(filepath, 'w') as f:
        f.write(content)
    print(f"Fixed {filepath}")

# ============================================================
# RenderManager.java - strip imports and entityRenderMap.put
# ============================================================
def fix_render_manager(c):
    for cls in ['EntityWither', 'EntityCaveSpider', 'EntityIronGolem', 'EntityMagmaCube',
                'EntitySlime', 'EntitySnowman', 'EntityWitch', 'EntityHorse',
                'EntityOcelot', 'EntityVillager', 'EntityWolf']:
        c = strip_import(c, cls)
    # Strip render class imports
    for cls in ['RenderCaveSpider', 'RenderIronGolem', 'RenderMagmaCube', 'RenderSlime',
                'RenderSnowMan', 'RenderWitch', 'RenderVillager', 'RenderWolf',
                'RenderOcelot', 'RenderHorse', 'RenderWither']:
        c = strip_import(c, cls)
    # Strip model imports
    for cls in ['ModelWolf', 'ModelOcelot', 'ModelSlime', 'ModelHorse', 'ModelWither',
                'ModelMagmaCube', 'ModelIronGolem', 'ModelSnowMan', 'ModelWitch', 'ModelVillager']:
        c = strip_import(c, cls)
    # Strip entityRenderMap.put lines for deleted entities
    for cls in ['EntityCaveSpider', 'EntityWolf', 'EntityOcelot', 'EntitySnowman',
                'EntityWitch', 'EntitySlime', 'EntityMagmaCube', 'EntityVillager',
                'EntityIronGolem', 'EntityWither', 'EntityHorse']:
        lines = c.split('\n')
        result = []
        for line in lines:
            if f'entityRenderMap.put({cls}.class' in line:
                result.append('        // stripped: ' + line.strip())
            else:
                result.append(line)
        c = '\n'.join(result)
    return c

fix_file('mc-src/net/minecraft/client/renderer/entity/RenderManager.java', fix_render_manager)

# ============================================================
# BiomeGenBase.java - strip slime and witch spawns
# ============================================================
def fix_biome_base(c):
    c = strip_import(c, 'EntitySlime')
    c = strip_import(c, 'EntityWitch')
    c = strip_line_containing(c, 'EntitySlime.class')
    c = strip_line_containing(c, 'EntityWitch.class')
    return c
fix_file('mc-src/net/minecraft/world/biome/BiomeGenBase.java', fix_biome_base)

# ============================================================
# BiomeGenSavanna.java - strip horse spawn
# ============================================================
def fix_biome_savanna(c):
    c = strip_import(c, 'EntityHorse')
    c = strip_line_containing(c, 'EntityHorse.class')
    return c
fix_file('mc-src/net/minecraft/world/biome/BiomeGenSavanna.java', fix_biome_savanna)

# ============================================================
# BiomeGenPlains.java - strip horse spawn
# ============================================================
fix_file('mc-src/net/minecraft/world/biome/BiomeGenPlains.java', fix_biome_savanna)

# ============================================================
# BiomeGenTaiga.java - strip wolf spawn
# ============================================================
def fix_biome_taiga(c):
    c = strip_import(c, 'EntityWolf')
    c = strip_line_containing(c, 'EntityWolf.class')
    return c
fix_file('mc-src/net/minecraft/world/biome/BiomeGenTaiga.java', fix_biome_taiga)

# ============================================================
# BiomeGenForest.java - strip wolf spawn
# ============================================================
fix_file('mc-src/net/minecraft/world/biome/BiomeGenForest.java', fix_biome_taiga)

# ============================================================
# BiomeGenSwamp.java - strip slime spawn
# ============================================================
def fix_biome_swamp(c):
    c = strip_import(c, 'EntitySlime')
    c = strip_line_containing(c, 'EntitySlime.class')
    return c
fix_file('mc-src/net/minecraft/world/biome/BiomeGenSwamp.java', fix_biome_swamp)

# ============================================================
# BiomeGenHell.java - strip magma cube spawn
# ============================================================
def fix_biome_hell(c):
    c = strip_import(c, 'EntityMagmaCube')
    c = strip_line_containing(c, 'EntityMagmaCube.class')
    return c
fix_file('mc-src/net/minecraft/world/biome/BiomeGenHell.java', fix_biome_hell)

# ============================================================
# BiomeGenJungle.java - strip ocelot spawn
# ============================================================
def fix_biome_jungle(c):
    c = strip_import(c, 'EntityOcelot')
    c = strip_line_containing(c, 'EntityOcelot.class')
    return c
fix_file('mc-src/net/minecraft/world/biome/BiomeGenJungle.java', fix_biome_jungle)

# ============================================================
# EntityTracker.java - strip EntityWither instanceof
# ============================================================
def fix_entity_tracker(c):
    c = strip_import(c, 'EntityWither')
    # Replace the instanceof check with a false condition
    c = c.replace(
        'else if (p_72786_1_ instanceof EntityWither)',
        'else if (false) // stripped: EntityWither instanceof'
    )
    return c
fix_file('mc-src/net/minecraft/entity/EntityTracker.java', fix_entity_tracker)

# ============================================================
# EntityLivingBase.java - strip EntityTameable/Wolf references
# ============================================================
def fix_entity_living_base(c):
    c = strip_import(c, 'EntityWolf')
    # Replace the EntityTameable block with simplified logic
    # The block checks if a tamed entity's owner matches the attacked player
    c = c.replace(
        'else if (entity instanceof net.minecraft.entity.passive.EntityTameable)',
        'else if (false) // stripped: EntityTameable instanceof check'
    )
    return c
fix_file('mc-src/net/minecraft/entity/EntityLivingBase.java', fix_entity_living_base)

# ============================================================
# EntityLiving.java - strip EntityTameable references
# ============================================================
def fix_entity_living(c):
    c = strip_import(c, 'EntityTameable')
    # Replace instanceof checks
    c = c.replace(
        '!(this instanceof EntityTameable) || !((EntityTameable)this).isTamed()',
        'true // stripped: EntityTameable tamed check'
    )
    c = c.replace(
        '((EntityTameable)this).func_152114_e(p_130002_1_)',
        'false // stripped: EntityTameable owner check'
    )
    return c
fix_file('mc-src/net/minecraft/entity/EntityLiving.java', fix_entity_living)

# ============================================================
# EntityCreature.java - strip EntityTameable references
# ============================================================
def fix_entity_creature(c):
    c = strip_import(c, 'EntityTameable')
    c = c.replace(
        'this instanceof EntityTameable && ((EntityTameable)this).isSitting()',
        'false // stripped: EntityTameable sitting check'
    )
    return c
fix_file('mc-src/net/minecraft/entity/EntityCreature.java', fix_entity_creature)

# ============================================================
# EntityCreeper.java - strip ocelot AI avoidance
# ============================================================
def fix_entity_creeper(c):
    c = strip_import(c, 'EntityOcelot')
    c = strip_line_containing(c, 'EntityOcelot.class')
    return c
fix_file('mc-src/net/minecraft/entity/monster/EntityCreeper.java', fix_entity_creeper)

# ============================================================
# EntityZombie.java - strip villager zombie conversion
# ============================================================
def fix_entity_zombie(c):
    c = strip_import(c, 'EntityVillager')
    # Replace villager attack conversion check
    c = c.replace(
        'p_70074_1_ instanceof EntityVillager',
        'false // stripped: EntityVillager conversion'
    )
    # Replace EntityAIAttackOnCollide with EntityVillager.class -> EntityLivingBase.class
    c = c.replace(
        'EntityVillager.class, 1.0D, true',
        'EntityLivingBase.class, 1.0D, true) ; // stripped: was EntityVillager target\n        // '
    )
    # Actually that's messy. Let me just strip the line
    # The zombie has AI tasks to attack villagers - just comment those lines
    lines = c.split('\n')
    result = []
    for line in lines:
        if 'EntityVillager.class' in line and not line.strip().startswith('//'):
            result.append('        // stripped: ' + line.strip())
        elif 'EntityVillager entityvillager' in line or 'entityvillager.' in line.strip():
            result.append('        // stripped: ' + line.strip())
        elif 'new EntityVillager' in line:
            result.append('        // stripped: ' + line.strip())
        else:
            result.append(line)
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/entity/monster/EntityZombie.java', fix_entity_zombie)

# ============================================================
# EntityAIAvoidEntity.java - strip EntityTameable references
# ============================================================
def fix_ai_avoid(c):
    c = strip_import(c, 'EntityTameable')
    c = c.replace(
        'this.theEntity instanceof EntityTameable && ((EntityTameable)this.theEntity).isTamed()',
        'false // stripped: EntityTameable tamed check'
    )
    return c
fix_file('mc-src/net/minecraft/entity/ai/EntityAIAvoidEntity.java', fix_ai_avoid)

# ============================================================
# EntityMinecart.java - strip EntityIronGolem reference
# ============================================================
def fix_entity_minecart(c):
    c = strip_import(c, 'EntityIronGolem')
    c = c.replace(
        '!(p_70108_1_ instanceof EntityIronGolem) && canBeRidden()',
        'canBeRidden() // stripped: EntityIronGolem check'
    )
    return c
fix_file('mc-src/net/minecraft/entity/item/EntityMinecart.java', fix_entity_minecart)

# ============================================================
# Block.java - strip EntityWither reference
# ============================================================
def fix_block(c):
    c = strip_import(c, 'EntityWither')
    c = c.replace(
        'entity instanceof EntityWither',
        'false // stripped: EntityWither check'
    )
    return c
fix_file('mc-src/net/minecraft/block/Block.java', fix_block)

# ============================================================
# BlockChest.java - strip EntityOcelot sitting cat check
# ============================================================
def fix_block_chest(c):
    c = strip_import(c, 'EntityOcelot')
    # The ocelot sitting check prevents chest opening - replace with false (no sitting cats)
    # Find the method that checks for ocelot and replace the entity list
    lines = c.split('\n')
    result = []
    skip_block = False
    brace_depth = 0
    for line in lines:
        if 'EntityOcelot.class' in line and not line.strip().startswith('//'):
            # This is the getEntitiesWithinAABB call - the whole method checks for sitting cats
            # Replace the iterator-based check with return false
            result.append('        // stripped: EntityOcelot sitting cat check - always allow chest open')
            result.append('        return false;')
            # Skip until end of method
            skip_block = True
            brace_depth = 0
            continue
        if skip_block:
            for ch in line:
                if ch == '{':
                    brace_depth += 1
                elif ch == '}':
                    brace_depth -= 1
            if brace_depth < 0:
                result.append(line)
                skip_block = False
            continue
        result.append(line)
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/block/BlockChest.java', fix_block_chest)

# ============================================================
# BlockPumpkin.java - strip golem/snowman creation
# ============================================================
def fix_block_pumpkin(c):
    c = strip_import(c, 'EntityIronGolem')
    c = strip_import(c, 'EntitySnowman')
    # The trySpawnGolem method creates snow golems and iron golems from block patterns
    # Replace the entity creation with no-ops
    # Find EntitySnowman and EntityIronGolem construction blocks and stub them
    c = c.replace('EntitySnowman entitysnowman = new EntitySnowman(p_149726_1_);',
                  '// stripped: snow golem creation')
    c = c.replace('EntityIronGolem entityirongolem = new EntityIronGolem(p_149726_1_);',
                  '// stripped: iron golem creation')
    # Comment out all lines referencing entitysnowman or entityirongolem
    lines = c.split('\n')
    result = []
    for line in lines:
        stripped = line.strip()
        if (('entitysnowman.' in stripped or 'entityirongolem.' in stripped) and
            not stripped.startswith('//')):
            result.append('                // stripped: ' + stripped)
        else:
            result.append(line)
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/block/BlockPumpkin.java', fix_block_pumpkin)

# ============================================================
# PlayerControllerMP.java - strip EntityHorse reference
# ============================================================
def fix_player_controller(c):
    c = strip_import(c, 'EntityHorse')
    c = c.replace(
        'this.mc.thePlayer.ridingEntity instanceof EntityHorse',
        'false // stripped: EntityHorse check'
    )
    return c
fix_file('mc-src/net/minecraft/client/multiplayer/PlayerControllerMP.java', fix_player_controller)

# ============================================================
# Village.java - strip EntityIronGolem/EntityVillager references
# ============================================================
def fix_village(c):
    c = strip_import(c, 'EntityIronGolem')
    c = strip_import(c, 'EntityVillager')
    # Replace iron golem spawning
    c = c.replace(
        'EntityIronGolem entityirongolem = new EntityIronGolem(this.worldObj);',
        '// stripped: iron golem spawning disabled')
    # Comment out all entityirongolem references
    lines = c.split('\n')
    result = []
    for line in lines:
        stripped = line.strip()
        if ('entityirongolem.' in stripped and not stripped.startswith('//')):
            result.append('            // stripped: ' + stripped)
        elif ('EntityIronGolem.class' in stripped and not stripped.startswith('//')):
            result.append('            // stripped: ' + stripped)
        elif ('EntityVillager.class' in stripped and not stripped.startswith('//')):
            result.append('            // stripped: ' + stripped)
        else:
            result.append(line)
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/village/Village.java', fix_village)

# ============================================================
# EntityPlayerMP.java - strip merchant/horse GUI
# ============================================================
def fix_player_mp(c):
    c = strip_import(c, 'IMerchant')
    c = strip_import(c, 'EntityHorse')
    c = strip_import(c, 'ContainerHorseInventory')
    c = strip_import(c, 'ContainerMerchant')
    c = strip_import(c, 'InventoryMerchant')
    c = strip_import(c, 'MerchantRecipeList')
    # Stub displayGUIMerchant method - replace entire body
    lines = c.split('\n')
    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if 'public void displayGUIMerchant(' in stripped:
            result.append('    public void displayGUIMerchant(Object p_71030_1_, String p_71030_2_) {} // stripped: merchant GUI')
            # Skip the method body
            brace_count = 0
            if '{' in stripped:
                brace_count = 1
            i += 1
            while i < len(lines) and brace_count > 0:
                for ch in lines[i]:
                    if ch == '{': brace_count += 1
                    elif ch == '}': brace_count -= 1
                i += 1
            continue
        elif 'public void displayGUIHorse(' in stripped:
            result.append('    public void displayGUIHorse(Entity p_110298_1_, IInventory p_110298_2_) {} // stripped: horse GUI')
            # Skip the method body
            brace_count = 0
            if '{' in stripped:
                brace_count = 1
            i += 1
            while i < len(lines) and brace_count > 0:
                for ch in lines[i]:
                    if ch == '{': brace_count += 1
                    elif ch == '}': brace_count -= 1
                i += 1
            continue
        else:
            result.append(line)
        i += 1
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/entity/player/EntityPlayerMP.java', fix_player_mp)

# ============================================================
# EntityPlayer.java - strip merchant/horse references
# ============================================================
def fix_player(c):
    c = strip_import(c, 'IMerchant')
    c = strip_import(c, 'EntityHorse')
    # Change method signatures to use base types
    c = c.replace(
        'public void displayGUIHorse(EntityHorse p_110298_1_, IInventory p_110298_2_) {}',
        'public void displayGUIHorse(Entity p_110298_1_, IInventory p_110298_2_) {} // stripped: was EntityHorse'
    )
    c = c.replace(
        'public void displayGUIMerchant(IMerchant p_71030_1_, String p_71030_2_) {}',
        'public void displayGUIMerchant(Object p_71030_1_, String p_71030_2_) {} // stripped: was IMerchant'
    )
    # Fix the instanceof check for EntityHorse riding
    c = c.replace(
        'this.ridingEntity instanceof EntityHorse',
        'false // stripped: EntityHorse riding check'
    )
    return c
fix_file('mc-src/net/minecraft/entity/player/EntityPlayer.java', fix_player)

# ============================================================
# EntityPlayerSP.java - strip merchant/horse references
# ============================================================
def fix_player_sp(c):
    c = strip_import(c, 'IMerchant')
    c = strip_import(c, 'EntityHorse')
    c = strip_import(c, 'GuiMerchant')
    # Fix method signatures
    lines = c.split('\n')
    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if 'public void displayGUIHorse(' in stripped:
            result.append('    public void displayGUIHorse(Entity p_110298_1_, IInventory p_110298_2_) {} // stripped: horse GUI')
            # Skip the method body
            brace_count = 0
            if '{' in stripped:
                brace_count = 1
            i += 1
            while i < len(lines) and brace_count > 0:
                for ch in lines[i]:
                    if ch == '{': brace_count += 1
                    elif ch == '}': brace_count -= 1
                i += 1
            continue
        elif 'public void displayGUIMerchant(' in stripped:
            result.append('    public void displayGUIMerchant(Object p_71030_1_, String p_71030_2_) {} // stripped: merchant GUI')
            # Skip the method body
            brace_count = 0
            if '{' in stripped:
                brace_count = 1
            i += 1
            while i < len(lines) and brace_count > 0:
                for ch in lines[i]:
                    if ch == '{': brace_count += 1
                    elif ch == '}': brace_count -= 1
                i += 1
            continue
        else:
            # Fix remaining EntityHorse instanceof
            if 'this.ridingEntity instanceof EntityHorse' in line:
                line = line.replace('this.ridingEntity instanceof EntityHorse',
                                   'false // stripped: EntityHorse check')
            result.append(line)
        i += 1
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/client/entity/EntityPlayerSP.java', fix_player_sp)

# ============================================================
# NetHandlerPlayClient.java - strip merchant/horse references
# ============================================================
def fix_net_client(c):
    c = strip_import(c, 'IMerchant')
    c = strip_import(c, 'NpcMerchant')
    c = strip_import(c, 'EntityHorse')
    c = strip_import(c, 'MerchantRecipeList')
    c = strip_import(c, 'GuiMerchant')
    # Stub the merchant GUI opening
    c = c.replace(
        'entityclientplayermp.displayGUIMerchant(new NpcMerchant(entityclientplayermp), p_147265_1_.func_148900_g() ? p_147265_1_.func_148902_e() : null);',
        '// stripped: merchant GUI opening'
    )
    # Stub the horse GUI opening
    c = c.replace(
        'if (entity != null && entity instanceof EntityHorse)',
        'if (false) // stripped: EntityHorse GUI'
    )
    # Fix the merchant recipe list handler
    lines = c.split('\n')
    result = []
    skip_merchant = False
    for line in lines:
        stripped = line.strip()
        if 'IMerchant imerchant' in stripped or 'GuiMerchant' in stripped or 'MerchantRecipeList merchantrecipelist' in stripped:
            result.append('                    // stripped: ' + stripped)
        elif 'imerchant.setRecipes' in stripped or 'merchantrecipelist' in stripped.lower():
            result.append('                    // stripped: ' + stripped)
        else:
            result.append(line)
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/client/network/NetHandlerPlayClient.java', fix_net_client)

# ============================================================
# NetHandlerPlayServer.java - strip horse/merchant references
# ============================================================
def fix_net_server(c):
    c = strip_import(c, 'EntityHorse')
    c = strip_import(c, 'ContainerMerchant')
    # Fix horse instanceof checks
    c = c.replace(
        'this.playerEntity.ridingEntity != null && this.playerEntity.ridingEntity instanceof EntityHorse',
        'false // stripped: EntityHorse check'
    )
    c = c.replace(
        'this.playerEntity.ridingEntity instanceof EntityHorse',
        'false // stripped: EntityHorse check'
    )
    # Fix ContainerMerchant instanceof
    c = c.replace(
        'container instanceof ContainerMerchant',
        'false // stripped: ContainerMerchant check'
    )
    return c
fix_file('mc-src/net/minecraft/network/NetHandlerPlayServer.java', fix_net_server)

# ============================================================
# Structure files
# ============================================================

# ComponentScatteredFeaturePieces.java - strip witch spawning
def fix_scattered(c):
    c = strip_import(c, 'EntityWitch')
    # Replace witch entity creation with no-op
    c = c.replace(
        'EntityWitch entitywitch = new EntityWitch(p_74875_1_);',
        '// stripped: witch spawning in witch hut')
    # Comment out entitywitch references
    lines = c.split('\n')
    result = []
    for line in lines:
        stripped = line.strip()
        if ('entitywitch.' in stripped and not stripped.startswith('//')):
            result.append('                            // stripped: ' + stripped)
        else:
            result.append(line)
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/world/gen/structure/ComponentScatteredFeaturePieces.java', fix_scattered)

# MapGenNetherBridge.java - strip magma cube spawn
def fix_nether_bridge(c):
    c = strip_import(c, 'EntityMagmaCube')
    c = strip_line_containing(c, 'EntityMagmaCube.class')
    return c
fix_file('mc-src/net/minecraft/world/gen/structure/MapGenNetherBridge.java', fix_nether_bridge)

# MapGenScatteredFeature.java - strip witch spawn
def fix_scattered_feature(c):
    c = strip_import(c, 'EntityWitch')
    c = strip_line_containing(c, 'EntityWitch.class')
    return c
fix_file('mc-src/net/minecraft/world/gen/structure/MapGenScatteredFeature.java', fix_scattered_feature)

# StructureVillagePieces.java - strip villager spawning
def fix_village_pieces(c):
    c = strip_import(c, 'EntityVillager')
    c = c.replace(
        'EntityVillager entityvillager = new EntityVillager(p_74893_1_, this.getVillagerType(i1));',
        '// stripped: villager spawning in village structures')
    # Comment out entityvillager references
    lines = c.split('\n')
    result = []
    for line in lines:
        stripped = line.strip()
        if ('entityvillager.' in stripped and not stripped.startswith('//')):
            result.append('                        // stripped: ' + stripped)
        else:
            result.append(line)
    return '\n'.join(result)
fix_file('mc-src/net/minecraft/world/gen/structure/StructureVillagePieces.java', fix_village_pieces)

print("\nAll entity cross-references fixed.")
