package netherite.oracle;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.io.BufferedReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import net.minecraft.entity.EnumCreatureType;
import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.entity.passive.EntityOcelot;
import net.minecraft.entity.passive.EntityWolf;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.biome.BiomeGenBase;

/**
 * What a world is: the seed, the difficulty, the render distance, the game
 * rules and which parts of vanilla it has, and how the client draws. Read
 * from config.yaml at the repo root (--conf; the grammar is in its header,
 * csrc/engine/worldconf.c reads the same file the same way; a run file's sim
 * part, its policy part skipped), then --set key=value ([sim.]section.key,
 * or a bare world key); a replay takes the values from
 * the tape header instead. KEYS is the registry: every key either reader
 * knows, with its default (config.yaml's value) and what it accepts.
 *
 * UNSUPPORTED lists the parts of vanilla the native engine does not have yet.
 * Each is off in every world, in this oracle too, so both run the same game;
 * asking for one prints a notice. Vanilla code checks the public fields below.
 * When the native engine gains a feature, its key moves out of UNSUPPORTED.
 *
 * The tape's world object (json()) is the seed, the feature switches and the
 * game rules that differ from vanilla's; the difficulty and the render
 * distance are client options in 1.7.10 (options.txt, applied as the
 * integrated server starts, not saved with the world), recorded as the
 * header's options.difficulty and options.rd like every tape before them.
 */
public final class WorldConf
{
    public static long seed = 1;
    /** EnumDifficulty's id: 0 peaceful, 1 easy, 2 normal, 3 hard. */
    public static int difficulty = 2;
    public static int renderDistance = 4;
    /** Game rules a new world starts with (doDaylightCycle, keepInventory). */
    public static boolean daylightCycle = true, keepInventory;
    /** client: fancy graphics, the particle setting (0 all, 1 decreased, 2 minimal). */
    public static boolean fancy;
    public static int particles = 2;
    /** client.gamma: options.txt's gamma (brightness; 0.0 Moody, 1.0 Bright): the frames' light, not the rows */
    public static String gamma = "0.0";

    /** Supported switches: vanilla when on. */
    public static boolean villages = true;

    // not supported yet, changes what a seed generates or spawns
    public static boolean horses, wolves, ocelots, builtGolems, wither, minecarts, boats;
    // not supported yet: the blocks and items stay; using them does nothing
    public static boolean redstone, enchanting, anvil, brewing, beacon, maps, fishing, fireworks, bookEditing, signEditing;

    static final String[] SUPPORTED = {"villages"};

    static final String[] UNSUPPORTED = {
        "horses", "wolves", "ocelots", "built_golems", "wither", "minecarts", "boats",
        "redstone", "enchanting", "anvil", "brewing", "beacon", "maps", "fishing", "fireworks", "book_editing", "sign_editing"
    };

    static final String[] DIFFICULTIES = {"peaceful", "easy", "normal", "hard"};
    static final String[] PARTICLES = {"all", "decreased", "minimal"};

    /**
     * The registry: section.key, its default, and what it takes: "onoff",
     * "long", "int:LO:HI", "size" (WxH), "name", "path", "cards" (CUDA
     * ordinals, a comma list) or "enum:A|B|...". The pipeline's RL keys and
     * the actions section are the runtime's and the trainer's; the oracle
     * only holds them to their kinds.
     * Kept in the order config.yaml lists them; csrc/engine/worldconf.c holds
     * the same table (csrc/tests/test_worldconf.c checks it against
     * config.yaml, which lists every key).
     */
    static final String[][] KEYS;
    static
    {
        List<String[]> k = new ArrayList<String[]>();
        k.add(new String[] {"world.seed", "1", "long"});
        k.add(new String[] {"world.difficulty", "normal", "enum:peaceful|easy|normal|hard"});
        k.add(new String[] {"world.render_distance", "4", "int:2:16"});
        k.add(new String[] {"world.daylight_cycle", "on", "onoff"});
        k.add(new String[] {"world.keep_inventory", "off", "onoff"});
        k.add(new String[] {"world.villages", "on", "onoff"});
        for (String u : UNSUPPORTED) k.add(new String[] {"world." + u, "off", "onoff"});
        k.add(new String[] {"client.graphics", "fast", "enum:fast|fancy"});
        k.add(new String[] {"client.particles", "minimal", "enum:all|decreased|minimal"});
        k.add(new String[] {"client.observation", "128x128", "size"});
        k.add(new String[] {"client.ticks_per_step", "4", "int:1:1000"});
        k.add(new String[] {"client.hud", "off", "onoff"});
        k.add(new String[] {"client.gamma", "0.0", "enum:0.0|0.25|0.5|0.75|1.0"});
        k.add(new String[] {"client.camera", "pixels", "enum:pixels|semantic|both"});
        k.add(new String[] {"client.semantic", "64x36", "size"});
        k.add(new String[] {"client.semantic_range", "64", "int:4:256"});
        k.add(new String[] {"engine.device_mesh", "0", "enum:0|1|2"});
        k.add(new String[] {"engine.device_generation", "off", "onoff"});
        k.add(new String[] {"engine.device_light", "off", "onoff"});
        k.add(new String[] {"pipeline.kind", "fresh", "name"});
        k.add(new String[] {"pipeline.start", "out/java/snapshots/fresh-play-s1", "path"});
        k.add(new String[] {"pipeline.worlds", "128", "int:1:65536"});
        k.add(new String[] {"pipeline.groups", "2", "int:1:65536"});
        k.add(new String[] {"pipeline.cards", "0", "cards"});
        k.add(new String[] {"pipeline.episode_seconds", "60", "int:1:86400"});
        k.add(new String[] {"actions.click_interval", "1", "int:1:100"});
        for (String a : new String[] {"mask_close_grid", "mask_noop_clicks", "move_stack", "item_select", "recipes", "hold_attack", "look_target"})
            k.add(new String[] {"actions." + a, "off", "onoff"});
        k.add(new String[] {"actions.look_ticks", "3", "int:1:100"});
        KEYS = k.toArray(new String[0][]);
    }

    /** The game rules a tape's world object names when they differ from vanilla: key, vanilla value, rule. */
    static final String[][] RULES = {{"daylight_cycle", "on", "doDaylightCycle"}, {"keep_inventory", "off", "keepInventory"}};

    /** A seed start's game rules are set once, at its first server tick. */
    static boolean rulesPending;

    private WorldConf() {}

    static final Pattern HEADER = Pattern.compile("([a-z][a-z0-9_]*):( +#.*| *)");
    static final Pattern SECTION2 = Pattern.compile("  ([a-z][a-z0-9_]*):( +#.*| *)");
    static final Pattern ENTRY = Pattern.compile("  ([a-z][a-z0-9_]*): ([A-Za-z0-9_./+,-]+)( +#.*| *)");
    static final Pattern ENTRY4 = Pattern.compile("    ([a-z][a-z0-9_]*): ([A-Za-z0-9_./+,-]+)( +#.*| *)");
    static final String SYNTAX = ": expected a part (sim: or policy:), a section (name:) or an entry (key: value) at its indent";

    /**
     * config.yaml's grammar (its header): section.key to value, in file order.
     * A file whose first line at column 0 is sim: or policy: is a run file:
     * parts at column 0, sections two spaces in, entries four; sim's are the
     * registry's, policy's (the trainer's) are held to the grammar and
     * skipped. Any other file is sim's sections alone.
     */
    static Map<String, String> parse(String path) throws IOException
    {
        Map<String, String> out = new LinkedHashMap<String, String>();
        List<String> seen = new ArrayList<String>(), keys = new ArrayList<String>();
        BufferedReader r = Rows.openRef(path);
        try
        {
            String line, part = "", section = null;
            int n = 0, parts = -1;
            while ((line = r.readLine()) != null)
            {
                ++n;
                String where = path + ":" + n;
                if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
                if (line.indexOf('\t') >= 0) throw new IllegalArgumentException("oracle: " + where + ": a tab (indent with spaces)");
                String t = line.trim();
                if (t.isEmpty() || t.startsWith("#")) continue;
                Matcher m = HEADER.matcher(line);
                boolean head = m.matches();
                if (head)
                {
                    String h = m.group(1);
                    boolean isPart = h.equals("sim") || h.equals("policy");
                    if (parts < 0) parts = isPart ? 1 : 0;
                    if (parts == 1)
                    {
                        if (!isPart) throw new IllegalArgumentException("oracle: " + where + ": unknown part " + h + " (sim or policy)");
                        if (seen.contains(h)) throw new IllegalArgumentException("oracle: " + where + ": part " + h + " twice");
                        seen.add(h);
                        part = h;
                        section = null;
                        continue;
                    }
                }
                else if (parts == 1)
                {
                    m = SECTION2.matcher(line);
                    head = m.matches();
                }
                if (head)
                {
                    section = m.group(1);
                    if (!part.equals("policy") && !known(section + "."))
                        throw new IllegalArgumentException("oracle: " + where + ": unknown section " + section);
                    String tag = (part.isEmpty() ? "" : part + ".") + section;
                    if (seen.contains(tag)) throw new IllegalArgumentException("oracle: " + where + ": section " + section + " twice");
                    seen.add(tag);
                    continue;
                }
                m = (parts == 1 ? ENTRY4 : ENTRY).matcher(line);
                if (!m.matches()) throw new IllegalArgumentException("oracle: " + where + SYNTAX);
                if (section == null) throw new IllegalArgumentException("oracle: " + where + ": an entry before any section");
                String k = section + "." + m.group(1), full = (part.isEmpty() ? "" : part + ".") + k;
                if (keys.contains(full)) throw new IllegalArgumentException("oracle: " + where + ": " + full + " twice");
                keys.add(full);
                if (part.equals("policy")) continue;
                check(k, m.group(2), where);
                out.put(k, m.group(2));
            }
        }
        finally { r.close(); }
        return out;
    }

    static boolean known(String prefix)
    {
        for (String[] k : KEYS) if (k[0].startsWith(prefix)) return true;
        return false;
    }

    static String[] key(String k)
    {
        for (String[] e : KEYS) if (e[0].equals(k)) return e;
        return null;
    }

    /** Refuses an unknown key or a value its kind does not take. */
    static void check(String k, String v, String where)
    {
        String[] e = key(k);
        if (e == null) throw new IllegalArgumentException("oracle: " + where + ": unknown key " + k);
        String kind = e[2];
        boolean ok;
        if (kind.equals("onoff")) ok = v.equals("on") || v.equals("off");
        else if (kind.equals("long")) ok = v.matches("-?[0-9]{1,19}");
        else if (kind.startsWith("int:"))
        {
            String[] b = kind.split(":");
            ok = v.matches("-?[0-9]{1,9}") && Integer.parseInt(v) >= Integer.parseInt(b[1]) && Integer.parseInt(v) <= Integer.parseInt(b[2]);
        }
        else if (kind.equals("size")) ok = v.matches("[0-9]{2,4}x[0-9]{2,4}");
        else if (kind.equals("name")) ok = v.matches("[a-z][a-z0-9_]*");
        else if (kind.equals("path")) ok = !v.isEmpty();
        else if (kind.equals("cards")) ok = v.matches("[0-9]{1,2}(,[0-9]{1,2})*");
        else ok = ("|" + kind.substring(5) + "|").contains("|" + v + "|");
        if (!ok) throw new IllegalArgumentException("oracle: " + where + ": " + k + " takes " + kind + ", not " + v);
    }

    /** section.key for a --set key: a bare key is a world key, sim.section.key is section.key. */
    static String full(String k)
    {
        if (k.startsWith("sim.") && k.length() > 4) k = k.substring(4);
        return k.indexOf('.') >= 0 ? k : "world." + k;
    }

    /** Resolve the world. tape != null: replay, take everything from its header. */
    static void load(String confPath, List<String> sets, String tape) throws IOException
    {
        Map<String, String> v = new LinkedHashMap<String, String>();
        for (String[] k : KEYS) v.put(k[0], k[1]);
        if (tape != null)
        {
            BufferedReader r = Rows.openRef(tape);
            String line = r.readLine();
            r.close();
            JsonObject h = line == null ? null : Rows.parse(line);
            if (h == null || !h.has("world")) throw new IOException("oracle: " + tape + " has no world config in its header; re-record it");
            for (Map.Entry<String, JsonElement> e : h.getAsJsonObject("world").entrySet())
            {
                String k = full(e.getKey()), val = e.getValue().getAsString();
                check(k, val, tape);
                v.put(k, val);
            }
            // a tape from before the keys: normal, and the pinned 4
            JsonObject o = h.has("options") ? h.getAsJsonObject("options") : null;
            if (o != null && o.has("difficulty")) v.put("world.difficulty", DIFFICULTIES[o.get("difficulty").getAsInt()]);
            if (o != null && o.has("rd")) v.put("world.render_distance", o.get("rd").getAsString());
            if (o != null && o.has("fancy")) v.put("client.graphics", o.get("fancy").getAsInt() != 0 ? "fancy" : "fast");
            if (o != null && o.has("particles")) v.put("client.particles", PARTICLES[o.get("particles").getAsInt()]);
            if (o != null && o.has("gamma")) v.put("client.gamma", Float.toString(o.get("gamma").getAsFloat()));
        }
        else
        {
            if (confPath == null) throw new IllegalArgumentException("oracle: pass --conf config.yaml");
            v.putAll(parse(confPath));
            for (String s : sets)
            {
                int eq = s.indexOf('=');
                if (eq < 0) throw new IllegalArgumentException("oracle: --set " + s + ": expected key=value");
                String k = full(s.substring(0, eq).trim()), val = s.substring(eq + 1).trim();
                check(k, val, "--set");
                v.put(k, val);
            }
        }

        seed = Long.parseLong(v.get("world.seed"));
        difficulty = indexOf(DIFFICULTIES, v.get("world.difficulty"));
        renderDistance = Integer.parseInt(v.get("world.render_distance"));
        daylightCycle = v.get("world.daylight_cycle").equals("on");
        keepInventory = v.get("world.keep_inventory").equals("on");
        villages = v.get("world.villages").equals("on");
        fancy = v.get("client.graphics").equals("fancy");
        particles = indexOf(PARTICLES, v.get("client.particles"));
        gamma = v.get("client.gamma");
        for (String k : UNSUPPORTED)
        {
            if (v.get("world." + k).equals("on"))
                System.out.println("netherite: " + k + " is not supported yet; it will come in a future release. This world runs with " + k + " = off.");
        }
        // Every key in UNSUPPORTED stays false: that is the whole point of the list.
        rulesPending = true;
    }

    private static int indexOf(String[] a, String s)
    {
        for (int i = 0; i < a.length; ++i) if (a[i].equals(s)) return i;
        throw new IllegalArgumentException("oracle: " + s);
    }

    /**
     * config.yaml's directory, the tree the run belongs to (its out/java):
     * the parent of configs/ for a named config there.
     */
    static java.io.File root(String confPath)
    {
        java.io.File d = new java.io.File(confPath).getAbsoluteFile().getParentFile();
        return d.getName().equals("configs") ? d.getParentFile() : d;
    }

    /** The resolved world, for the tape header. */
    static JsonObject json()
    {
        JsonObject o = new JsonObject();
        o.addProperty("seed", Long.toString(seed));
        o.addProperty("villages", villages ? "on" : "off");
        for (String k : UNSUPPORTED) o.addProperty(k, "off");
        // a rule at vanilla's value is left out, so every tape before the rules reads the same
        if (!daylightCycle) o.addProperty("daylight_cycle", "off");
        if (keepInventory) o.addProperty("keep_inventory", "on");
        return o;
    }

    /**
     * The first server tick of a run (Dev.atServerTick): a new world's game
     * rules, before anything ticks. A checkpoint's save keeps its own (its
     * manifest's world, which Checkpoint.verify held to this one, names them).
     */
    static void atServerTick(MinecraftServer server)
    {
        if (!rulesPending) return;
        rulesPending = false;
        if (Oracle.fromDir != null) return;
        net.minecraft.world.GameRules g = server.worldServers[0].getGameRules();
        if (!daylightCycle) g.setOrCreateGameRule("doDaylightCycle", "false");
        if (keepInventory) g.setOrCreateGameRule("keepInventory", "true");
    }

    /** /summon refuses what the world switches keep out, by EntityList name. */
    public static void checkSummon(String name)
    {
        boolean off = name.equals("EntityHorse") && !horses || name.equals("Wolf") && !wolves || name.equals("Ozelot") && !ocelots
            || name.equals("SnowMan") && !builtGolems || name.equals("WitherBoss") && !wither
            || name.startsWith("Minecart") && !minecarts || name.equals("Boat") && !boats;
        if (off) throw new net.minecraft.command.CommandException("netherite: " + name + " is not supported yet; it will come in a future release.");
    }

    /**
     * Take the switched-off animals out of every biome's spawn lists, before the
     * world exists. Both world-gen spawning and natural spawning read these lists.
     */
    static void pruneSpawns()
    {
        List<Class<?>> gone = new ArrayList<Class<?>>();
        if (!horses) gone.add(EntityHorse.class);
        if (!wolves) gone.add(EntityWolf.class);
        if (!ocelots) gone.add(EntityOcelot.class);
        for (BiomeGenBase b : BiomeGenBase.getBiomeGenArray())
        {
            if (b == null) continue;
            for (EnumCreatureType t : EnumCreatureType.values())
            {
                List l = b.getSpawnableList(t);
                if (l == null) continue;
                for (Iterator it = l.iterator(); it.hasNext();)
                    if (gone.contains(((BiomeGenBase.SpawnListEntry)it.next()).entityClass)) it.remove();
            }
        }
    }
}
