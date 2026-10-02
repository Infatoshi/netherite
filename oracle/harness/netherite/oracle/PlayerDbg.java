package netherite.oracle;

import com.google.gson.JsonObject;
import net.minecraft.client.Minecraft;
import net.minecraft.server.integrated.IntegratedServer;

/** Throwaway lane probe: dump the client player's movement state between step
 * commands, so a tape replay divergence in the sprint or collision flags can be
 * pinned to a row. Reads only; run between rows it changes nothing. */
public final class PlayerDbg
{
    private PlayerDbg() {}

    public static JsonObject run(IntegratedServer server, JsonObject cmd)
    {
        Minecraft mc = Minecraft.getMinecraft();
        net.minecraft.client.entity.EntityClientPlayerMP p = mc.thePlayer;
        JsonObject r = new JsonObject();
        r.addProperty("wt", p.worldObj != null ? (int)p.worldObj.getWorldTime() : -1);
        r.addProperty("x", p.posX);
        r.addProperty("y", p.posY);
        r.addProperty("z", p.posZ);
        r.addProperty("mx", p.motionX);
        r.addProperty("my", p.motionY);
        r.addProperty("mz", p.motionZ);
        r.addProperty("og", p.onGround);
        r.addProperty("ch", p.isCollidedHorizontally);
        r.addProperty("cv", p.isCollidedVertically);
        r.addProperty("spr", p.isSprinting());
        r.addProperty("ysz", (double)p.ySize);
        r.addProperty("fd", (double)p.fallDistance);
        r.addProperty("fwd", (double)p.movementInput.moveForward);
        r.addProperty("str", (double)p.movementInput.moveStrafe);
        r.addProperty("jmp", p.movementInput.jump);
        r.addProperty("snk", p.movementInput.sneak);
        r.addProperty("key", mc.gameSettings.keyBindSprint.getIsKeyPressed());
        r.addProperty("use", p.isUsingItem());
        net.minecraft.entity.ai.attributes.IAttributeInstance ai = p.getEntityAttribute(net.minecraft.entity.SharedMonsterAttributes.movementSpeed);
        r.addProperty("attr", ai.getAttributeValue());
        r.addProperty("mod", ai.getModifier(java.util.UUID.fromString("662A6B8D-DA3E-4C1C-8813-96EA6097278D")) != null);
        r.addProperty("spd", p.getAIMoveSpeed());
        r.addProperty("food", p.getFoodStats().getFoodLevel());
        return r;
    }
}
