# lane/brutetab: the brute-force table's rows (GOAL.md "Done means" 1), a random policy over the vector env on the CPUs
# (train/brutetab.c's header has the flags and the output):
#
#   make -C csrc/runtime brutetab                  out/runtime/train/brutetab (links libneth)
#   make -C csrc/runtime brutetab-names-check      the macro names it and ntloop read: unique, their bits distinct
#   make -C csrc/runtime brutetab-row BT_RUN=configs/brutetab-chain.yaml [BT_ARGS='--macros recipe --seeds 1-1000']
#          one row, CUDA_VISIBLE_DEVICES= (no device touched)
BT_RUN  ?= configs/brutetab-early.yaml
BT_ARGS ?=

.PHONY: brutetab brutetab-names-check brutetab-row
brutetab: rlbind $(RT_OUT)/train/brutetab

$(RT_OUT)/train/brutetab: $(RT_DIR)/train/brutetab.c $(RT_DIR)/train/curric.h $(RT_DIR)/train/stages.h $(RT_DIR)/vecenv.h \
                          $(RT_DIR)/rlbind.h $(RL_LIB)
	@mkdir -p $(RT_OUT)/train
	cd $(RT_DIR) && $(RT_CC) $(RT_CFLAGS) $(RT_LDFLAGS) -o $@ train/brutetab.c -L$(RT_OUT) -Wl,-rpath,$(RT_OUT) -lneth -lm

# (lane/macromod: ntloop and brutetab read one table now, the macros' registration table's; the check holds it to
# unique names and distinct bits, none 0)
brutetab-names-check: $(RT_OUT)/train/brutetab
	@$(RT_OUT)/train/brutetab --macro-names > $(RT_OUT)/train/brutetab-names.tool; \
	  n=$$(wc -l < $(RT_OUT)/train/brutetab-names.tool); \
	  dn=$$(cut -d' ' -f1 $(RT_OUT)/train/brutetab-names.tool | sort | uniq -d | head -3); \
	  db=$$(cut -d' ' -f2 $(RT_OUT)/train/brutetab-names.tool | sort | uniq -d | head -3); \
	  z=$$(awk '$$2 == 0' $(RT_OUT)/train/brutetab-names.tool | head -3); \
	  if [ $$n -gt 0 ] && [ -z "$$dn$$db$$z" ]; then echo "BRUTETAB-NAMES PASS ($$n macro names, the registration table's)"; \
	  else echo "BRUTETAB-NAMES FAIL ($$n names; repeated: $$dn $$db; bit 0: $$z)"; exit 1; fi

brutetab-row: brutetab
	cd $(RT_ROOT) && CUDA_VISIBLE_DEVICES= $(RT_OUT)/train/brutetab --run $(BT_RUN) $(BT_ARGS)
