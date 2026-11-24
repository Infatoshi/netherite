# lane/polish: what a run file needs to train on a fresh host, each made where it is missing (nether train and
# nether score run it first):
#
#   make -C csrc/runtime train-prep RUN=configs/NAME.yaml
#
#   the trainer   out/runtime/train/ntloop for a 16x16 observation, out/runtime/train/s1/ntloop for 64x64 (net.h's
#                 variants; any other size is refused), with libneth, playview.so, ntrec and the variant's ntinit
#                 (a fresh net's weights, train/ntinit.c)
#   the textures  out/assets (make -C csrc assets, from the client jar nether setup copied) and
#                 out/java/render/forest-fast (the cast's block table and atlas: the oracle's render-scenes)
#   the start     the run file's pipeline.start: a configs/checkpoints.tsv name or a snapshot under out/java/snapshots,
#                 recorded by the oracle from oracle/tests/NAME.jsonl on the seed its name ends in (-sN) when the
#                 script starts from a fresh world (gold-g1-s42, seed 42's spawn, about 684 ticks); a later segment of
#                 a chain is refused with the chain's command
#
# The last line is TRAIN-PREP OK BIN, BIN the trainer's path from the tree's root.
#   make -C csrc/runtime ntrec-bin    out/runtime/train/ntrec alone (nether replay)
NTP_RUN = $(if $(filter /%,$(RUN)),$(RUN),$(RT_ROOT)/$(RUN))
NTP_OBS = $(shell awk '$$1 == "observation:" {print $$2; exit}' $(NTP_RUN) 2>/dev/null)
NTP_V   = $(if $(filter 64x64,$(NTP_OBS)),s1,)
NTP_BIN = $(if $(NTP_V),$(NT_OUT)/$(NTP_V)/ntloop,$(NT_OUT)/ntloop)

NTP_INIT = $(dir $(NTP_BIN))ntinit

.PHONY: train-prep train-prep-check ntrec-bin
ntrec-bin: $(NT_OUT)/ntrec

# ntinit (train/ntinit.c): a fresh net of the variant as a .ntw (nether train's start without --init), built with
# the variant's -D flags like its ntloop (lane/audit-fixes: with neth_checkpoint.c's parser and writer, -DNETH_NTW_ONLY)
$(NT_OUT)/ntinit: $(NT_DIR)/ntinit.c $(NT_DIR)/net.h $(NT_DIR)/neth_checkpoint.c
	@mkdir -p $(@D)
	$(RT_CC) -O2 -std=c11 -Wall -Wextra -Werror -DNETH_NTW_ONLY -o $@ $< $(NT_DIR)/neth_checkpoint.c -lm

$(NT_OUT)/%/ntinit: $(NT_DIR)/ntinit.c $(NT_DIR)/net.h $(NT_DIR)/neth_checkpoint.c
	@mkdir -p $(@D)
	$(RT_CC) -O2 -std=c11 -Wall -Wextra -Werror -DNETH_NTW_ONLY $(call nt_vflags,$*) -o $@ $< $(NT_DIR)/neth_checkpoint.c -lm

train-prep-check:
	@[ -n "$(RUN)" ] || { echo "train-prep: RUN=configs/NAME.yaml"; exit 2; }; \
	  [ -f "$(NTP_RUN)" ] || { echo "train-prep: no run file $(NTP_RUN)"; exit 2; }; \
	  case "$(NTP_OBS)" in 16x16|64x64) ;; \
	    *) echo "train-prep: $(RUN)'s client.observation is '$(NTP_OBS)': the native trainer is built for 16x16 (the default) and 64x64 (s1)"; exit 2;; esac; \
	  grep -q '^    camera: raycast' $(NTP_RUN) || { echo "train-prep: $(RUN)'s client.camera is not raycast (the native trainer's observation is the ray cast)"; exit 2; }; \
	  [ -f $(RT_ROOT)/out/java/client/1.7.10.jar ] || { echo "train-prep: no client jar (out/java/client/1.7.10.jar): run nether setup first"; exit 2; }

train-prep: train-prep-check
	@$(MAKE) --no-print-directory -s -C $(RT_NATIVE) assets
	@cd $(RT_ROOT) && s=out/java/render/forest-fast; [ -f $$s/atlas.rgba ] && [ -f $$s/atlas.json ] || \
	  { echo "train-prep: the cast's atlas ($$s): make -C oracle render-scenes"; \
	    $(MAKE) --no-print-directory -s -C oracle render-scenes || exit 1; }
	@cd $(RT_ROOT) && start=$$(awk '$$1 == "start:" {print $$2; exit}' $(NTP_RUN)); \
	  [ -n "$$start" ] || { echo "train-prep: $(RUN) names no pipeline.start"; exit 2; }; \
	  snap=$$(awk -F'\t' -v n="$$start" '$$1 == n {print $$2; exit}' configs/checkpoints.tsv); \
	  case "$$start" in /*) snap=$$start;; esac; \
	  [ -n "$$snap" ] || { [ -f $$start/manifest.json ] && snap=$$start; }; \
	  [ -n "$$snap" ] || snap=out/java/snapshots/$$start; \
	  if [ ! -f $$snap/manifest.json ]; then \
	    name=$$(basename $$snap); script=oracle/tests/$$name.jsonl; seed=$${name##*-s}; \
	    case "$$name" in gold-g1-s42) ;; gold-g*) echo "train-prep: the start $$start ($$snap) is a later segment of the golden chain: record it with (cd oracle && bash tests/gold-chain.sh $${name#gold-g} 1), about a minute a segment"; exit 2;; esac; \
	    [ -f $$script ] && case "$$seed" in ''|*[!0-9]*) false;; esac && \
	      ! grep -q '"cmd":"run","class":"\(Load\|From\)' $$script || \
	      { echo "train-prep: the start $$start ($$snap) is missing and $$script does not make it from a fresh world"; exit 2; }; \
	    dev=; grep -q '"cmd":"dev"' $$script && dev=DEV=1; \
	    echo "train-prep: recording the start $$start ($$snap) on the oracle: make -C oracle script SEED=$$seed $$dev SCRIPT=tests/$$name.jsonl"; \
	    mkdir -p $$snap && $(MAKE) --no-print-directory -s -C oracle script SEED=$$seed $$dev SCRIPT=tests/$$name.jsonl \
	      TAPE=$(RT_ROOT)/$$snap/tape.jsonl > $$snap.log 2>&1 && [ -f $$snap/manifest.json ] || \
	      { tail -3 $$snap.log; echo "train-prep: recording $$snap failed ($$snap.log)"; exit 1; }; \
	    rm -f $$snap.log; \
	  fi
	@$(MAKE) --no-print-directory -s -C $(RT_DIR) rlbind $(NT_OUT)/ntrec $(NTP_BIN) $(NTP_INIT)
	@echo "TRAIN-PREP OK $(patsubst $(RT_ROOT)/%,%,$(NTP_BIN))"
