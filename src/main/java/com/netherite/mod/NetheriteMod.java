package com.netherite.mod;

import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class NetheriteMod implements ClientModInitializer {
    public static final String MOD_ID = "netherite";
    public static final Logger LOGGER = LoggerFactory.getLogger(MOD_ID);

    @Override
    public void onInitializeClient() {
        NetheriteConfig.INSTANCE.load();
        NetheriteConfig cfg = NetheriteConfig.INSTANCE;

        LOGGER.info("Netherite init: instance={}, seed={}, rl={}, {}x{}, rd={}, graphics={}",
                cfg.instanceId, cfg.seed, cfg.rl,
                cfg.width, cfg.height, cfg.renderDistance, cfg.graphics);

        FrameGrabber.INSTANCE.init(cfg.instanceId);
        ActionInjector.INSTANCE.init(cfg.instanceId);
        StateExporter.INSTANCE.init(cfg.instanceId);
        WorldController.INSTANCE.init(cfg);
        TaskReward.INSTANCE.init(cfg);

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            WorldController.INSTANCE.tick(client);
            if (client.world != null && client.player != null) {
                ActionInjector.INSTANCE.tick(client);
                // TaskReward runs before StateExporter so the reward block is
                // in place when StateExporter flips ready=1 at the end of tick.
                TaskReward.INSTANCE.tick(client);
                StateExporter.INSTANCE.tick(client);
            }
        });
    }
}
