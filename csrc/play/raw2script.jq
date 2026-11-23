# A played session's raw input (java RawRec: NAME.raw.jsonl beside the Java
# client's tape) as out/native/play's frame-structured input script:
#   jq -n -c --argjson start S -f csrc/play/raw2script.jq NAME.raw.jsonl > script.jsonl
# S is the first tick the native client plays (its start snapshot's tick).
#
# The raw file lists, in the order they happened, the events LWJGL delivered,
# the ticks, and the frames (each with the ticks it ran and the camera motion
# EntityRenderer turned the player by after them). play runs one iteration
# per recorded frame: the events that arrived before the frame's first tick
# go into the window's event queue, then the frame's ticks run, then its
# motion turns the camera. So each output frame is its events (t: the tick
# the frame starts at), a {"t","clock"} line per tick with a screen clock,
# and the {"t","frame","ticks","pt","rel"} line that ends it:
#  - keys become SDL key names; LWJGL's repeats (X's autorepeat) stay, as
#    "repeat":1 presses: the window gets SDL's repeats too, and the client
#    must skip them where Keyboard.next does (in game, under a container
#    screen);
#  - a press or release (buttons 0 to 4: left, right, middle and the two
#    side buttons, mouse 1 to 5) carries the pointer in GuiScreen pixels (the
#    absolute display position of an ungrabbed event, as handleMouseInput
#    scales it; a grabbed event keeps the last pointer);
#  - an ungrabbed motion is a {"motion"} line; a grabbed motion is dropped
#    (the in-game mouse loop does nothing with it; the camera reads the
#    frame's MouseHelper deltas, which become the frame's rel);
#  - a frame drawn under a screen carries its pointer ("px", GuiScreen
#    pixels: Mouse.getX and getY as it drew, the hover its keys read next,
#    however the pointer got there);
#  - a wheel notch is a {"wheel"} line, unless the tick that takes it came
#    over 200 ms after the last one ended (vanilla's mouse loop skips the
#    wheel then; the recorded "late" line says so); an ungrabbed notch (a
#    screen up) carries the pointer and is always there: GuiScreen's
#    handleMouseInput takes it as a motion to its position, late or not.
# Events and frames before S are dropped (the Java client took them before
# the native client's start). Unmapped keys come out as {"unmapped":CODE}
# lines, which play ignores and rawjudge.sh counts.

def keyname:
  {"1":"Escape","2":"1","3":"2","4":"3","5":"4","6":"5","7":"6","8":"7","9":"8","10":"9","11":"0",
   "12":"-","13":"=","14":"Backspace","15":"Tab","16":"Q","17":"W","18":"E","19":"R","20":"T",
   "21":"Y","22":"U","23":"I","24":"O","25":"P","26":"[","27":"]","28":"Return","29":"Left Ctrl",
   "30":"A","31":"S","32":"D","33":"F","34":"G","35":"H","36":"J","37":"K","38":"L","39":";",
   "40":"'","41":"`","42":"Left Shift","43":"\\","44":"Z","45":"X","46":"C","47":"V","48":"B",
   "49":"N","50":"M","51":",","52":".","53":"/","54":"Right Shift","56":"Left Alt","57":"Space",
   "58":"CapsLock","59":"F1","60":"F2","61":"F3","62":"F4","63":"F5","64":"F6","65":"F7",
   "66":"F8","67":"F9","68":"F10","87":"F11","88":"F12","157":"Right Ctrl","184":"Right Alt",
   "200":"Up","203":"Left","205":"Right","208":"Down","219":"Left GUI","220":"Right GUI"}[tostring];

def clamp(lo; hi): if . < lo then lo elif . > hi then hi else . end;

# an ungrabbed event's display position (y from the bottom) in GuiScreen pixels
def gui($h; $x; $y):
  [(($x | clamp(0; $h.w - 1)) * $h.sw / $h.w | floor),
   ($h.sh - (($y | clamp(0; $h.h - 1)) * $h.sh / $h.h | floor) - 1)];

# one recorded event as script lines at tick $t; .ptr is the pointer so far
def convert($h; $t; $late):
  . as $s
  | $s.ev as $e
  | if $e.k then
      if $e.k[3] == 1 then
        (if ($e.k[0] | keyname) then {ptr: $s.ptr, out: [{t: $t, key: ($e.k[0] | keyname), down: 1, repeat: 1}]}
         else {ptr: $s.ptr, out: []} end)
      elif ($e.k[0] | keyname) then {ptr: $s.ptr, out: [{t: $t, key: ($e.k[0] | keyname), down: $e.k[1]}]}
      else {ptr: $s.ptr, out: [{t: $t, unmapped: $e.k[0], down: $e.k[1]}]} end
    else
      ($e.m) as [$b, $st, $a, $c, $w]
      | (if $e.g == 0 then gui($h; $a; $c) else $s.ptr end) as $p
      | {ptr: $p, out:
          (if $w != 0 then
             (if $e.g == 0 then [{t: $t, wheel: (if $w > 0 then 1 else -1 end), x: $p[0], y: $p[1]}]
              elif $late then [] else [{t: $t, wheel: (if $w > 0 then 1 else -1 end)}] end)
           elif $b >= 0 and $b <= 4 then [{t: $t, mouse: ($b + 1), down: $st, x: $p[0], y: $p[1]}]
           elif $b >= 0 then [{t: $t, unmapped: ("mouse" + ($b | tostring)), down: $st}]
           elif $e.g == 0 then [{t: $t, motion: 1, x: $p[0], y: $p[1]}]
           else [] end)}
    end;

{"framed": 1},
( [inputs] as $all
  # each tick's "late" line, known ahead: a frame that runs no tick leaves
  # its events to the next tick, whose lateness comes later in the file
  | ($all | reduce .[] as $l ({cur: -1, m: {}};
      if $l.tick != null then .cur = $l.tick elif $l.late != null then .m[.cur | tostring] = $l.late else . end) | .m) as $lates
  | foreach ($all[], {eof: 1}) as $l (
    {h: null, ticked: false, batch: [], carry: [], clocks: [], cur: -1, ptr: [0, 0], emit: [], last: $start};
    .emit = []
    | if $l.raw then .h = $l | .ptr = [($l.sw / 2 | floor), ($l.sh / 2 | floor)]
      elif ($l.m or $l.k) then (if .ticked then .carry += [$l] else .batch += [$l] end)
      elif $l.tick != null then .ticked = true | .cur = $l.tick
      elif $l.clock != null then .clocks += [[.cur, $l.clock]]
      elif $l.frame != null then
        .h as $h0
        | ($l.t - $l.ticks) as $s0
        | if $l.t < $start or ($l.t == $start and $l.ticks > 0) then
            # the frame ran before the native start (or its ticks all did)
            .batch = .carry | .carry = [] | .ticked = false | .clocks = []
          else
            (if $s0 < $start then $start else $s0 end) as $s
            | ($l.ticks - ($s - $s0)) as $n
            # the tick that takes the frame's events: its first, or the next
            | (($lates[$s | tostring] // 0) > 200) as $late
            | (if $s0 < $start then [] else .batch end) as $evs
            | reduce $evs[] as $e ({ptr: .ptr, out: []};
                ({ptr: .ptr, ev: $e} | convert($h0; $s; $late)) as $r | .ptr = $r.ptr | .out += $r.out) as $cv
            | .ptr = $cv.ptr
            | .emit = $cv.out
                + [.clocks[] | select(.[0] >= $s) | {t: .[0], clock: .[1]}]
                + [{t: $s, frame: $l.frame, ticks: $n, pt: $l.pt}
                   + (if $l.gui != null and $l.mx != null then {px: gui($h0; $l.mx; $l.my)} else {} end)
                   + (if $l.cam == 1 and (($l.dx // 0) != 0 or ($l.dy // 0) != 0) then {rel: [$l.dx, (0 - $l.dy)]} else {} end)
                   + (if $l.smooth == 1 then {smooth: 1} else {} end)]
            | .last = $l.t
            | .batch = .carry | .carry = [] | .ticked = false | .clocks = []
          end
      elif $l.eof then .emit = [{t: .last, quit: 1}]
      else . end;
    .emit[] ) )
