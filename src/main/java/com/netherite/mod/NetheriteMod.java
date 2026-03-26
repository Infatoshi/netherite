package com.netherite.mod;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class NetheriteMod implements ClientModInitializer {
    public static final String MOD_ID = "netherite";
    public static final Logger LOGGER = LoggerFactory.getLogger(MOD_ID);

    public static int instanceId;
    public static long worldSeed;

    @Override
    public void onInitializeClient() {
        instanceId = Integer.getInteger("netherite.instance", 0);
        worldSeed = Long.getLong("netherite.seed", 12345L);
        LOGGER.info("Netherite init: instance={}, seed={}", instanceId, worldSeed);

        ActionInjector.INSTANCE.init(instanceId);
        StateExporter.INSTANCE.init(instanceId);
        WorldController.INSTANCE.init(instanceId, worldSeed);

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            WorldController.INSTANCE.tick(client);
            if (client.world != null && client.player != null) {
                ActionInjector.INSTANCE.tick(client);
                StateExporter.INSTANCE.tick(client);
            }
        });
    }
}
