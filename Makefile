# netherite. The Java oracle is self-contained in oracle/ (see oracle/Makefile), the C engine
# and everything built on it in csrc/ (csrc/Makefile).
.PHONY: all oracle gate lane lane-share lane-rm
# make: the nether CLI (below); nether setup builds the rest
all: cli
oracle:
	$(MAKE) -C oracle
gate:
	$(MAKE) -C oracle gate

# Parallel work lanes: a git worktree per lane on branch lane/NAME that shares
# this checkout's game files, chunk dumps, probes and structure dumps (read
# only: new ones a lane makes stay in the lane).
#   make lane NAME=light       ../nv2-lanes/light, built and ready
#   make lane-rm NAME=light    drop the worktree and the branch (after merging)
LANES ?= $(abspath ../nv2-lanes)
L      = $(LANES)/$(NAME)
# reference data folders under out/java that every lane sees (read only)
# out/java's builds and scratch; every other area holds recordings a lane
# links from master (make lane-share), so no lane edits a list of areas
NOSHARE = classes run tmp lib natives client assets coverage selfcheck
lane:
	@test -n "$(NAME)" || { echo "make lane NAME=..."; exit 2; }
	@git worktree add -q -b lane/$(NAME) $(L) HEAD
	@mkdir -p $(L)/out/java
	@for d in lib natives client assets; do ln -s $(CURDIR)/out/java/$$d $(L)/out/java/$$d; done
	@$(MAKE) -s lane-share NAME=$(NAME)
	@# this checkout's sources are HEAD's when oracle/ and csrc/ are clean: copy
	@# its builds with their times and give the lane's fresh sources this
	@# checkout's times, so the lane's make rebuilds exactly what this
	@# checkout's would (7 s saved). Not every build here is of HEAD: the
	@# merges rebuild all and play, not play-dev or the device programs, and
	@# marking every copy newer than the sources kept a play-dev from before
	@# lane/playarena in every lane (lane/playfix, 2026-09-30). Every copied
	@# dependency file names this checkout's objects, so all of them are
	@# rewritten: pic/, cg/, stk/ and cuda/ hold their own, and a pic/ left
	@# naming this checkout's paths never rebuilt libneth's engine objects on
	@# a header change (lane/vecticks: env.h's run_config.rung linked against
	@# pre-rung survival.o, a crash at the first server tick)
	@if [ -z "$$(git status --porcelain oracle csrc)" ] && [ -f out/java/classes/.stamp ]; then \
	  cp -a out/java/classes $(L)/out/java/classes && cp -a out/native $(L)/out/native && \
	  { [ ! -f out/java/src.stamp ] || { cp -a oracle/src $(L)/oracle/src && cp -p out/java/src.stamp $(L)/out/java/; }; } && \
	  git ls-files -z oracle csrc | xargs -0 cp --parents --attributes-only --preserve=timestamps -t $(L) && \
	  find $(L)/out/native -name '*.d' -exec sed -i "s#$(CURDIR)/out/native#$(L)/out/native#g" {} +; fi
	@$(MAKE) -s -C $(L)/oracle && $(MAKE) -s -C $(L)/csrc && echo "lane $(NAME): $(L) on lane/$(NAME)"
# Link every recording master has that the lane lacks; a lane made before
# master gained recordings runs this again to see them (a lane's suite skips
# an area it has no recordings for, silently).
# Checkpoints sit one level deeper (checkpoints/<seed>/<name>): the lane gets
# a real seed directory with one link per checkpoint, so a checkpoint the lane
# saves stays in the lane (a linked seed directory let a lane overwrite
# master's cp-far-s1 on 2026-09-25) and the merge moves it to master.
# The missing names of an area are collected and linked by one ln (the
# positional parameters are the list; basename by expansion): a fork per
# recording took 24 s for master's 3,265 links at load 70 (11 s to find
# nothing missing), this 1.3 s (0.4 s).
lane-share:
	@test -n "$(NAME)" || { echo "make lane-share NAME=..."; exit 2; }
	@for s in $(CURDIR)/out/java/checkpoints/*/; do [ -d "$$s" ] || continue; sb=$${s%/}; sb=$${sb##*/}; \
	  t=$(L)/out/java/checkpoints/$$sb; [ -L $$t ] && rm $$t; mkdir -p $$t; set --; \
	  for d in $$s*/; do b=$${d%/}; b=$${b##*/}; \
	    [ -d "$$d" ] && [ ! -e $$t/$$b ] && [ ! -L $$t/$$b ] && set -- "$$@" "$$d"; \
	  done; [ $$# = 0 ] || ln -s "$$@" $$t/; done; true
	@for a in $(CURDIR)/out/java/*/; do k=$${a%/}; k=$${k##*/}; \
	  case " $(NOSHARE) checkpoints " in *" $$k "*) continue;; esac; mkdir -p $(L)/out/java/$$k; set --; \
	  for d in $$a*/; do b=$${d%/}; b=$${b##*/}; \
	    [ -d "$$d" ] && [ ! -e $(L)/out/java/$$k/$$b ] && [ ! -L $(L)/out/java/$$k/$$b ] && set -- "$$@" "$$d"; \
	  done; [ $$# = 0 ] || ln -s "$$@" $(L)/out/java/$$k/; done; true

lane-rm:
	@test -n "$(NAME)" || { echo "make lane-rm NAME=..."; exit 2; }
	@git worktree remove --force $(L) && git branch -D lane/$(NAME)

# The nether CLI (csrc/cli/*.c: nether.c the verbs' table, a file a module): out/bin/nether, the
# front door (nether help). One compile of every module, C11, no warning allowed; -lm for the statistics.
# make install PREFIX=DIR copies it to DIR/bin (default ~/.local).
PREFIX ?= $(HOME)/.local
CLI_SRC := $(sort $(wildcard csrc/cli/*.c))
CLI_HDR := $(wildcard csrc/cli/*.h)
.PHONY: cli install
cli: out/bin/nether
out/bin/nether: $(CLI_SRC) $(CLI_HDR)
	@mkdir -p out/bin
	$(CC) -O2 -std=c11 -Wall -Wextra -Werror -DNETH_ROOT='"$(CURDIR)"' -o $@ $(CLI_SRC) -lm
install: out/bin/nether
	@mkdir -p $(PREFIX)/bin && cp out/bin/nether $(PREFIX)/bin/nether && echo "installed $(PREFIX)/bin/nether"
