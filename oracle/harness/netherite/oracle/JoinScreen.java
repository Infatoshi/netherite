package netherite.oracle;

import net.minecraft.client.gui.GuiScreen;

/**
 * Shown while the world loads, in place of the main menu. Unlike the main
 * menu it does not pause the game, so the integrated server never runs its
 * pause-save (which vanilla does not hit at join either).
 */
public final class JoinScreen extends GuiScreen
{
    /**
     * In a fresh JVM the first JoinScreen initializes GuiScreen, whose static
     * RenderItem draws one seed from the client seeder, after Det.reset. A
     * pool JVM initialized GuiScreen before its pristine point, so its first
     * JoinScreen of each run makes that draw here, at the same place in the
     * stream (Pool).
     */
    public JoinScreen()
    {
        if (Pool.active() && !Pool.joinDrawn)
        {
            Pool.joinDrawn = true;
            Det.newRandom();
        }
    }

    @Override
    public boolean doesGuiPauseGame()
    {
        return false;
    }

    @Override
    public void drawScreen(int mouseX, int mouseY, float partialTicks)
    {
        this.drawDefaultBackground();
        super.drawScreen(mouseX, mouseY, partialTicks);
    }
}
