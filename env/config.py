"""Netherite configuration -- every tunable knob for MC RL instances."""

from dataclasses import dataclass, field, fields


@dataclass
class NetheriteConfig:
    # -- Instance identity --
    instance_id: int = 0
    seed: int = 12345

    # -- Display --
    width: int = 854
    height: int = 480

    # -- Game settings --
    game_mode: str = "survival"  # survival, creative, adventure, spectator
    difficulty: str = "normal"  # peaceful, easy, normal, hard
    render_distance: int = 8  # chunks (2-32)
    simulation_distance: int = 5  # chunks (2-32)

    # -- Game rules --
    do_daylight_cycle: bool = False
    do_weather_cycle: bool = False
    do_mob_spawning: bool = False
    do_fire_tick: bool = False
    do_mob_griefing: bool = False
    do_entity_drops: bool = False
    do_tile_drops: bool = True
    natural_regeneration: bool = True
    random_tick_speed: int = 0
    keep_inventory: bool = True
    do_insomnia: bool = False
    do_patrol_spawning: bool = False
    do_trader_spawning: bool = False
    do_warden_spawning: bool = False

    # -- Graphics (performance knobs) --
    max_fps: int = 60
    vsync: bool = False
    graphics: str = "fast"  # fast, fancy, fabulous
    particles: str = "minimal"  # all, decreased, minimal
    clouds: str = "off"  # off, fast, fancy
    entity_shadows: bool = False
    smooth_lighting: bool = False
    biome_blend: int = 0  # 0 = off, 1-7
    gui_scale: int = 0  # 0 = auto
    fullscreen: bool = False
    fov: int = 70

    # -- RL mode --
    rl: bool = False  # set True for training (auto-dismiss menus, disable pause)
    headless: bool = False  # set True to hide MC window (GPU rendering still works)

    # -- JVM --
    jvm_xmx: str = "2G"
    jvm_xms: str = "1G"
    java_home: str | None = None

    def to_system_properties(self) -> list[str]:
        """Convert config to -Dnetherite.* JVM args."""
        props = []
        for f in fields(self):
            val = getattr(self, f.name)
            if val is None:
                continue
            key = f"netherite.{f.name}"
            if isinstance(val, bool):
                props.append(f"-D{key}={str(val).lower()}")
            else:
                props.append(f"-D{key}={val}")
        return props

    def to_gradle_args(self) -> list[str]:
        """Convert to gradle -D flags for ./gradlew runClient."""
        return self.to_system_properties()
