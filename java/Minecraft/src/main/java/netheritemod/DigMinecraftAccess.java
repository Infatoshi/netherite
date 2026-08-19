package netheritemod;

/**
 * Duck interface mixed onto {@code Minecraft} so dig-trace emission can read
 * {@code leftClickCounter} without reflection. Implemented by
 * {@code MixinMinecraftDigAccess}.
 */
public interface DigMinecraftAccess {
    int dig$leftClickCounter();
}
