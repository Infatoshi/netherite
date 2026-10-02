# The Java oracle, compiled with plain javac. Everything built, downloaded or
# recorded lands in ../out/java.
#   src/     Minecraft 1.7.10 client source (MCP decompile + 14 hook edits), one
#            folder per package (src/block is package net.minecraft.block);
#            generated, never committed: decomp/src.sh makes it from the
#            pristine decompile and decomp/oracle.patch (make src, every build)
#   harness/    our code: the netherite.oracle harness the hooks call into
#   decomp/  how src/ is made from the owned jar: oracle.patch is every edit
#   tests/   the gate suite and its inputs
#
#   make deps      libraries, client jar (resources), natives, assets link
#   make           compile src/ + harness/ (a few seconds)
#   make gate      all gates (Linux)
#   make play SEED=1          play; the tape is then replayed (on the dev host from the Mac)
#   make script SCRIPT=... | replay REF=... | serve PORT=...
#   make chunks SEED=N ... | structures SEED=N ... | structures-info DIR=...
#   make pristine-diff        every change made to the decompiled vanilla source

SHELL := /bin/bash
# No builtin implicit rules: with them every up-to-date run spent 0.4 s probing
# RCS/SCCS rules for each of the 1383 source prerequisites of the build.
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:
OS    := $(shell uname -s)
OUT   := $(abspath ../out/java)
LIB   := $(OUT)/lib
CLIENT_JAR := $(OUT)/client/1.7.10.jar
NATIVES    := $(OUT)/natives
CLASSES    := $(OUT)/classes
JAR_MD5    := e6b7a531b95d0c172acb704d1f54d1b3

ifeq ($(OS),Darwin)
  MC_HOME ?= $(HOME)/Library/Application Support/minecraft
  V1      ?= $(HOME)/dev/netherite
  JAVA8   ?= $(V1)/java/jdk8-macos-arm64/Contents/Home
  SHA1    := shasum
  MD5     := md5 -q
  XVFB    :=
else
  MC_HOME ?= $(HOME)/.minecraft
  JAVA8   ?= /usr/lib/jvm/java-8-openjdk-amd64
  SHA1    := sha1sum
  MD5     := md5sum
  XVFB     = bash $(CURDIR)/tests/headless.sh $(SLOTS)
endif

JAVAC   := $(JAVA8)/bin/javac
# the build's javac flags (the class store keys on them)
JAVACFLAGS := -nowarn -XDignore.symbol.file -encoding UTF-8 -source 1.8 -target 1.8
JAVA    := $(JAVA8)/bin/java
# The harness id, computed at its first use in a make run and kept (tests/harness.sh:
# cached against the sources' stat; it was recomputed at every use, 5 or 6 times a replay)
ifeq ($(OS),Darwin)
HARNESS  = $(eval HARNESS := $(shell find src harness -name '*.java' | LC_ALL=C sort | xargs cat | $(SHA1) | cut -c1-12))$(HARNESS)
else
HARNESS  = $(eval HARNESS := $(shell bash tests/harness.sh $(OUT)))$(HARNESS)
endif
# The serial collector for every oracle JVM and javac. The default parallel one
# starts a GC thread per core pair on a 128-core host and grows the heap toward
# -Xmx: night-s1's replay took 34.0 s at 1.58 GB peak RSS with it and 24.1 s at
# 0.98 GB with these flags, javac the same wall at a third of the CPU. All 200
# snapshot tapes replay the same under both (198 OK; play-reload-s1 and
# play-reload2-s1 diverge at t=2 under either), 2026-09-26.
JVMGC   ?= -XX:+UseSerialGC -XX:MinHeapFreeRatio=10 -XX:MaxHeapFreeRatio=30
# Oracle JVMs only: Minecraft.loadWorld's System.gc() ran two or three full
# collections per run (0.05 to 0.24 s each, lane/oracletick); the pool's
# members always ran without them.
JVMX    ?= -XX:+DisableExplicitGC
# JVMJIT=-XX:TieredStopAtLevel=1: C1 only, for short runs on a loaded host
# (csrc/play/session.sh's replay and next start): a 517-tick replay took
# 12.7 CPU seconds against 21.0 with C2, its next start 10.2 against 15.3,
# rows, checkpoint and snapshot byte-identical (lane/chainfuzz2, load 80)
JVMJIT  ?=
# Every oracle JVM, fresh or a pool member: an OutOfMemoryError ends the JVM
# (exit 3, "Terminating due to java.lang.OutOfMemoryError" on stderr) wherever
# it is thrown, so no catch (vanilla's client loop, a Snapshot worker's) can
# turn it into a run that answers rc 0 without its output (lane/poolfix).
JVMOOM  := -XX:+ExitOnOutOfMemoryError

# JaCoCo, for make coverage and make replay COVERAGE=FILE (below): its
# agent instruments net.minecraft.* (src/) and netherite.* (harness/: make
# selfcheck SELECT=1 maps a harness change to the recordings that run it)
# and keeps its data for Coverage.java, which writes it at the run's end
JACOCO_VER    := 0.8.12
JACOCO_SHA256 := 35cf2a2b8937659ecbc8d0d3902dfa7f55b7ed2250e82424036a3e1d12462cd7
JACOCO        := $(OUT)/jacoco-$(JACOCO_VER)
JACOCO_AGENT  := $(JACOCO)/lib/jacocoagent.jar
comma := ,
COVOPTS       := output=none$(comma)includes=net.minecraft.*:netherite.*$(comma)jmx=false
COVJVM         = $(if $(COVERAGE),"-javaagent:$(JACOCO_AGENT)=$(COVOPTS)")

# One game dir per run (concurrent runs must never share a save); the JVM starts
# inside it so log4j's logs/ land there. Paths handed to the game are absolute.
# A run's game directory is named after its tape: the tape's folder and file
# name, so parallel runs of different snapshots' tape.jsonl do not share a save.
runname = $(notdir $(patsubst %/,%,$(dir $(abspath $(1)))))-$(basename $(notdir $(1)))

oracle_java = mkdir -p $(OUT)/run/$(1) && cd $(OUT)/run/$(1) && \
              "$(JAVA)" -Xmx3G $(JVMGC) $(JVMX) $(JVMOOM) $(JVMJIT) $(2) -Djavax.accessibility.assistive_technologies= -Djava.library.path=$(NATIVES) \
              -cp "$(CLASSES):$(LIB)/*:$(CLIENT_JAR)" netherite.oracle.Main \
              --game-dir . --assets $(OUT)/assets --harness $(HARNESS)

# Oracle runs at once on this machine (all checkouts share the slots); more wait.
SLOTS ?= 24

# The world: ../config.yaml, or CONF=path (../configs/NAME.yaml); SEED=N and SET="key=value ..."
# (section.key, or a bare world key: SET="difficulty=peaceful render_distance=6") override it.
CONF  ?= ../config.yaml
WORLD  = --conf $(abspath $(CONF)) $(if $(SEED),--set seed=$(SEED)) $(foreach s,$(SET),--set $(s))
STAMP := $(shell date +%Y%m%dT%H%M%S)-$(shell echo $$$$)
TAPE  ?= $(OUT)/tapes/$(if $(SEED),seed$(SEED)-)$(STAMP).jsonl
# the dev host's checkout, from a Mac clone (../csrc/play/host.sh: git config
# netherite.host and netherite.dir, else origin's HOST:DIR)
REMOTE ?= $(word 1,$(shell bash ../csrc/play/host.sh))
RDIR   ?= $(word 2,$(shell bash ../csrc/play/host.sh))
T     = $(abspath $(TAPE))

.PHONY: build deps gate play check script replay jmath-probe anytick selfcheck pool-start pool-stop serve chunks structures structures-info probe probe-info run biomes-h blocks-h items-h stats-h pick-h harness pristine-diff clean

build: src $(CLASSES)/.stamp $(CLASSES)/poolagent.jar

# src/ from the pristine decompile and decomp/oracle.patch (decomp/src.sh: a
# no-op when current, 0.2 s to remake; it refuses to replace edits not yet in
# the patch). Commit decomp/oracle.patch, never src/ (make oracle-patch after
# an edit).
.PHONY: src
src:
	@bash decomp/src.sh

# A pool member's java agent (PoolAgent.java, -javaagent): a jar with only its
# manifest, the class itself comes from the classpath
$(CLASSES)/poolagent.jar: $(CLASSES)/.stamp
	@printf 'Manifest-Version: 1.0\nPremain-Class: netherite.oracle.PoolAgent\n' > $(OUT)/poolagent.mf && "$(JAVA8)/bin/jar" cfm $@ $(OUT)/poolagent.mf

# When only harness sources (harness/) changed since the last build, recompile all
# of harness/ against the existing classes: about 2 s instead of 5-10 s, and bot
# lanes rebuild after every GoldBot edit (53 builds in 30 min on 2026-09-26).
# Safe because vanilla code reads no compile-time constant of the harness
# (hooked.txt, written by the full build, lists the vanilla files that call
# in; none inlines a harness field), a changed hook signature fails loudly at
# its first call, and the merge trial always builds from scratch. Any change
# under src/, or a failed incremental compile, takes the full build.
# Before either, the class store (tests/classtore.sh, lane/trialcache): a full
# build's classes are kept under CLASSTORE by the exact sources, compiler and
# flags they came from, and a build of sources compiled before (a merge
# trial's fresh worktree, master after the trial built its merge) unpacks
# them in about a second instead of running javac (30 to 125 s at load 150).
# CLASSTORE= turns it off; not on macOS.
CLASSTORE ?= $(if $(filter Darwin,$(OS)),,$(HOME)/.cache/netherite-classes)
CLASSTORE_ARGS = $(CLASSTORE) "$(JAVA8)" $(LIB) $(CLASSES) $(JAVACFLAGS)
$(CLASSES)/.stamp: $(shell find src harness -name '*.java' 2>/dev/null)
	@test -f $(CLIENT_JAR) || { echo "run make deps first"; exit 1; }
	@if [ -n "$(CLASSTORE)" ] && bash tests/classtore.sh get $(CLASSTORE_ARGS); then \
	  grep -rl 'netherite\.oracle\.' src --include='*.java' > $(OUT)/hooked.txt; exit 0; fi; \
	if [ -f $@ ] && [ -f $(OUT)/hooked.txt ] && [ -z "$$(find src -name '*.java' -newer $@ -print -quit)" ]; then \
	  find harness -name '*.java' > $(OUT)/inc.txt; \
	  if "$(JAVAC)" $(addprefix -J,$(JVMGC)) $(JAVACFLAGS) -cp "$(CLASSES):$(LIB)/*" -d $(CLASSES) @$(OUT)/inc.txt > $(OUT)/javac-inc.log 2>&1; then \
	    echo "rebuilt $$(wc -l < $(OUT)/inc.txt | tr -d ' ') harness sources"; touch $@; exit 0; \
	  fi; \
	  echo "incremental compile failed; full build"; \
	fi; \
	$(MAKE) --no-print-directory -s full-build
	@touch $@

.PHONY: full-build
full-build:
	@rm -rf $(CLASSES) && mkdir -p $(CLASSES)
	@find src harness -name '*.java' > $(OUT)/sources.txt
	@"$(JAVAC)" $(addprefix -J,$(JVMGC)) $(JAVACFLAGS) -cp "$(LIB)/*" -d $(CLASSES) @$(OUT)/sources.txt > $(OUT)/javac.log 2>&1; \
	  rc=$$?; grep -v '^Note:' $(OUT)/javac.log; [ $$rc != 0 ] || [ -z "$(CLASSTORE)" ] || bash tests/classtore.sh put $(CLASSTORE_ARGS); true
	@test -f $(CLASSES)/netherite/oracle/Main.class && test -f $(CLASSES)/net/minecraft/client/Minecraft.class
	@echo "built $$(find $(CLASSES) -name '*.class' | wc -l | tr -d ' ') classes from $$(wc -l < $(OUT)/sources.txt | tr -d ' ') sources"
	@grep -rl 'netherite\.oracle\.' src --include='*.java' > $(OUT)/hooked.txt

# The 1.7.10 libraries exactly as its launcher manifest lists them, the owned
# client jar (only its resources are used), natives, and the assets directory.
deps:
	@V="$(MC_HOME)/versions/1.7.10"; test -f "$$V/1.7.10.jar" || { echo "no 1.7.10 in $(MC_HOME): install it once with any launcher"; exit 1; }
	@got=$$($(MD5) "$(MC_HOME)/versions/1.7.10/1.7.10.jar" | cut -d" " -f1); \
	  [ "$$got" = "$(JAR_MD5)" ] || { echo "client jar md5 $$got != $(JAR_MD5)"; exit 1; }
	@rm -rf $(LIB) $(NATIVES) && mkdir -p $(LIB) $(NATIVES) $(OUT)/client $(OUT)/tapes
	@cp "$(MC_HOME)/versions/1.7.10/1.7.10.jar" $(CLIENT_JAR)
	@ln -sfn "$(MC_HOME)/assets" $(OUT)/assets
	@jq -r '.libraries[] | select(.natives == null) | .name' "$(MC_HOME)/versions/1.7.10/1.7.10.json" | \
	  while IFS=: read g a v; do \
	    if [ "$(OS)" = Darwin ] && [ "$$g" = org.lwjgl.lwjgl ]; then continue; fi; \
	    cp "$(MC_HOME)/libraries/$${g//.//}/$$a/$$v/$$a-$$v.jar" $(LIB)/; done
ifeq ($(OS),Darwin)
	@# No arm64 build of LWJGL 2.9.1 exists: use the v1 Mac setup (2.9.4 jars + arm64 natives).
	@G="$(V1)/java/Minecraft/run/gradle/caches/modules-2/files-2.1/org.lwjgl.lwjgl"; \
	  cp "$$G"/lwjgl/2.9.4-nightly-20150209/*/lwjgl-2.9.4-nightly-20150209.jar "$$G"/lwjgl_util/2.9.4-nightly-20150209/*/lwjgl_util-2.9.4-nightly-20150209.jar $(LIB)/
	@N="$(V1)/java/Minecraft/run/gradle/caches/minecraft/net/minecraft/natives/1.11.2"; \
	  cp "$$N/liblwjgl.dylib" "$$N/openal.dylib" "$$N/libjinput-osx.jnilib" $(NATIVES)/
else
	@jq -r '.libraries[] | select(.natives.linux != null) | select([.rules[]? | select(.action == "disallow" and .os.name == "linux")] | length == 0) | .name + ":" + .natives.linux' "$(MC_HOME)/versions/1.7.10/1.7.10.json" | \
	  while IFS=: read g a v c; do \
	    unzip -oq "$(MC_HOME)/libraries/$${g//.//}/$$a/$$v/$$a-$$v-$$c.jar" -x 'META-INF/*' -d $(NATIVES) >/dev/null; done
endif
	@echo "deps: $$(ls $(LIB) | wc -l | tr -d ' ') jars, natives: $$(ls $(NATIVES) | tr '\n' ' ')"

# Keyframes (tests/keyframes.sh): snapshots every KEVERY ticks of each
# recording in out/java/keyframes/NAME, each segment checked natively
#   make keyframes [RECS="NAME ..."] [KEVERY=1000] [J=4] [FORCE=1]
.PHONY: keyframes
keyframes: build
	@bash tests/keyframes.sh $(RECS)

# The re-record check: re-record each recording from its script in fresh JVMs
# (tests/pool-proof.sh record) and compare every file of its snapshot
# directory with the recorded one; rc 1 on any difference. A recording
# the result cache already holds for this harness comes from the cache
# (CACHE=0 re-runs every one).
#   make rerecord RECS="fight-zombie-s1 gold-g28-s42 ..." [ROOT=dir, default ../out/java/rerecord]
.PHONY: rerecord
rerecord: build
	@r=$(abspath $(or $(ROOT),$(OUT)/rerecord)); rm -rf $$r; \
	bash tests/pool-proof.sh record fresh $$r $(RECS); bash tests/pool-proof.sh compare $$r $(RECS)

# The oracle result cache (tests/oraclecache.sh): a script or replay run
# whose inputs were run before (the same harness, JVM, script or tape bytes,
# checkpoint, world and output-shaping arguments; not the output paths, not
# POOL) restores its stored outputs and prints its log in about a second,
# without a JVM. A miss runs as before and is stored if its rc is 0; a
# concurrent identical miss waits for the first. Entries live under OCROOT
# (shared by every checkout on the machine), least recently used out past
# OCCAP MB; OCROOT/stats.tsv logs every lookup. CACHE=0 runs fresh (the
# gate, the pool's proofs and any byte-identity check need it).
# A coverage run (make replay COVERAGE=FILE) keeps its entries, execution
# data included, under COVROOT (COVCAP MB), keyed with the JaCoCo version
# and agent options besides: the shared cache's churn would evict them
# between two make coverage runs. CACHEONLY=1: a hit restores, a miss
# exits 125 without running (make coverage's first pass).
CACHE  ?= $(if $(filter Darwin,$(OS)),0,1)
OCROOT ?= $(HOME)/.cache/netherite-oracle
OCCAP  ?= 20000
COVROOT ?= $(HOME)/.cache/netherite-coverage
COVCAP  ?= 5000
# a CLASSES other than this tree's own build (pool-proof's MAKEX frozen copy) is not what HARNESS names
OCOFF   = $(or $(filter 0,$(CACHE)),$(filter-out $(CLASSES),$(OUT)/classes))
# $(1) script or replay, $(2) the run's name, $(3) its oracle arguments, $(4) what the fresh run's make needs
cached = bash $(CURDIR)/tests/oraclecache.sh $(if $(COVERAGE),$(COVROOT) $(COVCAP),$(OCROOT) $(OCCAP)) $(HARNESS) "$(JAVA)" $(OUT) $(1) $(OUT)/run/$(2) \
	  $(if $(COVERAGE),--key "jacoco $(JACOCO_VER) $(JACOCO_SHA256) $(COVOPTS)") $(if $(filter 1,$(CACHEONLY)),--lookup) \
	  $(MAKE) --no-print-directory $(1) CACHE=0 $(4) -- $(3)

# What the cache holds and has saved: make cache-stats [SINCE=epoch seconds]
.PHONY: cache-stats
cache-stats:
	@n=$$(ls -d $(OCROOT)/entries/*/*/done 2>/dev/null | wc -l); \
	  mb=$$(cat $(OCROOT)/entries/*/*/size 2>/dev/null | awk '{s += $$1} END {printf "%.0f", s / 1048576}'); \
	  echo "oracle cache $(OCROOT): $$n entries, $$mb MB of $(OCCAP)"; \
	  awk -F'\t' -v since=$(or $(SINCE),0) '$$1 >= since { n[$$3]++; if ($$3 == "hit") { saved += $$6; spent += $$5 } } \
	    END { for (r in n) printf "  %-8s %d\n", r, n[r]; printf "  hits saved %.2f h of oracle time in %.1f s\n", saved / 3600, spent }' \
	    $(OCROOT)/stats.tsv 2>/dev/null

# Agent mode from a JSONL command script: make script SCRIPT=tests/smoke.jsonl [FRAMES=dir EVERY=n ARGS=--detail]
# RAWGUI=events.jsonl: raw mouse and key events fed to the open container screen (RawGui.java, the GUI judge)
# FROM=checkpoint-dir starts on a checkpoint's save (a script's {"cmd":"save"} writes one)
SCRIPT_ARGS = --agent $(WORLD) $(if $(FROM),--from $(abspath $(FROM))) \
	  --script $(abspath $(SCRIPT)) --tape $(T) $(if $(FRAMES),--frames $(abspath $(FRAMES))) \
	  $(if $(EVERY),--frame-every $(EVERY)) $(if $(TRACE),--trace $(abspath $(TRACE))) $(if $(filter 1,$(DEV)),--dev) \
	  $(if $(RAWGUI),--raw-gui $(abspath $(RAWGUI))) $(ARGS)
script: build
	@$(if $(OCOFF),$(call pooled,$(SCRIPT_ARGS),$(call runname,$(TAPE))) \
	$(XVFB) bash -c '$(call oracle_java,$(call runname,$(TAPE))) $(SCRIPT_ARGS)',$(call cached,script,$(call runname,$(TAPE)),$(SCRIPT_ARGS),TAPE=$(T)))

# Replay a tape and stop at the first differing field (rc != 0): make replay REF=tape.jsonl
# SNAPAT=T,T,... SNAPDIR=dir also writes a Snapshot at the head of each tick T into dir/tT
# (SNAPPLAIN=1: each in the single-snapshot form, without the shared dir/blobs)
# SAVEEND=dir: after the last row, a Save and Quit into the checkpoint dir (csrc/play/session.sh)
# (SAVEPRE=0: without the checkpoint's pre/ Snapshot, the roundtrip gate's reference)
REPLAY_ARGS = --agent \
	  --replay $(abspath $(REF)) --tape $(OUT)/run/$(call runname,$(REF)).replay/replay.jsonl \
	  $(if $(SAVEEND),--save-at-end $(abspath $(SAVEEND))) $(if $(filter 0,$(SAVEPRE)),--save-no-pre) \
	  $(if $(FRAMES),--frames $(abspath $(FRAMES))) $(if $(EVERY),--frame-every $(EVERY)) \
	  $(if $(TRACE),--trace $(abspath $(TRACE))) $(if $(SNAPAT),--snap-at $(SNAPAT) --snap-dir $(abspath $(SNAPDIR)) $(if $(filter 1,$(SNAPPLAIN)),--snap-plain)) \
	  $(if $(COVERAGE),--coverage $(abspath $(COVERAGE))) $(ARGS)
replay: build $(if $(COVERAGE),$(JACOCO_AGENT))
	@$(if $(OCOFF),$(call pooled,$(REPLAY_ARGS),$(call runname,$(REF)).replay) \
	$(XVFB) bash -c '$(call oracle_java,$(call runname,$(REF)).replay,$(COVJVM)) $(REPLAY_ARGS)',$(call cached,replay,$(call runname,$(REF)).replay,$(REPLAY_ARGS)))

# Java coverage (tests/coverage.sh): every stored recording under
# ../out/java/snapshots (or RECS, names there or recording directories)
# replayed with JaCoCo's agent, J at a time (default: from the load), each
# from the coverage cache, a coverage pool member or a fresh JVM; writes
# ../out/java/coverage: exec/NAME.exec and recs/NAME.methods.tsv per
# recording, merged.exec, methods.tsv (every method of src/ with the
# recordings that run it), unexecuted.tsv (the in-scope methods no recording
# runs, by the index's records) and summary.txt. The recs/ data names the
# methods of harness/ too, and fingerprints.tsv, inputs.tsv and state tell make
# selfcheck SELECT=1 what tree it was made on. A RECS run replaces those
# recordings' data and re-merges every exec/ file; a run with no RECS
# starts from an empty exec/.
#   make coverage [RECS="a b"] [J=N]
# COVERAGE=FILE on make replay is one such run: the JaCoCo agent on the JVM
# and Main's --coverage FILE (Coverage.java). Its result is cached like any
# replay (under COVROOT, the execution data with it), and a miss goes to a
# coverage member of the shared pool (tests/poolauto.sh: members that carry
# the agent and cut the data per job, Coverage.java and CovInit.java),
# else to a fresh JVM with the agent.
$(JACOCO_AGENT):
	@mkdir -p $(OUT) && rm -rf $(JACOCO) $(JACOCO).zip && \
	  curl -sSfL -o $(JACOCO).zip https://repo1.maven.org/maven2/org/jacoco/jacoco/$(JACOCO_VER)/jacoco-$(JACOCO_VER).zip && \
	  echo "$(JACOCO_SHA256)  $(JACOCO).zip" | sha256sum -c --quiet && \
	  mkdir -p $(JACOCO) && unzip -q $(JACOCO).zip 'lib/*' -d $(JACOCO) && rm -f $(JACOCO).zip && touch $@
$(OUT)/covtool/.stamp: tests/CovReport.java tests/SelfSelect.java $(JACOCO_AGENT)
	@rm -rf $(OUT)/covtool && mkdir -p $(OUT)/covtool && \
	  "$(JAVAC)" -nowarn -encoding UTF-8 -source 1.8 -target 1.8 -cp "$(JACOCO)/lib/jacococli.jar:$(LIB)/gson-2.2.4.jar" -d $(OUT)/covtool tests/CovReport.java tests/SelfSelect.java && touch $@
.PHONY: coverage
coverage: build $(JACOCO_AGENT) $(OUT)/covtool/.stamp
	@bash tests/coverage.sh $(if $(J),-j $(J)) -p $(POOLMAX) -- "$(JAVA)" "$(OUT)/covtool:$(JACOCO)/lib/jacococli.jar:$(LIB)/gson-2.2.4.jar" $(RECS)

# The oracle's self-check (tests/selfcheck.sh): every stored recording under
# ../out/java/snapshots replayed fresh (CACHE=0) on this tree's oracle, one
# line each, rc 1 on any divergence: make selfcheck [RECS="a b"] [J=8] [TSV=file]
# SELECT=1 [COV=dir]: only the recordings that can reach code changed since
# the coverage data in COV (default ../out/java/coverage, make coverage)
# was made, or every one with a FALLBACK line saying why (tests/SelfSelect.java)
COV ?= $(OUT)/coverage
selfcheck: build $(if $(filter 1,$(SELECT)),$(OUT)/covtool/.stamp)
	@bash tests/selfcheck.sh $(if $(J),-j $(J)) $(if $(TSV),-o $(abspath $(TSV))) $(if $(filter 1,$(SELECT)),--select $(abspath $(COV)) --java "$(JAVA)") $(RECS)

# The any-tick sweep (tests/anytick.sh): snapshots at many ticks of each
# recording, each resumed natively: make anytick [RECS="fight-zombie-s1 ..."]
# (no RECS: every recording in tests/anytick.list)
# [ROWS=200 EARLY=30 SPREAD=25 J=4 KEEP=1 DETAIL=1 TICKS=t,t]
anytick: build
	$(foreach v,ROWS EARLY SPREAD J KEEP DETAIL TICKS,$(if $($(v)),$(v)=$($(v)))) bash tests/anytick.sh $(RECS)

# The warm pool (netherite.oracle.Pool): long-lived oracle JVMs that take
# script and replay runs one after another, each byte-identical to a fresh
# JVM's, without the JVM start, the client start or the JIT warm-up.
#   make pool-start [POOLN=1]   start POOLN members for this tree (each on its own display)
#   make pool-start POOLTAG=mip4- POOLARGS="--pool-options TAPE"   a member with the options of
#                               TAPE's header: the ones a client reads at start (mipmaps, filtering)
#                               must be the member's own; a run goes to the first member that has them
#   make pool-stop              stop them
#   POOLARGS="--pool-max-mb N"  a member ends after a job that leaves it above N MB resident
#                               (default 2500; its "ORACLE POOL job" lines carry its rss)
# While members are up, make script and make replay hand their run to a free
# member whose harness equals this tree's; with none free (or POOL=0, or a run
# the pool refuses: play, serve, --trace, other options) they start a fresh
# JVM as before.
#
# The shared pool (tests/poolauto.sh): with no free member of its own, a run
# goes to a free member of the machine-wide pool under POOLHOME whose harness
# (and JVM, flags, libraries, assets) equals this tree's, whichever tree
# started it; with none free it runs fresh and a member starts in the
# background for the next run, at most POOLMAX on the machine and together
# at most the oracle half of the memory budget. A member retires after
# POOLIDLE seconds without a run or past POOLMAXMB resident.
#   make pool-warm              a shared member of this harness up (started if none is), for proofs
#   make pool-status            every shared member: key, pid, free or busy, jobs, idle, rss
#   make pool-shared-stop       stop them (after their runs in progress; by their pid files)
# POOLAUTO=0 keeps runs off the shared pool; a CLASSES other than the tree's
# own (pool-proof's MAKEX) never uses it.
POOL    ?= 1
POOLN   ?= 1
POOLDIR := $(OUT)/pool
POOLAUTO  ?= $(if $(filter Darwin,$(OS)),0,1)
POOLHOME  ?= $(HOME)/dev/nw/oraclepool
POOLIDLE  ?= 600
POOLMAXMB ?= 2500
# POOLMAX from the memory: half the memslot budget over POOLMAXMB (the dev host's 64000 MB: 12, and
# poolauto's half-budget rule stops members of 2.7 GB at 11), or on a machine without a budget
# (the Mac) a quarter of its RAM over POOLMAXMB (16 GB: 1, 32 GB: 3), at least 1. It was 8: the
# shared members sat at 8 (10,586 starts refused for it by 2026-09-28) while a round of 16
# session checks ran most of its oracle runs fresh (lane/poolsave)
ifeq ($(origin POOLMAX),undefined)
POOLMAX := $(shell b=$$(cat /tmp/netherite-mem/budget 2>/dev/null); \
  if [ "$${b:-0}" -gt 0 ] 2>/dev/null; then m=$$((b / 2)); \
  else m=$$(( $$( { sysctl -n hw.memsize 2>/dev/null || awk '/^MemTotal:/ {print $$2 * 1024}' /proc/meminfo; } ) / 4194304 )); fi; \
  n=$$((m / $(POOLMAXMB))); [ $$n -ge 1 ] || n=1; echo $$n)
endif
AUTOPOOL   = $(and $(filter 1,$(POOLAUTO)),$(filter $(OUT)/classes,$(CLASSES)))
# the run's arguments go to a pool member first (the tree's own, then a shared
# one); exit 125 means none took it, 124 that a member failed it (a fault:
# tests/pool.sh), and either way the run goes on to a fresh JVM
pooled = $(if $(filter 0,$(POOL)),,rc=125; $(if $(COVERAGE),false,[ -d $(POOLDIR) ]) && { bash $(CURDIR)/tests/pool.sh $(if $(AUTOPOOL),--state /dev/null) $(POOLDIR) $(HARNESS) $(OUT)/run/$(2) \
	  --game-dir . --assets $(OUT)/assets --harness $(HARNESS) $(1); rc=$$?; }; \
	$(if $(AUTOPOOL),[ $$rc = 125 ] && { bash $(CURDIR)/tests/poolauto.sh run $(call POOLAUTOARGS,$(2)) -- \
	  --game-dir . --assets $(OUT)/assets --harness $(HARNESS) $(1); rc=$$?; };) [ $$rc = 125 ] || [ $$rc = 124 ] || exit $$rc;)
# the shared pool's arguments for this tree: HOME HARNESS CWD JAVA JVMFLAGS CLASSES LIB NATIVES CLIENTJAR ASSETS SLOTS MAX IDLE MAXMB COV
# (a coverage run's members: its own key, JaCoCo's agent and options; the tree's own pool never takes one)
POOLAUTOARGS = $(POOLHOME) $(HARNESS) $(OUT)/run/$(1) "$(JAVA)" "$(JVMGC) $(JVMOOM) $(POOLJVM)" \
	  $(CLASSES) $(LIB) $(NATIVES) $(CLIENT_JAR) $(OUT)/assets $(SLOTS) $(POOLMAX) $(POOLIDLE) $(POOLMAXMB) \
	  $(if $(COVERAGE),$(JACOCO_AGENT)=$(COVOPTS),-)

.PHONY: pool-status pool-shared-stop pool-warm
# a shared member of this tree's harness up (started if none is), for a proof that needs one
pool-warm: build
	@bash tests/poolauto.sh warm $(call POOLAUTOARGS,-)
pool-status:
	@bash tests/poolauto.sh status $(POOLHOME)
pool-shared-stop:
	@bash tests/poolauto.sh stop $(POOLHOME)

pool-start: build
	@for i in $$(seq 1 $(POOLN)); do \
	  d=$(POOLDIR)/$(POOLTAG)$$i; \
	  if [ -f $$d/pid ] && kill -0 $$(cat $$d/pid) 2>/dev/null; then echo "pool $$i: running (pid $$(cat $$d/pid))"; continue; fi; \
	  rm -rf $$d; mkdir -p $$d; \
	  setsid -f $(XVFB) bash -c '$(call oracle_java,pool-$(POOLTAG)'$$i',$(POOLJVM) -javaagent:$(CLASSES)/poolagent.jar) --agent --pool '$$d' $(POOLARGS)' \
	    > $$d/log 2>&1 < /dev/null; \
	done; \
	for i in $$(seq 1 $(POOLN)); do \
	  d=$(POOLDIR)/$(POOLTAG)$$i; \
	  for t in $$(seq 1 600); do [ -f $$d/port ] && break; grep -q "ORACLE POOL END\\|Exception in thread \"main\"" $$d/log 2>/dev/null && break; sleep 0.1; done; \
	  if [ -f $$d/port ]; then echo "pool $(POOLTAG)$$i: ready, pid $$(cat $$d/pid), port $$(cat $$d/port)"; \
	  else echo "pool $(POOLTAG)$$i: did not start, see $$d/log"; exit 1; fi; \
	done

pool-stop:
	@for d in $(POOLDIR)/*/; do \
	  [ -f $$d/pid ] || continue; p=$$(cat $$d/pid); \
	  if kill $$p 2>/dev/null; then while kill -0 $$p 2>/dev/null; do sleep 0.1; done; echo "pool $$(basename $$d): stopped (pid $$p)"; fi; \
	  rm -f $$d/pid $$d/port; \
	done; true
# The pool JVM's own flags: vanilla's System.gc() on every world swap would stop it for a full collection
POOLJVM  ?= -XX:+DisableExplicitGC
POOLARGS ?=
POOLTAG  ?=
# TCP control port (the Malmo replacement): make serve PORT=25590
serve: build
	$(XVFB) bash -c '$(call oracle_java,$(call runname,$(TAPE))) --agent $(WORLD) $(if $(FROM),--from $(abspath $(FROM))) \
	  --port $(PORT) --tape $(T) $(if $(TRACE),--trace $(abspath $(TRACE))) $(ARGS)'

# Play with a tape; needs a display. Close the window to finish (Esc only
# releases the mouse: there is no pause menu). The tape is then checked unless CHECK=0.
# Every keyboard and mouse event the client took is recorded beside the tape
# (NAME.raw.jsonl, RawRec.java): csrc/play/rawjudge.sh TAPE plays them through
# the native client and judges it against this session. DRIVE=file.jsonl: a
# scripted stand-in for the person (RawDrive.java, the headless proof).
play: build
	@echo "playing; close the window to finish. log: $(T:.jsonl=.log)"
	-@$(call oracle_java,$(call runname,$(TAPE))) --play $(WORLD) --tape $(T) --raw-rec $(T:.jsonl=.raw.jsonl) $(if $(DRIVE),--raw-drive $(abspath $(DRIVE))) $(ARGS) > $(T:.jsonl=.log) 2>&1
	@grep -a "ORACLE" $(T:.jsonl=.log) || { echo "client failed, see $(T:.jsonl=.log)"; exit 1; }
	$(if $(filter 0,$(CHECK)),,@$(MAKE) -s check TAPE=$(T))

# Replay a finished tape where the gates live: locally on Linux; on the dev host
# (REMOTE) from the Mac, and only when its source hash equals this tree's (push first).
check:
ifeq ($(OS),Darwin)
	@here=$(HARNESS); there=$$(ssh $(REMOTE) "make -s -C $(RDIR)/oracle harness" 2>/dev/null); \
	  if [ "$$here" != "$$there" ]; then echo "not replayed: $(REMOTE) source $${there:-unreachable}, this tree $$here (push, then make check TAPE=$(T))"; exit 0; fi; \
	  ssh $(REMOTE) "mkdir -p $(RDIR)/out/java/tapes" && rsync -a $(T) $(REMOTE):$(RDIR)/out/java/tapes/ && \
	  ssh $(REMOTE) "make -s -C $(RDIR)/oracle replay REF=../out/java/tapes/$(notdir $(T)) 2>&1 | grep -a 'ORACLE REPLAY\|ORACLE DIVERGE'"
else
	@$(MAKE) -s replay REF=$(T) 2>&1 | grep -a "ORACLE REPLAY\|ORACLE DIVERGE"
endif

# Stage-by-stage raw chunk dump, the reference for native worldgen:
#   make chunks SEED=1 [X0=-4 Z0=-4 X1=3 Z1=3 STEP=1 DIR=../out/java/chunks/seed1]
# TRACE=path (any target above) records the Trace.t call sites to path.
X0 ?= -4
Z0 ?= -4
X1 ?= 3
Z1 ?= 3
STEP ?= 1
DIR ?= $(OUT)/chunks/seed$(SEED)
chunks: build
	@test -n "$(SEED)" || { echo "make chunks needs SEED=N"; exit 2; }
	@mkdir -p $(OUT)/tmp && echo '{"cmd":"chunks","x0":$(X0),"z0":$(Z0),"x1":$(X1),"z1":$(Z1),"step":$(STEP),"out":"$(abspath $(DIR))"}' > $(OUT)/tmp/chunks-$(notdir $(DIR)).jsonl
	@$(XVFB) bash -c '$(call oracle_java,chunks-$(notdir $(DIR))) --agent $(WORLD) --script $(OUT)/tmp/chunks-$(notdir $(DIR)).jsonl $(if $(TRACE),--trace $(abspath $(TRACE)))' 2>&1 | grep -a "ORACLE CHUNKS\|Exception"

# SetBlock fuzz probe, the reference for the native world core (height maps,
# sky and block light), and the population feature probe (KIND=feature):
#   make probe SEED=2 DIR=../out/java/probes/sb-plain [CX=2000 CZ=2000 RADIUS=2 OPS=2000 OPSEED=1 BLOCKS=plain RING=1 TRACE=path]
# BLOCKS is plain or light; RING is how many chunks beyond the ops' own radius
# are loaded (1: 49 chunks at RADIUS=2, the ops' 3x3 hash neighborhood).
# KIND=move is the movement probe, the reference for the native port of
# Entity.moveEntity:
#   make probe SEED=2 KIND=move DIR=../out/java/probes/mv-2 [CX CZ RADIUS RING OPS=5000 SHAPES=400 OPSEED=1]
# KIND defaults to setblock, so every existing invocation records the same bytes.
# For KIND=move, OPS defaults to 5000 and SHAPES to 400 (the movement probe's
# own defaults); setblock keeps its historical OPS=2000.
#   make probe SEED=2 KIND=feature FEATURE=minable CASES=600 DIR=../out/java/probes/fe-minable-2 [OPSEED=1 CX CZ RADIUS RING DIM=0]
# DIM picks the world a feature probe runs in: -1 the Nether, 0 the overworld, 1 the End.
# BLOCKS is plain or light for setblock; RING is how many chunks beyond the
# ops' own radius are loaded (1: 49 chunks at RADIUS=2, the ops' 3x3 hash
# neighborhood). KIND=feature runs CASES feature.generate calls on the same raw
# region, with a case position from the ops square and the feature's own y band.
CX ?= 2000
CZ ?= 2000
RADIUS ?= 2
BLOCKS ?= plain
RING ?= 1
KIND ?= setblock
ifeq ($(KIND),move)
OPS ?= 5000
SHAPES ?= 400
else
OPS ?= 2000
SHAPES ?= 400
endif
OPSEED ?= 1
FEATURE ?= minable
CASES ?= 600
DIM ?= 0
PNAME = probe-$(notdir $(DIR))
probe: build
	@test -n "$(SEED)" && test -n "$(findstring /probes/,$(abspath $(DIR)))$(findstring /moves/,$(abspath $(DIR)))" || { echo "make probe needs SEED=N and DIR=../out/java/probes/NAME (moves/NAME for KIND=move)"; exit 2; }
	@mkdir -p $(abspath $(DIR)) $(OUT)/tmp
	@echo '{"cmd":"probe","kind":"$(KIND)","feature":"$(FEATURE)","cx":$(CX),"cz":$(CZ),"radius":$(RADIUS),"ring":$(RING),"ops":$(OPS),"cases":$(CASES),"opseed":$(OPSEED),"blocks":"$(BLOCKS)","shapes":$(SHAPES),"dim":$(DIM),"out":"$(abspath $(DIR))"}' > $(OUT)/tmp/$(PNAME).jsonl
	@$(XVFB) bash -c '$(call oracle_java,$(PNAME)) --agent $(WORLD) --script $(OUT)/tmp/$(PNAME).jsonl $(if $(TRACE),--trace $(abspath $(TRACE)))' 2>&1 | grep -a "ORACLE PROBE\|Exception"

# What a probe wrote, for humans: the manifest, the op count, the first 5 ops.
probe-info:
	@test -f $(DIR)/manifest.json || { echo "no $(DIR)/manifest.json: run make probe DIR=$(DIR) first"; exit 2; }
	@jq . $(DIR)/manifest.json
	@printf 'ops.bin %s bytes = %s ops\nfinal.bin.gz %s bytes\n' \
	  "$$(stat -c%s $(DIR)/ops.bin)" "$$(( $$(stat -c%s $(DIR)/ops.bin) / 24 ))" "$$(stat -c%s $(DIR)/final.bin.gz)"
	@echo 'first 5 ops, x y z (flags<<24)|meta<<16|id, hash_lo hash_hi:'
	@od -An -v -tu4 -N120 $(DIR)/ops.bin | xargs -n 6 echo

# Every structure start the real game computes for a region, the reference for
# the native ports of mineshafts, strongholds and temples:
#   make structures SEED=2 X0=-40 Z0=-40 X1=40 Z1=40 DIR=../out/java/structures/s2
# CHECK=1 also rebuilds every start from its canonical NBT and compares.
# X0/Z0/X1/Z1/DIR are shared with chunks, whose defaults are -4..3 and
# out/java/chunks; a bound this target is not given on the command line falls
# back to the wide region below.
SPX0 = $(if $(filter command line,$(origin X0)),$(X0),-40)
SPZ0 = $(if $(filter command line,$(origin Z0)),$(Z0),-40)
SPX1 = $(if $(filter command line,$(origin X1)),$(X1),40)
SPZ1 = $(if $(filter command line,$(origin Z1)),$(Z1),40)
SPDIR = $(if $(filter command line,$(origin DIR)),$(DIR),$(OUT)/structures/seed$(SEED))
structures: build
	@test -n "$(SEED)" || { echo "make structures needs SEED=N"; exit 2; }
	@mkdir -p $(OUT)/tmp && echo '{"cmd":"structures","x0":$(SPX0),"z0":$(SPZ0),"x1":$(SPX1),"z1":$(SPZ1),"check":$(if $(CHECK),true,false),"out":"$(abspath $(SPDIR))"}' > $(OUT)/tmp/structures-$(notdir $(SPDIR)).jsonl
	@$(XVFB) bash -c '$(call oracle_java,structures-$(notdir $(SPDIR))) --agent $(WORLD) --script $(OUT)/tmp/structures-$(notdir $(SPDIR)).jsonl $(if $(TRACE),--trace $(abspath $(TRACE)))' 2>&1 | grep -a "ORACLE STRUCTURES\|Exception"

# The manifest and the first start of each type: make structures-info DIR=...
structures-info:
	@test -n "$(DIR)" || { echo "make structures-info needs DIR=path"; exit 2; }
	@jq . $(DIR)/manifest.json
	@for t in Mineshaft Stronghold Temple; do jq -c "select(.type==\"$$t\")" $(DIR)/starts.jsonl | head -n 1; done

# Any probe class without its own target: netherite.oracle.CLASS.run(server, cmd)
# gets the JSON object CMD (its "cmd" and "class" are filled in):
#   make run SEED=2 CLASS=Crafting NAME=cr-2 CMD='{"cases":500,"out":"/abs/out/java/crafting/cr-2"}'
run: build
	@test -n "$(SEED)" && test -n "$(CLASS)" && test -n "$(NAME)" || { echo "make run needs SEED=N CLASS=Name NAME=runname [CMD=json]"; exit 2; }
	@mkdir -p $(OUT)/tmp && echo '$(CMD)' | jq -c '. + {"cmd":"run","class":"$(CLASS)"}' > $(OUT)/tmp/run-$(NAME).jsonl
	@$(XVFB) bash -c '$(call oracle_java,run-$(NAME)) --agent $(WORLD) --script $(OUT)/tmp/run-$(NAME).jsonl $(if $(TRACE),--trace $(abspath $(TRACE)))' 2>&1 | grep -a "ORACLE RUN\|Exception\|	at netherite"
CMD ?= {}

# The biome registry as a C header and the one .c that defines it (no game window needed).
biomes-h: build
	@"$(JAVA)" -cp "$(CLASSES):$(LIB)/*:$(CLIENT_JAR)" netherite.oracle.BiomeTable $(abspath ../csrc/engine/biomes.h) $(abspath ../csrc/engine/biomes_table.c)

# The block registry's static properties: a C header and the one .c that defines the tables.
blocks-h: build
	@"$(JAVA)" -cp "$(CLASSES):$(LIB)/*:$(CLIENT_JAR)" netherite.oracle.BlockTable $(abspath ../csrc/engine/blocks.h) $(abspath ../csrc/engine/blocks_table.c)

# The item registry's static properties: a C header and the one .c that defines the tables.
items-h: build
	@"$(JAVA)" -cp "$(CLASSES):$(LIB)/*:$(CLIENT_JAR)" netherite.oracle.ItemTable $(abspath ../csrc/engine/items.h) $(abspath ../csrc/engine/items_table.c)

# The StatList registry (per-id stat arrays, general stats, achievements) as a C header.
stats-h: build
	@"$(JAVA)" -cp "$(CLASSES):$(LIB)/*:$(CLIENT_JAR)" netherite.oracle.StatTable $(abspath ../csrc/engine/stats_table.h)

# MathHelper's floors and the yaw facings at huge, NaN and infinite inputs
# (JmathProbe), the reference for csrc/tests/test_jmath.c:
#   make jmath-probe DIR=../out/java/jmath/NAME
jmath-probe: build
	@test -n "$(DIR)" || { echo "make jmath-probe needs DIR=../out/java/jmath/NAME"; exit 2; }
	@"$(JAVA)" -cp "$(CLASSES):$(LIB)/*:$(CLIENT_JAR)" netherite.oracle.JmathProbe $(abspath $(DIR))

# Block.damageDropped per id and metadata (pick block's default damage) as a C header.
pick-h: build
	@"$(JAVA)" -cp "$(CLASSES):$(LIB)/*:$(CLIENT_JAR)" netherite.oracle.PickTable $(abspath ../csrc/engine/pick_table.h)

gate: build
	bash tests/gate.sh

# Play's two recorded scenes (its world textures, block atlas and HUD sheets,
# all drawn from the user's jar): tests/scenes/NAME.jsonl recorded into
# SCENES/NAME (default ../out/java/render) unless it is there already
# (FORCE=1 records again). hud_mixed is the old fancy profile (fancy, smooth
# lighting, 4 mipmaps, render distance 8), forest-fast config.yaml's.
SCENES ?= $(OUT)/render
SCENE_ARGS_hud_mixed := --option fancy=1 --option ao=2 --option mip=4 --rd 8
SCENE_ARGS_forest-fast :=
.PHONY: render-scenes
render-scenes: build
	@for s in hud_mixed forest-fast; do d=$(abspath $(SCENES))/$$s; \
	  [ -z "$(FORCE)" ] && [ -f $$d/manifest.json ] && [ -f $$d/state/gui_items.rgba ] && continue; \
	  mkdir -p $$d && sed "s#@DIR@#$$d#g" tests/scenes/$$s.jsonl > $$d/script.jsonl && \
	  args="$(SCENE_ARGS_hud_mixed)"; [ $$s = hud_mixed ] || args="$(SCENE_ARGS_forest-fast)"; \
	  $(MAKE) --no-print-directory -s script SEED=1 CACHE=0 POOL=0 SCRIPT=$$d/script.jsonl TAPE=$(OUT)/tapes/scene-$$s.jsonl \
	    ARGS="$$args --renderstate $$d/state" > $$d/oracle.log 2>&1 && [ -f $$d/manifest.json ] \
	    || { tail -5 $$d/oracle.log; echo "render-scenes: $$s failed ($$d/oracle.log)"; exit 1; }; \
	  echo "render-scenes: $$d"; done

harness:
	@echo $(HARNESS)

# Every change made to the decompiled vanilla source, against the pristine
# decompile laid out flat like src/ (FULL=1 for the whole patch instead of the summary).
pristine-diff: src
	@rm -rf $(OUT)/pd && mkdir -p $(OUT)/pd && \
	  cp -R "$$(bash decomp/src.sh pristine)" $(OUT)/pd/pristine && cp -R src $(OUT)/pd/now
	@cd $(OUT)/pd && git diff --no-index $(if $(FULL),,--stat=120) pristine now | cat; true

clean:
	rm -rf $(CLASSES) $(OUT)/run $(OUT)/frames

# oracle/src rebuilt from public inputs plus our patch (oracle/decomp/oracle-src.sh:
# Mojang's 1.7.10 client jar and libraries, MCP 9.08, Docker), so the
# decompiled tree never has to be distributed. oracle-patch regenerates
# oracle/decomp/oracle.patch from src/ after an edit to it (commit the patch;
# src/ is not tracked); oracle-src-check proves the route (QUICK=1: only that the patch is
# current, a second; COLD=1: empty download cache, image rebuilt).
.PHONY: oracle-src oracle-patch oracle-src-check
oracle-src:
	@bash decomp/oracle-src.sh $(or $(OSRC),$(OUT)/oracle-src)
oracle-patch:
	@bash decomp/mkpatch.sh
oracle-src-check:
	@bash decomp/oracle-src-check.sh
