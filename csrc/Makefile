# Native Minecraft 1.7.10, checked stage by stage against the Java oracle's dumps
# (make -C ../oracle chunks SEED=N). -ffp-contract=off keeps C from fusing
# multiply-adds that Java evaluates as two roundings.
CC      ?= cc
CFLAGS  ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -Werror
# The link: GNU ld took 40 to 46 s per tool against the 67 MB archive (-g) at
# load 80, 1,210 s over a rebuild's 59 links, so a world.h touch rebuilt in 118
# to 186 s; mold links each in well under a second (the same rebuild: 11 s).
# Linux with mold installed only; kept out of CFLAGS because clangcheck's
# -fsyntax-only rejects a linker flag under -Werror.
# (asked once: a ?= variable is recursive, and ran uname and command -v at
# every link, 122 shells a build)
ifeq ($(origin LDFLAGS),undefined)
LDFLAGS := $(if $(and $(filter Linux,$(shell uname -s)),$(shell command -v ld.mold 2>/dev/null)),-fuse-ld=mold)
endif
OUT     := $(abspath ../out/native)
# lane/polish renamed native/ to csrc/ and src/ to engine/: objects and their
# dependency files from before name the old paths (make stops on a missing
# src/X.c), so they go once; ccache rebuilds them in seconds
$(shell [ -f $(OUT)/.layout-csrc ] || { find $(OUT) \( -name '*.d' -o -name '*.o' -o -name '*.a' \) -delete 2>/dev/null; mkdir -p $(OUT); touch $(OUT)/.layout-csrc; })
# the renderer's textures, made from the client jar (assets.tsv, below)
ASSETS  := $(abspath ../out/assets)
# The region cache's build hash (engine/regioncache.h): every engine source,
# the compiler and its flags (without the checkout's own -include path and
# debug prefix map, which a sub-make's CFLAGS carry: the fncov build's hash
# named its checkout until lane/trialcache), so an image made by other code
# is never read. Kept in a stamp that changes only with the hash, which
# regioncache.o depends on.
RC_BUILD := $(shell { cat engine/*.c engine/*.h; printf '%s\n' '$(filter-out -include %envheap.h -fdebug-prefix-map=%,$(CFLAGS))'; $(CC) --version 2>/dev/null | head -1; } | { sha1sum 2>/dev/null || shasum; } | cut -c1-16)
$(shell mkdir -p $(OUT)/obj; [ "$$(cat $(OUT)/obj/regioncache.build 2>/dev/null)" = "$(RC_BUILD)" ] || echo "$(RC_BUILD)" > $(OUT)/obj/regioncache.build)
# REGIONCACHE=off|verify|DIR: make test's snapshot and interleave replays
# take --region-cache $(REGIONCACHE) (engine/regioncache.h): off builds every
# seed world anew, verify builds it and fails (exit 3) where the cached image
# differs, an absolute DIR keeps the cache there. Unset: the binaries'
# default, on, in ~/.cache/netherite-regioncache.
REGIONCACHE ?=
# every source and test allocates through the current environment's image
# heap when it has one (engine/envheap.h, engine/image.h); by a path relative to
# csrc/, where every compile runs, so no object names its checkout
override CFLAGS += -include engine/envheap.h
# the debug info's compilation directory as ".", not this checkout's path:
# with the relative paths above an object is the same bytes in every
# checkout, which is what lets ccache (below) serve one checkout's compile to
# another. Debug info only: the code, __FILE__ included, is unchanged.
override CFLAGS += -fdebug-prefix-map=$(CURDIR)=.
# Every compile (-c) goes through ccache when it is installed (CCACHE= turns
# it off; optional on the Mac), with its cache in ~/.cache/ccache: a fresh
# checkout (a merge trial's worktree, a lane) builds from it in seconds where
# it compiled every source again (25 to 40 s idle, 60 to 290 s at load 150 to
# 220, lane/trialcache). ccache keys on the preprocessed source, the compiler
# and the flags, and hands back the object the compiler made: an object
# through ccache is byte-equal to one without it (compared, lane/trialcache).
# Links do not go through it.
ifeq ($(origin CCACHE),undefined)
CCACHE     := $(shell command -v ccache 2>/dev/null)
endif
CCACHE_DIR := $(HOME)/.cache/ccache
CCC         = $(if $(CCACHE),CCACHE_DIR=$(CCACHE_DIR) $(CCACHE) )$(CC)
# one section per function: a call between two functions of a file is then
# a relocation that names the callee, not a distance the assembler fixed, so
# the test selection's hash of a function (tests/fnkey.c) does not move when
# a function before it in the file changes size. Placement only: the same
# instructions.
override CFLAGS += -ffunction-sections

# Every source compiles once to an object (with its header dependencies) and
# every tool links the archive; a new engine/*.c or tests/*.c needs no edit here,
# so parallel lanes do not collide on this file. Compiling every source into
# every tool took 276 s serially for 19 tools; this is one compile per source.
# bounded: many lane checkouts build at once on one box
JOBS ?= 16
MAKEFLAGS += -j$(JOBS)
# the compile flags as a file every object depends on: make alone does not
# rebuild when CFLAGS change (-ffunction-sections above reached no existing
# object without it)
FLAGSTAMP := $(OUT)/obj/flags.stamp
# (with the checkout's path as ".": an object is the same bytes in every
# checkout, so the objects make lane copies from the main checkout stay good;
# the path in it made every new lane compile everything again)
$(shell mkdir -p $(OUT)/obj && { printf '%s\n' '$(subst $(CURDIR),.,$(CC) $(CFLAGS)) ' | cmp -s - $(FLAGSTAMP) || printf '%s\n' '$(subst $(CURDIR),.,$(CC) $(CFLAGS)) ' > $(FLAGSTAMP); })
SRC     := $(sort $(filter-out engine/dev.c,$(wildcard engine/*.c)))
OBJ     := $(patsubst engine/%.c,$(OUT)/obj/%.o,$(SRC))
LIB     := $(OUT)/libnetherite.a
DEVOBJ  := $(OUT)/obj/dev.o
BIN     := $(patsubst tests/%.c,$(OUT)/%,$(sort $(filter-out tests/input_guard.c tests/allocount.c tests/fnkey.c,$(wildcard tests/*.c))))

.PHONY: all test clean clangcheck

# the divergence report (tests/diverge.sh), run as out/native/diverge,
# several recordings replayed at once (tests/recs.sh), as out/native/recs, and
# one command template over several items at once (tests/par.sh), out/native/par
all: $(BIN) $(OUT)/diverge $(OUT)/recs $(OUT)/par $(ASSETS)/.stamp

# Language values come from the same user-owned jar as the textures.
$(OUT)/langgen: ../tools/langgen.c
	@mkdir -p $(OUT)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -o $@ $<
$(OUT)/lang_generated.h: engine/lang_keys.txt ../out/java/client/1.7.10.jar $(OUT)/langgen
	@mkdir -p $(OUT)
	unzip -p ../out/java/client/1.7.10.jar assets/minecraft/lang/en_US.lang > $(OUT)/en_US.lang.tmp
	$(OUT)/langgen engine/lang_keys.txt $(OUT)/en_US.lang.tmp $@.tmp
	mv $@.tmp $@
	rm -f $(OUT)/en_US.lang.tmp
$(OUT)/obj/lang.o: $(OUT)/lang_generated.h

$(OUT)/diverge: tests/diverge.sh
	@mkdir -p $(OUT)
	cp $< $@ && chmod +x $@

$(OUT)/recs: tests/recs.sh
	@mkdir -p $(OUT)
	cp $< $@ && chmod +x $@

$(OUT)/par: tests/par.sh
	@mkdir -p $(OUT)
	cp $< $@ && chmod +x $@

$(OUT)/obj/%.o: engine/%.c engine/envheap.h $(FLAGSTAMP)
	@mkdir -p $(OUT)/obj
	$(CCC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(OUT)/obj/regioncache.o: $(OUT)/obj/regioncache.build
$(OUT)/obj/regioncache.o: override CFLAGS += -DREGIONCACHE_BUILD='"$(RC_BUILD)"'

$(LIB): $(OBJ)
	@rm -f $@
	ar rcs $@ $(OBJ)

$(DEVOBJ): engine/dev.c engine/envheap.h $(FLAGSTAMP)
	@mkdir -p $(OUT)/obj
	$(CCC) $(CFLAGS) -DNETHERITE_DEV -MMD -MP -c -o $@ $<

$(OUT)/test_registry: tests/test_registry.c $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm

# the gates (test_*) run behind input_guard's fail-closed preflight and carry
# the dev ops (NETHERITE_DEV); the tools beside them (raster, trace_diff,
# hud_live_test) are plain programs. input_guard.c and play.c compile to
# objects of their own: the test selection (tests/fnkey.c) hashes each
# function in the objects a program links.
$(OUT)/obj/input_guard.o: tests/input_guard.c engine/envheap.h $(FLAGSTAMP)
	@mkdir -p $(OUT)/obj
	$(CCC) $(CFLAGS) -MMD -MP -c -o $@ $<

# (the object its own target, both named: compiled and linked in one recipe,
# every test object was compiled again whenever the library changed;
# test_registry keeps its own rule above)
TESTSRC := $(filter-out tests/test_registry.c,$(wildcard tests/test_*.c))
TESTOBJ := $(patsubst tests/%.c,$(OUT)/obj/%.o,$(TESTSRC))
$(TESTOBJ): $(OUT)/obj/test_%.o: tests/test_%.c engine/envheap.h $(FLAGSTAMP)
	@mkdir -p $(OUT)/obj
	$(CCC) $(CFLAGS) -DNETHERITE_DEV -Dmain=gate_test_main -MMD -MP -MF $(OUT)/obj/test_test_$*.d -MT $@ -c -o $@ $<
$(patsubst tests/%.c,$(OUT)/%,$(TESTSRC)): $(OUT)/test_%: $(OUT)/obj/test_%.o $(OUT)/obj/input_guard.o $(LIB) $(DEVOBJ) $(FLAGSTAMP)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(OUT)/obj/input_guard.o $(DEVOBJ) $(LIB) -lz -lm

$(OUT)/%: tests/%.c $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm

$(OUT)/%: tests/%.c $(LIB) $(DEVOBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -DNETHERITE_DEV -MMD -MP -MF $(OUT)/obj/test_$*.d -MT $@ -o $@ $< $(DEVOBJ) $(LIB) -lz -lm

-include $(OBJ:.o=.d) $(DEVOBJ:.o=.d) $(patsubst tests/%.c,$(OUT)/obj/test_%.d,$(wildcard tests/*.c)) $(wildcard $(OUT)/obj/play.d $(OUT)/obj/play-dev.d $(OUT)/obj/input_guard.d)

# The playable client (play/play.c) links SDL3, so it stays out of `all` and the
# suite: `make -C csrc play` builds out/native/play; run it from the repo root.
SDL_CFLAGS = $(shell pkg-config --cflags sdl3)
SDL_LIBS   = $(shell pkg-config --libs sdl3)
.PHONY: play
GUI_SHEETS := inventory crafting_table furnace generic_54 villager dispenser hopper
GUI_RGBA := $(foreach x,$(GUI_SHEETS),$(OUT)/gui/$(x).rgba)
# GuiWinGame's text: the end poem and the credits, as the jar has them
GUI_TEXTS := $(OUT)/gui/end.txt $(OUT)/gui/credits.txt
play: $(GUI_RGBA) $(GUI_TEXTS) $(OUT)/play
$(OUT)/gui/%.txt: ../out/java/client/1.7.10.jar
	@mkdir -p $(OUT)/gui
	unzip -p $< assets/minecraft/texts/$*.txt > $@.tmp && mv $@.tmp $@
$(OUT)/gui/%.rgba: ../out/java/client/1.7.10.jar $(OUT)/png2rgba
	@mkdir -p $(OUT)/gui
	unzip -p $< assets/minecraft/textures/gui/container/$*.png > $@.png
	$(OUT)/png2rgba $@.png $@ && rm -f $@.png
$(OUT)/test_render: $(GUI_RGBA) $(GUI_TEXTS)

# out/assets: the renderer's textures, made from the user's client jar (and
# two from play's recorded hud scene) as assets.tsv lists them; no Mojang file
# is committed. The scene's two wait for the scene (make -C oracle render-scenes).
HUDSCENE := ../out/java/render/hud_mixed
.PHONY: assets
assets: $(ASSETS)/.stamp $(OUT)/lang_generated.h
$(ASSETS)/.stamp: assets.tsv ../out/java/client/1.7.10.jar $(OUT)/png2rgba $(wildcard $(HUDSCENE)/atlas.json $(HUDSCENE)/state/gui_items.rgba)
	@test -f ../out/java/client/1.7.10.jar || { echo "assets: no client jar (make -C oracle deps, or netherite setup)"; exit 1; }
	@rm -rf $(ASSETS).tmp && mkdir -p $(ASSETS).tmp && \
	  grep -v '^#' assets.tsv | while IFS='	 ' read -r name kind src; do \
	    mkdir -p "$$(dirname "$(ASSETS).tmp/$$name")"; t=$(ASSETS).tmp/$$name; \
	    case $$kind in \
	      png|rgb) unzip -p ../out/java/client/1.7.10.jar "assets/minecraft/$$src" > "$$t.png" && \
	               $(OUT)/png2rgba $$([ $$kind = rgb ] && echo --rgb) "$$t.png" "$$t" && rm -f "$$t.png" || exit 1;; \
	      scene) [ ! -f $(HUDSCENE)/$$src ] || cp $(HUDSCENE)/$$src "$$t";; \
	      sprite) [ ! -f $(HUDSCENE)/atlas.json ] || $(OUT)/png2rgba --cut $(HUDSCENE)/atlas.rgba \
	               $$(jq -r --arg s "$$src" '[.atlas_width, (.sprites[] | select(.name == $$s) | .x, .y, .w, .h)] | map(tostring) | join(" ")' $(HUDSCENE)/atlas.json) "$$t" || exit 1;; \
	      *) echo "assets.tsv: $$name: unknown source $$kind"; exit 1;; \
	    esac; done
	@rm -rf $(ASSETS) && mv $(ASSETS).tmp $(ASSETS) && touch $@
	@n=$$(find $(ASSETS) -type f ! -name .stamp | wc -l | tr -d ' '); w=$$(grep -vc '^#' assets.tsv); \
	  [ $$n = $$w ] || echo "assets: $$n of $$w ($(HUDSCENE) not recorded yet: make -C oracle render-scenes)"
$(OUT)/obj/play.o: play/play.c play/clientents.c engine/envheap.h $(FLAGSTAMP)
	@mkdir -p $(OUT)/obj
	$(CCC) $(CFLAGS) $(SDL_CFLAGS) -MMD -MP -MF $(OUT)/obj/play.d -MT $@ -c -o $@ $<
$(OUT)/play: $(OUT)/obj/play.o $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(SDL_LIBS) -lz -lm

.PHONY: play-dev
play-dev: $(OUT)/play-dev
$(OUT)/obj/play-dev.o: play/play.c play/clientents.c engine/envheap.h $(FLAGSTAMP)
	@mkdir -p $(OUT)/obj
	$(CCC) $(CFLAGS) -DNETHERITE_DEV $(SDL_CFLAGS) -MMD -MP -MF $(OUT)/obj/play-dev.d -MT $@ -c -o $@ $<
$(OUT)/play-dev: $(OUT)/obj/play-dev.o $(LIB) $(DEVOBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(DEVOBJ) $(LIB) $(SDL_LIBS) -lz -lm

# Every check. Each tests/test_X.c runs on every directory under out/java/D,
# where D is X unless mapped below, or once with no argument when D does not
# exist; all at once, logs in order, any failure fails the target. A new test
# with its own data directory needs no edit here.
# Each out/java/guijudge directory runs tests/gui_judge.sh: the playable
# client's container screens against the oracle's GuiContainer over the same
# raw mouse and key events; each out/java/clientframes directory runs
# tests/frame_judge.sh: its live frames against the oracle's (not the 36
# lane/clientrand re-captured as cr-NAME, retired by lane/framerest: master
# moves them to out/java/clientframes-retired), and so does each
# out/java/clientframes-WxH directory (the observation's goldens,
# oracle/tests/obsframes.sh, judged at that size, tests/frame_budget_WxH.txt); each
# out/java/rawjudge directory runs play/rawjudge.sh: the client on a Java
# session's raw keyboard and mouse events, its rows, screen ops and frames
# against Java's (none from TESTCACHE: play.c is not in its key).
# Snapshot directories also run tests/play_check.sh: the playable client's
# own tick pair (out/native/play --check) over the same rows. Since
# lane/playfix both sides run session.c's tick, so its row comparison is a
# subset of test_snapshots' (positions, w, the server digests, the S02s);
# what only it checks is the code play adds to that tick (play.c's play_tick
# and session hooks, play/clientents.c, the client-side functions only play
# calls) and the client's own view: clientview.jsonl (the clock, chest lids,
# spawners, items, pickups, blasts, leads, equipment) and cwhash.txt (the
# client world's light). lane/playcheck (2026-09-28) found every play-only
# failure in the history in that code or those two files. Which recordings:
# PLAYCHECK=all (or 1) every one; sample the recordings of
# tests/play_check.list (every function play runs that test_snapshots does
# not run on the same recording, and each recording a play-only failure was
# found on) and every one with a clientview.jsonl or cwhash.txt; 0 none;
# auto (the default) all when this tree's play/*.c, play/*.h, engine/session.c,
# engine/session.h or tests/play_check.sh differ from master (or there is no
# git or no master) or TESTCACHE is empty (the full suite: master's
# 12-hourly one, out/flywheel/bin/testmap.sh), else sample. It was 37.6% of the suite's job-seconds
# with every recording (10,893 of 28,933 s, lane/gpurender's trial); the
# sample is 18 recordings (lane/playcheck). make play-check-sample rewrites the list.
PLAYCHECK ?= auto
# A job whose test binary, arguments and recording files are unchanged since
# a passing run replays that run's log instead of running (TESTCACHE, a
# directory; empty turns it off). The key is the binary's SHA-1 with the
# job's flags and the directory's file list, sizes and mtimes: a Java-only
# lane's final suite runs only its new recordings. The merge trial runs
# with TESTCACHE= and stays the full check. GNU find and sha1sum only.
TESTCACHE ?= $(HOME)/.cache/netherite-testcache
TESTCACHE_ON := $(if $(TESTCACHE),$(shell find /dev/null -maxdepth 0 -printf '' 2>/dev/null && command -v sha1sum > /dev/null && echo 1),)
# Test selection (TESTSELECT=1, the default when TESTCACHE is on): a job is
# also replayed from a recorded passing run when every function it executed
# there, every file it read, probed or ran, and its recording's files are
# unchanged. A function's key is its machine code in this build's objects
# with what its relocations name (tests/fnkey.c), so a one-function fix
# reruns only the jobs that ran that function. make test-map records every
# job (tests/testmap.c): it runs each on the function coverage build
# (out/native/fncov, gcc -finstrument-functions-once and tests/fncov_rt.h)
# under strace and writes $(TESTCACHE)/sel; run it on master after a merge.
# A job runs when it has no record, when a function or file it used changed,
# or when fnkey cannot read one of its programs (test_registry, rawjudge.sh
# and Mach-O objects have none). The compiler's version and flags
# (FNKEY_GLOBAL) are in every key.
TESTSELECT ?= 1
# (the path flags above change no code: the key keeps the string it had
# before them, -include ./engine/envheap.h and no prefix map, so no record is lost)
FNKEY_GLOBAL := $(subst $(CURDIR),.,$(CC) $(shell $(CC) -dumpfullversion 2>/dev/null) $(patsubst engine/envheap.h,./src/envheap.h,$(filter-out -fdebug-prefix-map=%,$(CFLAGS))) $(LDFLAGS))
FNKEY_ARGS = --root $(abspath ..) --out $(OUT) --data $(DATA) --global '$(FNKEY_GLOBAL)'
# PLAYCHECK (above) resolved: all, sample or none
PLAYCHECK_MODE := $(if $(filter 1 all,$(PLAYCHECK)),all,$(if $(filter 0 none,$(PLAYCHECK)),none,$(if $(filter sample,$(PLAYCHECK)),sample,$(if $(TESTCACHE),,all)$(shell [ -z "$(TESTCACHE)" ] || { git diff --quiet master -- 'play/*.[ch]' engine/session.c engine/session.h tests/play_check.sh 2>/dev/null && echo sample || echo all; }))))
PLAYCHECK_ON := $(if $(filter none,$(PLAYCHECK_MODE)),0,1)
PLAYCHECK_SAMPLE := $(shell sed 's/\#.*//' tests/play_check.list 2>/dev/null)
DATA     := $(abspath ../out/java)
DATA_worldgen   = chunks
DATA_probe      = probes
DATA_structures = structures
DATA_move       = moves
DATA_feature    = probes
# the logs print in PAIRS order: LONG's tests first, then the rest by name
LONG     := place portals
TESTS    := $(patsubst tests/test_%.c,%,$(sort $(wildcard tests/test_*.c)))
PAIRS    := $(foreach x,$(filter $(LONG),$(TESTS)) $(filter-out $(LONG),$(TESTS)),$(x):$(or $(DATA_$(x)),$(x)))
# The longest single checks start first, so none of them is the last to begin:
# the jobs of RUN_FIRST (a few of 7-14 s each), then RUN_MID (jobs of 1-8 s),
# then the snapshot replays longest tape first, then the rest.
RUN_FIRST := place portals explosions paths projectiles ticks animals
RUN_MID   := sblocks villagers populate hostiles seedworld spawning activate slimes ghasts potions items harvestdrops itemuse

# Bounded test parallelism: the suite is CPU-bound and an unbounded fan-out
# over hundreds of jobs oversubscribes the host. Memory is bounded separately
# by tests/memslot.sh's machine-wide budget (each job reserves its measured
# peak; since lane/worldmem most jobs peak under 300 MB). On the GPU host's 128
# cores the full suite (TESTCACHE=) took 60 s at 24 and 47 s at 48
# (2026-09-26, load about 30); its longest single jobs (golden replays,
# 15-20 s) set the floor.
# a suite job that runs past JOBTIMEOUT seconds is killed and fails (2026-09-30: a merge trial waited
# 40 minutes on two test_relocate jobs a lane had made loop; the longest job takes about 5 minutes)
JOBTIMEOUT ?= 900
TESTJ ?= 48

# SEGMENTS=1: a snapshot recording with keyframes (out/java/keyframes/NAME,
# made by make -C ../oracle keyframes) runs test_snapshots as its
# keyframe-to-keyframe segments in parallel (tests/segments.sh: each
# segment's rows, and its end state against the next keyframe where that
# round-trip was clean when the set was made). Off by default: the suite is
# bound by its total work, and every segment loads a snapshot (lane/keyframes,
# 2026-09-26: TESTJ=32 64.6 s whole against 88.5 s in segments, TESTJ=8
# 169.2 s against 193.6 s, the same results). SEGROWS=N skips keyframes
# closer than N rows. A segment reserves memory (tests/memslot.sh) under its
# recording's name. play_check.sh stays one whole replay either way.
SEGMENTS ?= 0
SEGROWS  ?= 1

# PHASETRACE=1: every snapshot replay and interleaved pair runs with the
# phase tracer on (engine/phase.h, --phase-digest): each prints its trace's
# digest, and a suite with the trace on must pass exactly as one without.
# Not with SEGMENTS (a segment's rows start mid-recording).
PHASETRACE ?= 0

# make phase-check [RECS="name ..."]: the phase tracer's check (--phase-check)
# over snapshot recordings (all of them by default): a phase that changes a
# state class its entry in engine/phase.c does not declare fails, and each
# replay prints how many runs of each phase wrote each class it declares.
RECS ?= $(notdir $(patsubst %/,%,$(wildcard $(DATA)/snapshots/*/)))
.PHONY: phase-check
phase-check: $(OUT)/test_snapshots
	@rm -f $(OUT)/phase-check.*.txt
	@printf '%s\n' $(RECS) | xargs -P $(TESTJ) -I{} sh -c 'bash tests/memslot.sh $(DATA)/snapshots/{} $(OUT)/test_snapshots --phase-check $(DATA)/snapshots/{} > $(OUT)/phase-check.{}.txt 2>&1; echo "{} rc=$$?"' | sort > $(OUT)/phase-check.rc
	@cat $(OUT)/phase-check.*.txt | grep -h '^phase check:' | sort | uniq -c || true
	@echo "phase-check: $$(grep -c 'rc=0' $(OUT)/phase-check.rc) of $$(wc -l < $(OUT)/phase-check.rc) recordings pass; logs $(OUT)/phase-check.NAME.txt"
	@! grep -v 'rc=0' $(OUT)/phase-check.rc

# make phase-kernel-check [PHASES=all] [RECS="name ..."] [VERIFY=N]: the
# per-phase kernel harness (GPU plan L13, tests/phasekernel.h) with its
# stand-in kernel, the C phase itself, over snapshot recordings (all of
# them by default). Each recording's phase trace is made first; then the
# replay runs in an image environment, and at every tested phase's start
# the host's writes go down to a device copy of the image (engine/imgxfer.h:
# env_export, env_import), the copy is moved onto the host's address and the
# phase runs there, its writes come back, and every row's digests must equal
# the trace's. PHASES is labels (S6, 0.S6; comma separated) or all; VERIFY=N
# also compares the whole image with the copy every N rows. Logs
# $(OUT)/phase-kernel.NAME.txt. A CUDA kernel for phase P is checked the same
# way, with the kernel in place of the stand-in.
PHASES ?= all
.PHONY: phase-kernel-check
phase-kernel-check: $(OUT)/test_snapshots
	@rm -f $(OUT)/phase-kernel.*.txt $(OUT)/phase-kernel.*.ref
	@printf '%s\n' $(RECS) | xargs -P $(TESTJ) -I{} bash tests/phase_kernel_check.sh $(OUT) $(DATA)/snapshots/{} "$(PHASES)" "$(VERIFY)" | sort > $(OUT)/phase-kernel.rc
	@grep -v ' rc=0: ' $(OUT)/phase-kernel.rc || true
	@echo "phase-kernel-check ($(PHASES)): $$(grep -c ' rc=0: ' $(OUT)/phase-kernel.rc) of $$(wc -l < $(OUT)/phase-kernel.rc) recordings pass, $$(grep ' rc=0: ' $(OUT)/phase-kernel.rc | sed -n 's/.* rc=0: \([0-9]*\) phase runs.*/\1/p' | awk '{s += $$1} END {print s + 0}') phase runs round-tripped; logs $(OUT)/phase-kernel.NAME.txt"
	@! grep -qv ' rc=0: ' $(OUT)/phase-kernel.rc

# make phase-kernel-cost [PHASES=S6] [RECS="name ..."]: the round trip's
# cost per row with PHASES on the device (pages each way; the host export's
# and import's user and kernel instructions), what a split host/device tick
# pays, over the GPU audit's twelve recordings by default; one at a time
# under the memory budget, then a table.
.PHONY: phase-kernel-cost
phase-kernel-cost: $(OUT)/test_snapshots
	@for r in $(if $(filter command line,$(origin RECS)),$(RECS),$(PROFRECS)); do \
	  bash tests/memslot.sh --key phase-kernel $(DATA)/snapshots/$$r $(OUT)/test_snapshots --phase-kernel $(if $(filter command line,$(origin PHASES)),$(PHASES),S6) \
	    $(DATA)/snapshots/$$r > $(OUT)/phase-kernel-cost.$$r.$(if $(filter command line,$(origin PHASES)),$(PHASES),S6).txt 2>&1; \
	  echo "== $$r rc=$$?"; \
	done
	@bash tests/phase_kernel_table.sh $(OUT) $(if $(filter command line,$(origin PHASES)),$(PHASES),S6) $(if $(filter command line,$(origin RECS)),$(RECS),$(PROFRECS))

# make phase-profile [RECS="name ..."] [ROWS=1]: the phase profiler
# (engine/phaseprof.c, test_snapshots --phase-profile) over snapshot recordings,
# one at a time, with tests/allocount.c preloaded to count heap allocations.
# Per recording and per phase and sub-mark: user-space instructions (the
# process's own counters: the host's load does not move them), TSC time
# (it does), per-tick p50, p99 and max, allocations per tick, the worst
# ticks and what they spent it in; the environment's bytes by owner (--mem,
# engine/envmem.c); then a table of all of them. Logs
# $(OUT)/phase-profile.NAME.txt; ROWS=1 also writes each row's columns to
# $(OUT)/phase-profile.NAME.tsv. Default recordings: the GPU audit's ten
# (out/plan/gpu-audit-2026-09-25.md section 2), the dragon fight and a
# mid-game golden segment.
PROFRECS := play-reload2-s1 gen-walk-s42 end-dragon-s1 fresh-s42 mobspawn-final-s1 persist-village-s42 \
	combat-mobs-s1 fight-skeleton-s1 nether-trip-s1 nether-trip2-s42 gold-g30-s42 gold-g15-s42
$(OUT)/allocount.so: tests/allocount.c
	@mkdir -p $(OUT)
	$(CC) -O2 -shared -fPIC -o $@ $<
# GEN=cuda (or c) profiles with chunk generation served ahead of each step
# (engine/genahead.h; cuda runs out/native/cuda/worldgen/test_snapshots on GPU
# $(CUDA_DEV)), its logs and TSVs named phase-profile.NAME.GEN.
PROF_GEN := $(if $(filter command line,$(origin GEN)),$(GEN),)
PROF_BIN := $(if $(filter cuda,$(PROF_GEN)),$(OUT)/cuda/worldgen/test_snapshots,$(OUT)/test_snapshots)
PROF_SFX := $(if $(PROF_GEN),.$(PROF_GEN),)
.PHONY: phase-profile
phase-profile: $(PROF_BIN) $(OUT)/allocount.so
	@for r in $(if $(filter command line,$(origin RECS)),$(RECS),$(PROFRECS)); do \
	  CUDA_VISIBLE_DEVICES=$(CUDA_DEV) bash tests/memslot.sh --key test_snapshots $(DATA)/snapshots/$$r env LD_PRELOAD=$(OUT)/allocount.so \
	    $(PROF_BIN) --phase-profile --mem$(if $(PROF_GEN), --gen-ahead $(PROF_GEN))$(if $(ROWS), --phase-profile-rows $(OUT)/phase-profile.$$r$(PROF_SFX).tsv) \
	    $(DATA)/snapshots/$$r > $(OUT)/phase-profile.$$r$(PROF_SFX).txt 2>&1; rc=$$?; \
	  echo "== $$r rc=$$rc $$(grep -c . $(OUT)/phase-profile.$$r$(PROF_SFX).txt) lines"; [ $$rc = 0 ] || tail -3 $(OUT)/phase-profile.$$r$(PROF_SFX).txt; \
	done
	@bash tests/phase_profile_table.sh $(OUT) $(addsuffix $(PROF_SFX),$(if $(filter command line,$(origin RECS)),$(RECS),$(PROFRECS)))

# The tick's writable globals against tests/globals.allow: two environments
# in one process share every one (engine/env.h says where state goes instead).
.PHONY: globals
globals: all
	@bash tests/globals.sh

# The containers whose iteration order is game state, against the JDK 8
# emulations they replaced (tests/order_test.c, GPU audit L4).
.PHONY: order
order: $(OUT)/order_test
	@$(OUT)/order_test

# The parked chunks' and spill file's codec (engine/lzpack.h): round trips of
# every path of its format, and damaged streams refused inside their buffer
# (tests/lzpack_check.c, lane/spillfast).
.PHONY: lzpack-check
lzpack-check: $(OUT)/lzpack_check
	@$(OUT)/lzpack_check

# A pool worker's thread scratch shared by two environments: nothing in it
# keeps one environment's slab block past its call (tests/scratch_check.c,
# lane/slabfix).
.PHONY: scratch-check
scratch-check: $(OUT)/scratch_check
	@$(OUT)/scratch_check

# config.yaml's C reader (engine/worldconf.h) against the file, its grammar, and
# the start each config names (tests/worldconf_check.c, lane/worldyaml).
.PHONY: worldconf-check
worldconf-check: $(OUT)/worldconf_check
	@$(OUT)/worldconf_check ..

# The render scenes and frame views the oracle no longer replayed, re-recorded
# by lane/rerender (tests/retired_scenes.txt, AREA/NAME: test:NAME here);
# master moves them to out/java/render-retired and rendertick-retired, and the
# suite skips them until then.
RETIRED_SCENES := $(shell grep -v '^\#' tests/retired_scenes.txt | tr / : | paste -sd'|')

# make test's jobs, written to $$t/jobs (N SORTKEYS RUNNER DIR TAG [FLAGS]) and
# $$t/order (N RUNNER DIR TAG [FLAGS], in the order they start); make
# test-map records the same jobs. A snapshot recording is three: its replay
# (test_snapshots DIR), play_check.sh, and its load check (test_snapshots
# --load-check DIR: the chunks' bytes and hashes, the NBT round-trips), which
# test selection reruns only when the snapshot's files or the loading code
# change; with SEGMENTS, each keyframe a segment starts from has one too.
SUITE_JOBS = n=0; : > $$t/jobs; \
	  for p in $(PAIRS); do x=$${p%%:*}; d=$(DATA)/$${p\#*:}; \
	    if [ -d "$$d" ]; then for s in "$$d"/*/; do [ -d "$$s" ] || continue; \
	      case "$$x:$$(basename "$$s")" in $(RETIRED_SCENES)) continue;; esac; \
	      if [ "$$x" = feature ] || [ "$$x" = probe ]; then \
	        kind=$$(jq -r '.kind // "setblock"' "$$s/manifest.json"); \
	        [ "$$x" != feature ] || [ "$$kind" = feature ] || continue; \
	        [ "$$x" != probe ] || [ "$$kind" != feature ] || continue; \
	      fi; \
	      case " $(RUN_FIRST) " in *" $$x "*) k="0 0";; *) case " $(RUN_MID) " in *" $$x "*) k="1 0";; *) k="3 0";; esac;; esac; \
	      [ "$$x" != snapshots ] || k="2 -$$(( $$(wc -c < "$$s/tape.jsonl" 2>/dev/null || echo 0) ))"; \
	      pc=0; b=$${s%/}; b=$${b\#\#*/}; [ "$$x" != snapshots ] || case "$(PLAYCHECK_MODE)" in all) pc=1;; sample) \
	        case " $(PLAYCHECK_SAMPLE) " in *" $$b "*) pc=1;; esac; [ -f "$$s/clientview.jsonl" ] || [ -f "$$s/cwhash.txt" ] && pc=1;; esac; \
	      if [ $$pc = 1 ]; then n=$$((n+1)); \
	        printf '%s %s %s %s -\n' "$$n" "$$k" "$(CURDIR)/tests/play_check.sh" "$$s" >> $$t/jobs; fi; \
	      if [ "$$x" = snapshots ]; then n=$$((n+1)); \
	        printf '%s 3 0 %s %s - --load-check\n' "$$n" "$(OUT)/test_$$x" "$$s" >> $$t/jobs; fi; \
	      if [ "$$x" = snapshots ] && [ "$(SEGMENTS)" != 0 ] && bash tests/segments.sh "$$s" $(SEGROWS) > $$t/seg; then \
	        while read -r rows tag flags; do n=$$((n+1)); \
	          printf '%s 2 -%s %s %s %s%s\n' "$$n" "$$((rows * 1000))" "$(OUT)/test_$$x" "$$s" "$$tag" "$${flags:+ $$flags}" >> $$t/jobs; \
	          case " $$flags " in *" --keyframe "*) n=$$((n+1)); \
	            printf '%s 3 0 %s %s l%s --keyframe %s --load-check\n' "$$n" "$(OUT)/test_$$x" "$$s" "$$tag" \
	              "$$(printf '%s\n' "$$flags" | sed 's/.*--keyframe \([^ ]*\).*/\1/')" >> $$t/jobs;; esac; \
	        done < $$t/seg; continue; fi; \
	      n=$$((n+1)); f=; case "$(PHASETRACE):$$x" in 1:snapshots|1:interleave) f=" --phase-digest";; esac; \
      case "$(REGIONCACHE):$$x" in :*) ;; *:snapshots|*:interleave) f="$$f --region-cache $(REGIONCACHE)";; esac; \
	      printf '%s %s %s %s -%s\n' "$$n" "$$k" "$(OUT)/test_$$x" "$$s" "$$f" >> $$t/jobs; done; \
	    else n=$$((n+1)); printf '%s 3 0 %s - -\n' "$$n" "$(OUT)/test_$$x" >> $$t/jobs; fi; done; \
	  for s in $(DATA)/guijudge/*/; do [ -d "$$s" ] || continue; n=$$((n+1)); \
	    printf '%s 1 0 %s %s -\n' "$$n" "$(CURDIR)/tests/gui_judge.sh" "$$s" >> $$t/jobs; done; \
	  for s in $(DATA)/clientframes/*/ $(DATA)/clientframes-[0-9]*x[0-9]*/*/; do [ -d "$$s" ] || continue; \
	    case $$(basename "$$s") in sweep-gold-g*-s42|sweep-fresh-play-s1|game-s1|drops-s1|hang-s1|pickup-s1|screens-s1) continue;; esac; n=$$((n+1)); \
	    printf '%s 1 0 %s %s -\n' "$$n" "$(CURDIR)/tests/frame_judge.sh" "$$s" >> $$t/jobs; done; \
	  for s in $(DATA)/rawjudge/*/; do [ -f "$$s/tape.raw.jsonl" ] && [ -f "$$s/start" ] || continue; n=$$((n+1)); \
	    printf '%s 1 0 %s %s - --in-budget\n' "$$n" "$(CURDIR)/play/rawjudge.sh" "$$s" >> $$t/jobs; done; \
	  sort -s -k2,2n -k3,3n $$t/jobs | cut -d' ' -f1,4- > $$t/order

test: all globals order lzpack-check scratch-check worldconf-check $(OUT)/play $(OUT)/play-dev $(OUT)/fnkey
	@t=$$(mktemp -d); $(SUITE_JOBS); \
	  C=; if [ "$(TESTCACHE_ON)" = 1 ]; then C="$(TESTCACHE)"; mkdir -p "$$C"; \
	    for f in $$(cut -d' ' -f2 $$t/order | sort -u); do case $${f##*/} in test_*) [ -f "$$f" ] && sha1sum < "$$f" | cut -c1-16 > "$$t/bh.$${f##*/}";; \
	      play_check.sh) cat "$$f" $(OUT)/play $(OUT)/play-dev | sha1sum | cut -c1-16 > "$$t/bh.play_check.sh";; esac; done; fi; \
	  : > $$t/selok; if [ -n "$$C" ] && [ "$(TESTSELECT)" = 1 ] && [ -x $(OUT)/fnkey ]; then \
	    $(OUT)/fnkey check $(FNKEY_ARGS) --store "$$C/sel" --into $$t < $$t/order > $$t/selok 2> $$t/sel.err || : > $$t/selok; fi; \
	  awk 'FILENAME == ARGV[1] {s[$$1]; next} !($$1 in s)' $$t/selok $$t/order > $$t/run; \
	  C=$$C T=$$t xargs -r -P $(TESTJ) -L1 sh -c '\
	    n=$$1; b=$$2; d=$$3; g=$$4; shift 4; s=$$(date +%s%N); key=; \
	    if [ -n "$$C" ] && [ "$$d" != "-" ] && [ -f "$$T/bh.$${b##*/}" ]; then \
	      key="$$C/$$(cat "$$T/bh.$${b##*/}")-$$(printf "%s " "$$g" "$$@" | sha1sum | cut -c1-8)-$$(find -L "$$d" -type f -printf "%P %s %T@\\n" 2>/dev/null | sort | sha1sum | cut -c1-16)"; \
	      if [ -f "$$key" ]; then cp "$$key" "$$T/$$n"; echo 0 > "$$T/$$n.rc"; : > "$$T/$$n.hit"; exit 0; fi; fi; \
	    if [ "$$d" = "-" ]; then timeout -k 10 $(JOBTIMEOUT) "$$b" > "$$T/$$n" 2>&1; \
	    else timeout -k 10 $(JOBTIMEOUT) bash tests/memslot.sh --t "$$T/$$n.acq" "$$d" "$$b" "$$@" "$$d" > "$$T/$$n" 2>&1; fi; \
	    rc=$$?; echo $$rc > "$$T/$$n.rc"; e=$$(date +%s%N); a=$$(cat "$$T/$$n.acq" 2>/dev/null || echo $$s); \
	    if [ $$rc = 0 ] && [ -n "$$key" ]; then cp "$$T/$$n" "$$key.$$$$" && mv "$$key.$$$$" "$$key"; fi; \
	    case $$s$$e$$a in *N*) ;; *) echo "$$(( (e - a) / 1000000 )) $$(( (a - s) / 1000000 )) $${b##*/} $$d$$([ "$$g" = "-" ] || echo " $$g")" > "$$T/$$n.ms";; esac' _ < $$t/run; \
	  seq -f "$$t/%g" 1 $$n | xargs cat; \
	  fail=0; for i in $$(seq 1 $$n); do rc=; read -r rc < $$t/$$i.rc; [ "$$rc" = 0 ] || { fail=1; \
	    echo "FAILED job $$(grep "^$$i " $$t/jobs | cut -d' ' -f4- | sed 's/ -$$//') rc=$$rc" >> $$t/failed; }; done 2> /dev/null; \
	  [ ! -s $$t/failed ] || { echo; cat $$t/failed; }; \
	  h=$$(ls $$t/*.hit 2>/dev/null | wc -l); sh=$$(wc -l < $$t/selok); [ $$h = 0 ] || echo "($$h of $$n jobs replayed from the test cache $(TESTCACHE):"\
	    "$$sh ran no function or file that changed since a recorded pass (make test-map), $$((h - sh)) the same binary; TESTCACHE= runs them all)"; \
	  cat $$t/*.ms 2>/dev/null | sort -rn > $(OUT)/suite-times.txt; \
	  rm -rf $$t; exit $$fail

$(OUT)/fnkey: tests/fnkey.c
	@mkdir -p $(OUT)
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -o $@ $<

# The repository's tools, in C since lane/cport (they were Python), each
# beside what it serves and sharing tests/tool.h: tests/*.c's tools build by
# the rule above (callgraph, smoke_select, testmap, testsel_history,
# fncov_recs, play_check_sample); these live elsewhere. Every one is in `all`.
TOOLSRC := ../tools/index_check.c ../tools/pxdiff.c cuda/mirror.c tests/playfuzz/gen.c tests/playfuzz/ends.c \
           tests/playfuzz/agentgen.c tests/playfuzz/coverage.c tests/rawfuzz/gen.c
TOOLBIN := $(OUT)/index_check $(OUT)/pxdiff $(OUT)/mirror $(OUT)/playfuzz_gen $(OUT)/playfuzz_ends \
           $(OUT)/playfuzz_agentgen $(OUT)/playfuzz_coverage $(OUT)/rawfuzz_gen
all: $(TOOLBIN)
$(TOOLBIN) $(patsubst tests/%.c,$(OUT)/%,$(wildcard $(addprefix tests/,callgraph.c smoke_select.c testmap.c testsel_history.c fncov_recs.c play_check_sample.c))): tests/tool.h
$(OUT)/index_check: ../tools/index_check.c $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm
$(OUT)/pxdiff: ../tools/pxdiff.c $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm
$(OUT)/mirror: cuda/mirror.c $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm
$(OUT)/playfuzz_gen: tests/playfuzz/gen.c tests/playfuzz/playgen.h $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm
$(OUT)/playfuzz_ends: tests/playfuzz/ends.c tests/playfuzz/playgen.h $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm
$(OUT)/playfuzz_agentgen: tests/playfuzz/agentgen.c $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm
$(OUT)/playfuzz_coverage: tests/playfuzz/coverage.c $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm
$(OUT)/rawfuzz_gen: tests/rawfuzz/gen.c $(LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) -lz -lm
.PHONY: index-check
# make index-check [ARGS=--summary]: tools/index_check.c over every record (the merge trial's index gate)
index-check: $(OUT)/index_check
	@$(OUT)/index_check $(ARGS)

# make test-map: the test selection's records (TESTSELECT above) for this
# tree: every job of make test (the play checks too) that has no record this
# tree reproduces runs on the coverage build (MAPALL=1: every job);
# MAPARGS="--only REGEX" records only the matching jobs.
# (the entry hook changes what gcc inlines, so -Wformat-truncation warns
# where the normal build does not: raster_hud.c's tooltip_lines, 2026-09-27)
FNCOV := $(OUT)/fncov
.PHONY: fncov test-map
fncov:
	@$(MAKE) --no-print-directory OUT=$(FNCOV) CFLAGS="$(CFLAGS) -Wno-error=format-truncation -finstrument-functions-once -include tests/fncov_rt.h" \
	  all $(FNCOV)/play $(FNCOV)/play-dev
# make coverage: the functions of csrc/engine and csrc/play each snapshot
# recording runs (test_snapshots and play_check on the fncov build), and the
# ones none runs, crossed with index/csrc (tests/fncov_recs.c); writes
# out/native/coverage. RECS="a b" (names under out/java/snapshots or
# recording directories) runs only those; J at a time (default TESTJ).
.PHONY: coverage
coverage: fncov $(OUT)/fncov_recs
	@$(OUT)/fncov_recs --root $(abspath ..) --fncov $(FNCOV) \
	  --data $(DATA) --out $(OUT)/coverage --jobs $(or $(J),$(TESTJ)) $(if $(filter command line,$(origin RECS)),$(RECS))
test-map: PLAYCHECK_MODE := all
test-map: all $(OUT)/play $(OUT)/play-dev $(OUT)/fnkey fncov
	@t=$$(mktemp -d); $(SUITE_JOBS); : > $$t/have; \
	  [ "$(MAPALL)" = 1 ] || $(OUT)/fnkey check $(FNKEY_ARGS) --store $(TESTCACHE)/sel < $$t/order > $$t/have; \
	  $(OUT)/testmap $(FNKEY_ARGS) --store $(TESTCACHE)/sel --fncov $(FNCOV) \
	    --order $$t/order --skip $$t/have --jobs $(TESTJ) --path "$$PATH" $(MAPARGS); rc=$$?; rm -rf $$t; exit $$rc

# make smoke: a refactor lane's check after each edit, under 15 s (make test
# stays the check before a commit). tests/smoke.sh runs the globals and order
# checks and tests/smoke.list's jobs at once, never from TESTCACHE; the play
# checks as make test's PLAYCHECK. make smoke-select rewrites the list from
# measurements (tests/smoke_select.c: every test binary, every (phase, state
# class) pair any recording writes, then engine/ lines per second on a coverage
# build, out/native/cov; cached in out/native/smoke, about 30 minutes when
# every job reruns): after recordings or tests are added. SELECT="--budget S"
# etc.; SMOKELIST=FILE runs another list.
.PHONY: smoke smoke-select
smoke: all $(OUT)/scratch_check $(if $(filter 1,$(PLAYCHECK_ON)),$(OUT)/play $(OUT)/play-dev)
	@bash tests/smoke.sh $(OUT) $(DATA) $(TESTJ) $(PLAYCHECK_ON) $(SMOKELIST)
smoke-select: all
	@$(MAKE) --no-print-directory OUT=$(OUT)/cov CFLAGS="$(CFLAGS) --coverage" all
	@$(OUT)/smoke_select --jobs $(TESTJ) $(SELECT)

# make play-check-sample: rewrite tests/play_check.list's cover section
# (tests/play_check_sample.c) from the test selection's records and the last
# suite's times; after make test-map, when recordings or client code change.
.PHONY: play-check-sample
play-check-sample: $(OUT)/play_check_sample
	@$(OUT)/play_check_sample --store $(TESTCACHE)/sel --data $(DATA) --times $(OUT)/suite-times.txt

# The light re-check memo's check build: world.c with NETHERITE_LIGHT_MEMO_CHECK,
# where every func_150809_p retry the memo skips is run in full anyway and the
# light bytes of its 5x5 chunks are compared before and after (world.c).
LMCHECK ?= play-reload2-s1 mobspawn-final-s1 combat-mobs-s1 persist-village-s42
.PHONY: lightmemo-check
lightmemo-check:
	@$(MAKE) --no-print-directory OUT=$(OUT)/lmcheck CFLAGS="$(CFLAGS) -DNETHERITE_LIGHT_MEMO_CHECK" $(OUT)/lmcheck/test_snapshots
	@for r in $(LMCHECK); do echo "== $$r"; $(OUT)/lmcheck/test_snapshots $(DATA)/snapshots/$$r || exit 1; done

# The still-move memo's check build: entity.c with NETHERITE_MOVE_MEMO_CHECK,
# where every move, cell pass and fire test the memo would skip runs in full
# and must give the memo's answers (the build aborts on the first that does
# not). MMCHECK=all runs every recording under $(DATA)/snapshots, J at a time.
MMCHECK ?= cov-villagerep-s42 fight-zombie-s1 fight-skeleton-s1 combat-mobs-s1 persist-village-s42
.PHONY: move-memo-check
move-memo-check:
	@$(MAKE) --no-print-directory OUT=$(OUT)/mmcheck CFLAGS="$(CFLAGS) -DNETHERITE_MOVE_MEMO_CHECK" $(OUT)/mmcheck/test_snapshots
	@if [ "$(MMCHECK)" = all ]; then ls -d $(DATA)/snapshots/*/; else for r in $(MMCHECK); do echo $(DATA)/snapshots/$$r; done; fi | \
	  xargs -P $(or $(J),16) -I{} sh -c '$(OUT)/mmcheck/test_snapshots {} >/dev/null 2>$(OUT)/mmcheck/$$(basename {}).err; echo "$$? $$(basename {})"' | \
	  sort | awk '{n++; if ($$1 != 0) {f++; print "FAIL " $$2}} END {print "move-memo-check: " n - f "/" n " rc 0"; exit f > 0}'

# The mob model cull's check (raster_mobs.c mob_off_view): play and play-dev
# built with NETHERITE_MOB_CULL_CHECK draw every culled model as a probe
# that aborts on a quad not wholly outside one clip plane, over every frame
# judge (make test's list; MCCHECK="DIR ..." for some). The pipeline's views:
# make -C runtime pipe-gate RT_OUT=... with the flag in RT_CFLAGS.
.PHONY: mob-cull-check
mob-cull-check:
	@$(MAKE) --no-print-directory OUT=$(OUT)/mccheck CFLAGS="$(CFLAGS) -DNETHERITE_MOB_CULL_CHECK" play $(OUT)/mccheck/play-dev
	@if [ -n "$(MCCHECK)" ]; then printf '%s\n' $(MCCHECK); else for s in $(DATA)/clientframes/*/ $(DATA)/clientframes-[0-9]*x[0-9]*/*/; do \
	  case $$(basename "$$s") in sweep-gold-g*-s42|sweep-fresh-play-s1|game-s1|drops-s1|hang-s1|pickup-s1|screens-s1) continue;; esac; echo "$$s"; done; fi | \
	  xargs -P $(or $(J),8) -I{} sh -c 'n=$$(basename {}); [ -f {}/obs ] && n=$$(basename $$(dirname {}))-$$n; bash tests/frame_judge.sh {} $(OUT)/mccheck > $(OUT)/mccheck/$$n.log 2>&1; echo "$$? $$n"' | \
	  sort | awk '{n++; if ($$1 != 0) {f++; print "FAIL " $$2}} END {print "mob-cull-check: " n - f "/" n " frame judges rc 0"; exit f > 0}'

# The worklists' depth check: every explicit stack that replaced a recursion
# in the tick (engine/blockwl.h) notes the deepest it went in a build with
# NETHERITE_WL_DEPTH; tests/wldepth.sh runs every test over its data and
# names the run that went deepest for each.
.PHONY: worklist-depth
worklist-depth:
	@$(MAKE) --no-print-directory OUT=$(OUT)/wldepth CFLAGS="$(CFLAGS) -DNETHERITE_WL_DEPTH" all
	@bash tests/wldepth.sh $(OUT)/wldepth $(DATA)

# The tick's call graph: a build without inlining that writes gcc's
# -fcallgraph-info next to each object, and tests/callgraph.c, which lists
# the cycles reachable from session_tick (worldgen cut) and fails on any
# outside worldgen. POINTERS=1 also resolves the function pointer calls
# (reported, not gated).
.PHONY: callgraph
callgraph: $(OUT)/callgraph
	@$(MAKE) --no-print-directory OUT=$(OUT)/cg CFLAGS="$(CFLAGS) -fno-inline -fno-ipa-cp -fno-ipa-sra -fno-ipa-icf -fcallgraph-info=su" $(OUT)/cg/libnetherite.a
	@$(OUT)/callgraph $(OUT)/cg/obj src $(if $(POINTERS),--pointers)

# The stack budget (GPU audit L8): every frame the tick roots reach
# (session_tick and the world, server player and client ticks, worldgen
# included) at most 4 KB and the worst chain from each root under 32 KB,
# from gcc's -fstack-usage numbers over the call graph (tests/callgraph.c
# --stack, function pointer calls resolved: an over-approximation, so an
# upper bound), in both the callgraph build (no inlining: each source
# function its own frame) and the -O2 build the tools use (inlined frames
# merged). A local too big for a frame goes on the env's scratch stack
# (engine/envstack.h).
.PHONY: stack
stack: $(OUT)/callgraph
	@$(MAKE) --no-print-directory OUT=$(OUT)/cg CFLAGS="$(CFLAGS) -fno-inline -fno-ipa-cp -fno-ipa-sra -fno-ipa-icf -fcallgraph-info=su" $(OUT)/cg/libnetherite.a
	@$(MAKE) --no-print-directory OUT=$(OUT)/stk CFLAGS="$(CFLAGS) -fcallgraph-info=su" $(OUT)/stk/libnetherite.a
	@echo "== no inlining"; $(OUT)/callgraph $(OUT)/cg/obj src --stack --pointers; a=$$?; \
	  echo "== -O2"; $(OUT)/callgraph $(OUT)/stk/obj src --stack --pointers; b=$$?; \
	  [ $$a$$b = 00 ]

# The Mac builds with clang; the GPU host's gcc accepts things clang rejects (a Java
# 'D' float suffix, for one). The merge trial runs this so master stays
# buildable on both.
clangcheck: $(OUT)/lang_generated.h
	@clang -fsyntax-only $(filter-out -g,$(CFLAGS)) -DNETHERITE_DEV $(SRC) $(filter-out tests/input_guard.c,$(wildcard tests/*.c)) $(TOOLSRC) && echo "clangcheck ok"

clean:
	rm -rf $(OUT)

# The device code (csrc/cuda, one folder a part: worldgen, lighting,
# meshing, render, tick; include the C library's stand-ins the clang device
# builds read). Every part needs nvcc (the tick and the mesher clang 21 with
# NVPTX too), so none is in `all`, `test` or clangcheck: each keeps its
# rules and its gpu-PART, gpu-PART-check targets in cuda/PART/PART.mk. --fmad=false
# and IEEE division and square root keep the kernels' float rounding the C
# engine's (-ffp-contract=off).
NVCC      ?= /usr/local/cuda/bin/nvcc
# GPU work runs on the dev host's RTX 3090, device 0 (Elliot, 2026-09-27; the GPU host's GPU 0 before)
CUDA_DEV  ?= 0
# the device's own architecture (sm_86 on the dev host, sm_120 on the GPU host), asked once
# and only by a make that uses it; sm_120 when nvidia-smi answers nothing
CUDA_ARCH ?= $(eval CUDA_ARCH := sm_$(or $(shell nvidia-smi --query-gpu=compute_cap --format=csv,noheader -i $(CUDA_DEV) 2>/dev/null | tr -d .),120))$(CUDA_ARCH)
# Every device build (the generators, the light engine, the renderer, the
# judges' framedev, the mesher) compiles its device code at NVOPT: -Xcicc -O1
# by default, the correctness build (the gates are equality with C, and the
# light engine's unit took 4.4 s where cicc's default -O3 took 20 s at load
# 180, 2026-09-29); PERF=1 builds at cicc's -O3 for performance runs. Every
# kernel's gate is byte-identical at both (lane/cudasplit). The objects
# depend on NVSTAMP, which changes with the level, so switching rebuilds
# them (from ccache, below, when that level was built before).
# PERF=1 (-O3) is the default since 2026-09-29 (Elliot: the performance phase times nearly every
# run, and a -O1 default made lanes build twice and switch levels); PERF=0 is the -O1 build for
# quick correctness iteration. ccache keeps both.
PERF      ?= 1
NVOPT     := $(if $(filter 1,$(PERF)),,-Xcicc -O1)
NVSTAMP   := $(OUT)/obj/nvopt.stamp
$(shell mkdir -p $(OUT)/obj && { printf '%s\n' '$(NVOPT)' | cmp -s - $(NVSTAMP) || printf '%s\n' '$(NVOPT)' > $(NVSTAMP); })
# the device code's rounding and level, every part's (recursive: CUDA_ARCH
# is asked only when a device build expands it)
NVDEV      = -O3 -std=c++17 -arch=$(CUDA_ARCH) --fmad=false -prec-div=true -prec-sqrt=true -ftz=false \
             -Xcompiler -ffp-contract=off $(NVOPT) $(NVSPLIT)
# nvcc splits one unit's optimizer (cicc) and assembler (ptxas) work over
# NVSPLITN threads: render_kern.cu at -O3 took 30.3 s and takes 17.7 on the GPU host
# (cicc 13.7 to 7.9 s, ptxas 16.1 to 9.3), the device output still gated
# byte for byte (2026-09-29, lane/splitcompile). NVSPLITN=1 turns it off.
NVSPLITN  ?= 16
NVSPLIT    = $(if $(filter-out 1,$(NVSPLITN)),--split-compile=$(NVSPLITN))
# Every nvcc compile (-c) goes through ccache as the C compiles do (CCC): an
# unchanged unit is a hit in any checkout (a merge trial's worktree, a new
# lane); nvcc's objects name no checkout (relative sources, no -g).
NVCCC      = $(if $(CCACHE),env CCACHE_DIR=$(CCACHE_DIR) $(CCACHE) )$(NVCC)
# Units compile in parallel (make -j), each under the machine-wide memory
# budget (tests/memslot.sh, key nvcc: cicc takes about a GB a unit, and did
# 8.8 GB on worldgen's units as one file); they were chained one at a time for
# the GPU host's small slice. The key is the unit's folder and file (worldgen_host).
NVJOB      = bash tests/memslot.sh --key nvcc $(subst /,_,$(patsubst cuda/%,%,$(basename $<))) $(NVCCC)
NVFLAGS   ?= $(NVDEV) -rdc=true -Xcompiler -Wall
# each device part under cuda/ keeps its own rules in cuda/PART/PART.mk (the
# device renderer: cuda/render/render.mk, lane/gpurender), so lanes add
# device code without editing this file; the objects are
# $(OUT)/obj/cuda_PART_FILE.o, so two parts may each have a host.cu
-include $(wildcard cuda/*/*.mk)
# The mirror rule (GPU design 2026-09-28 part 6): cuda/mirror.tsv names every
# device kernel, the C it mirrors and its gates; cuda/mirror.c maps a change's
# C items to the kernels it reaches. `make -C csrc mirror-gates [BASE=master]
# [REV=rev] [RUN=1] [FULL=1]` prints (RUN=1: runs, from the repository root)
# the build and quick gates of every kernel the change from BASE to REV (the
# working tree without REV) reaches; FULL=1 every kernel's full gates (the
# nightly's). `make -C csrc mirror-check`: every root, file and gate the
# manifest names exists.
BASE ?= master
.PHONY: mirror-gates mirror-check
mirror-gates: $(OUT)/mirror
	@$(OUT)/mirror --base $(BASE) $(if $(REV),--head $(REV),--worktree) $(if $(filter 1,$(RUN)),--run) \
	  $(if $(filter 1,$(FULL)),--full)
mirror-check: $(OUT)/mirror
	@$(OUT)/mirror --check
