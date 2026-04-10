from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_window_mixin_applies_configured_size_and_disables_retina_scaling():
    text = (ROOT / "src/main/java/com/netherite/mod/mixin/WindowMixin.java").read_text()

    assert "glfwDefaultWindowHints()V" in text
    assert "remap = false" in text
    assert "GLFW.glfwWindowHint(GLFW.GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW.GLFW_FALSE);" in text
    assert "GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GLFW.GLFW_FALSE);" in text
    assert '@Inject(method = "<init>", at = @At("TAIL"))' in text
    assert "@Shadow" in text
    assert "GLFW.glfwHideWindow(this.handle);" in text
