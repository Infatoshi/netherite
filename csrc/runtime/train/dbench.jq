# dbench.jq (lane/dbench): the dragon benchmark's tables, read by dbench.sh.
#   jq -n -r -f dbench.jq --slurpfile books DIR/books.jsonl --slurpfile eps DIR/episodes.jsonl --arg mode MODE [--arg a A --arg b B]
# books.jsonl: ntloop --score-books (line 1 the drivers, then a line an env: the milestones' first steps, the steps by
# stage and driver, the deaths, the macros' books); episodes.jsonl: ntloop --out (a line an episode: ms, deaths, items).
# Steps are 1/20 s: minutes = steps / 1200. MODE funnel | deaths | link (A, B: a link's two ends, below) | links

def MS: [["wooden_pickaxe", 0], ["stone_pickaxe", 1], ["furnace", 4], ["iron_ingot", 5], ["iron_pickaxe", 2],
         ["bucket", 6], ["flint_and_steel", 7], ["diamond_pickaxe", 3], ["obsidian", 8], ["nether", 13],
         ["blaze_rod", 9], ["ender_pearl", 11], ["ender_eye", 12], ["stronghold", "room"], ["end", 14],
         ["dragon", 15]];
def STAGES: ["spawn", "wooden_pickaxe", "stone_pickaxe", "furnace", "iron_ingot", "iron_pickaxe", "bucket",
             "flint_and_steel", "diamond_pickaxe", "obsidian", "nether", "blaze_rod", "blaze_powder", "ender_pearl",
             "ender_eye", "end"];
def EGGS: ["creeper", "skeleton", "spider", "zombie", "slime", "ghast", "pigman", "enderman", "cave_spider",
           "silverfish", "blaze", "magma_cube", "bat", "witch", "pig", "sheep", "cow", "chicken", "squid", "wolf",
           "mooshroom", "ocelot", "horse", "villager"];
def DIMS: {"-1": "Nether", "0": "overworld", "1": "End"};
# a link: its name, the stage the run is at when it begins, the milestone it needs (MS index key), and the macros that
# would do it (by their run-file names), the kit items worth a line (item id, name)
def LINKS: [
  {n: "spawn>wooden_pickaxe", j: 0, a: null, b: 0, x: ["craft", "table-anywhere", "surface"]},
  {n: "wooden_pickaxe>stone_pickaxe", j: 1, a: 0, b: 1, x: ["craft", "mine"]},
  {n: "stone_pickaxe>furnace", j: 2, a: 1, b: 4, x: ["craft"]},
  {n: "furnace>iron_ingot", j: 3, a: 4, b: 5, x: ["craft", "iron", "strip", "strip2"]},
  {n: "iron_ingot>iron_pickaxe", j: 4, a: 5, b: 2, x: ["craft", "iron"]},
  {n: "iron_pickaxe>bucket", j: 5, a: 2, b: 6, x: ["craft", "iron", "strip2"]},
  {n: "bucket>flint_and_steel", j: 6, a: 6, b: 7, x: ["craft", "strip2"]},
  {n: "flint_and_steel>diamond_pickaxe", j: 7, a: 7, b: 3, x: ["strip", "strip2"]},
  {n: "diamond_pickaxe>obsidian", j: 8, a: 3, b: 8, x: ["obsidian", "bportal", "strip2"]},
  {n: "obsidian>nether", j: 9, a: 8, b: 13, x: ["obsidian", "bportal", "portal"]},
  {n: "nether>blaze_rod", j: 10, a: 13, b: 9, x: ["nether"]},
  {n: "blaze_rod>ender_pearl", j: 11, a: 9, b: 11, x: ["pearl", "pearlcave"]},
  {n: "ender_pearl>ender_eye", j: 13, a: 11, b: 12, x: ["late", "craft"]},
  {n: "ender_eye>stronghold", j: 14, a: 12, b: "room", x: ["stronghold"]},
  {n: "stronghold>end", j: 14, a: "room", b: 14, x: ["stronghold"]},
  {n: "end>dragon", j: 15, a: 14, b: 15, x: ["dragon"]}];
def KIT: [[265, "iron_ingot"], [318, "flint"], [264, "diamond"], [4, "cobble"], [257, "iron_pickaxe"], [278, "diamond_pickaxe"],
          [325, "bucket"], [259, "flint&steel"], [326, "water_bucket"], [327, "lava_bucket"], [49, "obsidian"],
          [369, "blaze_rod"], [368, "pearl"], [381, "eye"]];

def pad($n): tostring | (. + (" " * $n))[0:$n];
def rpad($n): tostring | ((" " * $n) + .)[-$n:];
def f($d): if . == null then "-" else (. * pow(10; $d) | round / pow(10; $d)) | tostring end;
def fx($d): if . == null then "-" else (. * pow(10; $d) | round) as $r | ($r / pow(10; $d)) as $v | ($v | floor) as $w
  | "\($w).\(($r - $w * pow(10; $d)) | tostring | ("0" * $d + .)[-$d:])" end;
def median: sort | if length == 0 then null else (length / 2 | floor) as $i
  | if length % 2 == 1 then .[$i] else (.[$i - 1] + .[$i]) / 2 end end;
def first($r; $m): if $m == "room" then ($r.sh.room // -1) else $r.ms_first[$m] end;
def reached($r; $m): $m != null and first($r; $m) >= 0;
def heldat($r; $m): $m != null and first($r; $m) == 0;   # held by the start
def drv($d; $m): ($d[($m | tostring)] // (if $m == 16 then "screen" else "driver" + ($m | tostring) end));

($books | map(select(.drivers != null)) | .[0].drivers) as $D
| ($eps | map({key: (.env | tostring), value: .}) | from_entries) as $E
| ($books | map(select(.env != null) | . + {ep: $E[(.env | tostring)]})) as $R

# ---- causes of death: a label for death k of run r
| def cause($r; $k):
    if $r.d_egg[$k] >= 0 then "killed by " + EGGS[$r.d_egg[$k]]
    elif $r.d_fall[$k] == 1 then (if $r.d_dim[$k] == 1 then "fell (void)" else "fell" end)
    elif $r.d_fire[$k] == 1 then "fire or lava"
    elif $r.d_air[$k] == 1 then "drowned"
    elif $r.d_food[$k] == 1 then "starved"
    elif $r.d_dim[$k] == 1 then "the End, no mob (dragon or other)"
    else "other (suffocation, cactus, an unseen source)" end;
  def deaths_of($r): [range(0; ([$r.ndeath, 8] | min)) as $k
                      | {k: $k, step: $r.d_step[$k], dim: $r.d_dim[$k], stage: STAGES[$r.d_stage[$k]],
                         who: drv($D; $r.d_mb[$k]), cause: cause($r; $k)}];
  def hist: group_by(.) | map({key: .[0], n: length}) | sort_by(-.n);
  def histlines($pre; $tot): .[:8][] | "\($pre)\(.n | rpad(4)) \(((100 * .n / $tot) | round) | rpad(3))%  \(.key)";

# ---- the funnel
def funnel($R):
  ($R | length) as $N
  | [range(0; MS | length) as $i | MS[$i] as $m
     | ($R | map(select(reached(.; $m[1])))) as $H
     | (if $i == 0 then $R else ($R | map(select(reached(.; MS[$i - 1][1])))) end) as $P
     | ($P | map(select(reached(.; $m[1]))) | length) as $both
     | ($P | length) as $pn
     | (if $pn > 0 then $both / $pn else null end) as $c
     | {name: $m[0], n: ($H | length), share: (($H | length) / $N), med: ($H | map(first(.; $m[1]) / 1200) | median),
        conv: $c, pn: $pn, se: (if $c != null then ($c * (1 - $c) / $pn | sqrt) else null end),
        f0: (if ($H | length) > 0 then ($H | map(select(.ep.food <= 0)) | length) / ($H | length) else null end),
        f0min: ($H | map(select((.food0 // -1) >= 0) | .food0 / 1200) | median)}];

def funnel_text($R):
  ($R | length) as $N
  | "\("milestone" | pad(17)) \("runs" | rpad(5)) \("share" | rpad(6)) \("median min" | rpad(11)) \("conversion" | rpad(11)) \("(SE)" | rpad(7)) \("food0 end" | rpad(9)) \("1st food0" | rpad(9))  from",
    (funnel($R) | to_entries[] | .key as $i | .value as $v
     | "\($v.name | pad(17)) \($v.n | rpad(5)) \($v.share | fx(3) | rpad(6)) \($v.med | fx(1) | rpad(11)) \($v.conv | fx(3) | rpad(11)) \($v.se | fx(3) | rpad(7)) \($v.f0 | fx(3) | rpad(9)) \($v.f0min | fx(1) | rpad(9))  \(if $i == 0 then "all \($N) runs" else "\($v.pn) at \(MS[$i - 1][0])" end)");

def funnel_tsv($R): funnel($R)[] | [.name, .n, .share, .med, .conv, .se, .pn, .f0, .f0min] | @tsv;

# ---- deaths: a run, the share that died, what killed, who drove, where
def deaths_text($R):
  ($R | length) as $N
  | ($R | map(.ep.deaths) | add / $N) as $dpr
  | ($R | map(select(.ep.deaths > 0)) | length) as $dr
  | ([$R[] | deaths_of(.)[]]) as $all
  | ($all | length) as $T
  | "deaths a run \($dpr | fx(3)); runs that died at least once \($dr) of \($N) (\(100 * $dr / $N | round)%); deaths booked \($T) of \($R | map(.ep.deaths) | add) (the books keep a run's first 8)",
    (if $T == 0 then empty else
      "what killed:", ($all | map(.cause) | hist | histlines("  "; $T)),
      "who drove the step before:", ($all | map(.who) | hist | histlines("  "; $T)),
      "where (dimension):", ($all | map(DIMS[.dim | tostring]) | hist | histlines("  "; $T)),
      "the run's stage:", ($all | map(.stage) | hist | histlines("  "; $T)),
      "first death, minute: median \($R | map(select(.ndeath > 0) | .d_step[0] / 1200) | median | fx(1))"
     end);

# ---- a link: the runs at A that did or did not reach B
def drivers_after($r; $j): [$r.mb[] | select(.[0] >= $j) | {m: drv($D; .[1]), n: .[2]}];
# distinct trajectories among runs: a frozen policy's runs from one start only diverge when its sampler does, and a macro
# that drives every step does not (lane/rod: 118 Nether deaths all at step 14543), so a rate over N runs of fewer than 8
# distinct ones is not a rate. A run's signature: its start, the steps every milestone was first held, its deaths' steps and
# dimensions, where it ended (net, cells) and what it held.
# A run that died is its start and its first death (the step, the dimension, the driver, the killer, the stage): what it
# does after a respawn with nothing is another game and would make equal runs look apart. A run that lived is its start,
# its milestones' steps and where it ended (net, cells, items).
def sigs: map(if .ndeath > 0 then [.ep.start, .d_step[0], .d_dim[0], .d_mb[0], .d_egg[0], .d_stage[0]]
                else [.ep.start, .ms_first, .ep.net, .ep.cells, .ep.items] end);
def trajs: sigs | unique | length;
def topgroup: sigs | group_by(.) | map(length) | max // 0;   # the runs the largest identical trajectory holds
def link_row($R; $L):
  ($R | map(select(. as $r | ($L.a == null or reached($r; $L.a)) and (heldat($r; $L.b) | not)))) as $S
  | ($S | map(select(reached(.; $L.b)))) as $H
  | ($S | length) as $n
  | {n: $L.n, runs: $n, hit: ($H | length), rate: (if $n > 0 then ($H | length) / $n else null end), S: $S, H: $H,
     traj: ($S | trajs), top: ($S | topgroup), starts: ($S | map(.ep.start) | unique | length)};

def tA($x; $L): if $L.a == null then 0 else (first($x; $L.a) | if . < 0 then 0 else . end) end;
def link_deaths($x; $L): deaths_of($x) | map(select(.step >= tA($x; $L)));
def link_fail_text($R; $L):
  link_row($R; $L) as $r
  | ($r.S | map(select(reached(.; $L.b) | not))) as $F
  | ($F | length) as $nf
  # died on the link: a death after the run reached A (a library start holds A from step 0)
  | ($F | map(select(link_deaths(.; $L) | length > 0))) as $dead
  | ($F | map(select(link_deaths(.; $L) | length == 0))) as $alive
  | "link \($L.n): \($r.hit) of \($r.runs) reached it (\($r.rate | fx(3))); of the \($nf) that did not, \($dead | length) died after reaching its start and \($alive | length) did not",
    (if ($dead | length) == 0 then empty else
      "  died (first death on the link; cause | who drove the step before | dimension):",
      ($dead | map(link_deaths(.; $L)[0]) | map(.cause + " | " + .who + " | " + DIMS[.dim | tostring]) | hist
        | histlines("    "; ($dead | length)))
     end),
    (if ($alive | length) == 0 then empty else
      "  did not die on the link (\($alive | length)): who drove the steps at and after the stage (share of steps; policy = the net's own keys):",
      ($alive | map(drivers_after(.; $L.j)) | flatten | group_by(.m) | map({m: .[0].m, n: (map(.n) | add)})
        | (map(.n) | add) as $tot | sort_by(-.n) | .[:7][] | "    \(.m | pad(14)) \((100 * .n / $tot) | round | rpad(3))%  \(.n) steps"),
      "  the link's macros (\($L.x | join(", "))) drove no step at or after the stage in \($alive | map(select(drivers_after(.; $L.j) | map(select(.m as $m | $L.x | index($m))) | length == 0)) | length) of \($alive | length) runs",
      "  held at the end, runs of \($alive | length): \(KIT | map(. as $k | "\($k[1]) \($alive | map(select((.ep.items[($k[0] | tostring)] // 0) > 0)) | length)") | join(", "))"
     end);

def link_tsv($R; $L): link_row($R; $L) | [.n, .runs, .hit, .rate] | @tsv;

if $mode == "funnel" then funnel_text($R)
elif $mode == "funnel_tsv" then funnel_tsv($R)
elif $mode == "deaths" then deaths_text($R)
elif $mode == "links_tsv" then (LINKS[] as $L | link_tsv($R; $L))
elif $mode == "link_fail" then (LINKS[] | select(.n == $ARGS.named.a) as $L | link_fail_text($R; $L))
elif $mode == "link_rate" then (LINKS[] | select(.n == $ARGS.named.a) as $L | link_row($R; $L) | "\(.n)\t\(.runs)\t\(.hit)\t\(.rate)\t\(.traj)\t\(.starts)\t\(.top)")
elif $mode == "traj" then "\($R | trajs)\t\($R | map(.ep.start) | unique | length)\t\($R | topgroup)"
elif $mode == "linknames" then LINKS[].n
elif $mode == "stage_links" then (STAGES | index($ARGS.named.a)) as $j | LINKS[] | select(.j == $j) | .n
else error("dbench.jq: mode " + $mode) end
