# Scripts in the tree

Every shell, Perl and awk file in the tree (99 at base 7db1ed3d: 96 .sh, 2 .pl, 1 .awk; 0 .py), audited on
2026-11-23 by lane/nether-polish. Callers are the Makefiles, .mk fragments, C sources, configs and other kept scripts
that run each one, found by git grep on its name. A script was deleted only when nothing but its own text, the
DEVLOG or another deleted script named it. Five were deleted, so 94 remain (91 .sh, 2 .pl, 1 .awk).

## csrc/cuda/lighting

| script | lines | status | callers | why |
|---|---|---|---|---|
| client_check.sh | 30 | kept | csrc/cuda/lighting/lighting.mk:gpu-lighting-client-check | Client-world light check wrapper; called by lighting.mk gpu-lighting-client-check. |

## csrc/cuda/render

| script | lines | status | callers | why |
|---|---|---|---|---|
| gate.sh | 235 | kept | csrc/cuda/meshing/meshing.mk:gpu-meshing-check, csrc/cuda/mirror.tsv, csrc/cuda/render/render.mk:gpu-render-bench, csrc/cuda/render/render.mk:gpu-render-check | Render/meshing quick gate driver; called by render.mk, meshing.mk and registered in mirror.tsv. |
| judge_gate.sh | 93 | kept | csrc/cuda/render/judge.mk:gpu-render-judge-check, csrc/tests/dragonrun.sh | Frame-judge device-vs-C gate; called by judge.mk gpu-render-judge-check. |

## csrc/cuda/tick

| script | lines | status | callers | why |
|---|---|---|---|---|
| build.sh | 42 | kept | csrc/cuda/tick/tick.mk:$(S6_OUT)/tick.cubin | Builds the S6 tick cubin/bitcode for the device; called by tick.mk and mirror.tsv. |
| sweep.sh | 94 | kept | csrc/cuda/tick/tick.mk:gpu-tick-check | Batched S6 tick check (gpu-tick-check); called by tick.mk. |

## csrc/play

| script | lines | status | callers | why |
|---|---|---|---|---|
| chain.sh | 77 | kept | csrc/tests/dragonrun.sh | Plays the seed-42 golden chain through play and checks it; named in AGENTS.md and called by dragonrun.sh and play.c messages. |
| check.sh | 51 | kept | csrc/cli/netherite.c, csrc/cuda/lighting/lighting.mk:gpu-lighting-check, csrc/cuda/lighting/lighting.mk:gpu-lighting-client-check, csrc/cuda/mirror.tsv, csrc/cuda/worldgen/worldgen.mk:gpu-worldgen-ahead-check, csrc/Makefile:$(OUT)/play-dev, csrc/Makefile:phase-kernel-check, csrc/Makefile:test, csrc/Makefile:worldconf-check, csrc/play/chain.sh, csrc/play/play.c, csrc/tests/fncov_recs.c, csrc/tests/play_check_sample.c, csrc/tests/play_check.sh, csrc/tests/playfuzz/agentrun.sh, csrc/tests/smoke.sh, csrc/tests/sweep/sweep.sh, csrc/tests/testmap.c, oracle/Makefile:oracle-src-check, oracle/Makefile:selfcheck, flywheel:glmprompt.sh | Checks a played tape on Java vs native; called by the CLI (netherite check), Makefiles and many kept scripts. |
| host.sh | 18 | kept | csrc/play/check.sh, csrc/play/rawjudge.sh, csrc/play/session.sh, oracle/Makefile:.SUFFIXES | Shared dev-host/Mac ssh helper sourced by check.sh, rawjudge.sh and session.sh. |
| rawjudge.sh | 228 | kept | csrc/Makefile:worldconf-check, csrc/tests/divergences/recheck/rawdrive.sh, csrc/tests/divergences.sh, csrc/tests/rawfuzz/run.sh, csrc/tests/rawjudge_plant.sh, csrc/tests/sweep/sweep.sh | Raw-event judge for a Java session; documented in AGENTS.md, called by sweep.sh, rawfuzz and Makefile. |
| session.sh | 378 | kept | csrc/cli/netherite.c, csrc/tests/divergences.sh, csrc/tests/playfuzz/chain.sh, csrc/tests/playfuzz/pins.txt, csrc/tests/playfuzz/run.sh, csrc/tests/session-proof/run.sh, csrc/tests/sweep/sweep.sh | Full-game session driver; called by the CLI (netherite play) and documented in AGENTS.md. |

## csrc/runtime

| script | lines | status | callers | why |
|---|---|---|---|---|
| castmatrix.sh | 300 | kept | csrc/runtime/castmatrix.mk:cast-matrix | Cast precision matrix driver; called by castmatrix.mk cast-matrix, listed in AGENTS.md Commands. |
| macrodig.sh | 70 | kept | csrc/runtime/vecenv.mk:macro-digests, csrc/runtime/vecenv.mk:macro-digests-pin | Macro digest matrix runner; called by vecenv.mk macro-digests, listed in AGENTS.md Commands. |
| pipe_play.sh | 51 | kept | csrc/runtime/pipe_gate.c | Makes play frames at a pipeline observation size; called from pipe_gate.c and AGENTS.md (PLAY=). |
| playgate.sh | 57 | kept | csrc/runtime/vecenv.mk:rl-play-gate | Play-vs-vector-env gate; called by vecenv.mk rl-play-gate. |
| rcfast.sh | 49 | kept | csrc/runtime/raycast.mk:raycast-fast-gate | Ray-cast fast-vs-full gate; called by raycast.mk raycast-fast-gate, listed in AGENTS.md. |
| rcparity.sh | 123 | kept | csrc/runtime/raycast.mk:raycast-parity | Ray-cast parity gate; called by raycast.mk raycast-parity and pool_gate.c. |

## csrc/runtime/train

| script | lines | status | callers | why |
|---|---|---|---|---|
| alone_gate.sh | 79 | kept | csrc/runtime/train/chain.mk:chain-alone-gate | Chain alone-vs-batched gate; called by chain.mk chain-alone-gate, listed in AGENTS.md. |
| chainrun.sh | 409 | kept | none (run by hand) | Lane/chain's long-run search supervisor (run by hand); kept because it is the only runner of chainverify.sh, whose output chain.c --bad reads, and the chain ledgers cite it as their proof. |
| chainverify.sh | 74 | kept | csrc/runtime/train/chainrun.sh | Proves a chain's nodes on the Java client; its bad-node list is what chain.c --bad FILE reads (chain.c:2202), and the chain ledgers cite it. |
| dbench.sh | 209 | kept | csrc/runtime/train/loop.mk:dragon-bench, csrc/runtime/train/loop.mk:dragon-bench-report | Dragon benchmark runner; called by loop.mk dragon-bench and dragon-bench-report. |
| earn.sh | 125 | kept | csrc/runtime/train/chain.mk:chain-earn | Earned-start builder; called by chain.mk chain-earn. |
| fwd.sh | 272 | kept | none (run by hand) | Borderline: no caller by code, but ka.sh -f documents it and ppoac.yaml/ppoac-eval.yaml name its draw/start/runfile commands as the provenance of their starts; kept for reproducibility. |
| harvest.sh | 169 | kept | csrc/engine/cpsave.c, csrc/runtime/playgate.sh, csrc/runtime/train/verify_kills.sh | Start-library harvester; documented in AGENTS.md Commands, called by playgate.sh and bank-smoke.yaml. |
| ka.sh | 127 | kept | csrc/runtime/train/chain.mk:chain-ka, csrc/runtime/train/earn.sh | Known-answer check of a chain attempt; called by chain.mk chain-ka and earn.sh, documented in AGENTS.md. |
| logbreak.sh | 18 | deleted | none | Lane/rawsearch one-off report (log eval success by spawn offer); no references anywhere in the tree, DEVLOG or flywheel. |
| rawdemo.sh | 167 | deleted | none | Lane/rawskill one-off HTML demo generator; only references are DEVLOG and a comment in rawruns.sh (which is itself DELETE), nothing builds or runs it. |
| raweval.sh | 119 | kept | csrc/runtime/train/raweval.mk:raweval | Raw-controls held-out eval; called by raweval.mk raweval. |
| rawruns.sh | 40 | deleted | none | Lane/rawsearch one-off TSV summariser of rawpix runs; only mentioned by rawdemo.sh (DELETE) and DEVLOG. |
| rawskill_sum.sh | 49 | kept | csrc/runtime/train/rawskill.mk:rawskill | Summary of a rawskill run; called by rawskill.mk rawskill. |
| verify_kills.sh | 62 | deleted | none | One-off kill verifier of a start library; nothing calls it (harvest.sh only appears as its own helper text), only DEVLOG history. |
| viewbooks.sh | 21 | kept | csrc/runtime/train/rawpix.mk:rawpix | View-books readout for a rawpix eval; called by rawpix.mk rawpix (RP_VIEW). |

## csrc/tests

| script | lines | status | callers | why |
|---|---|---|---|---|
| agent_values.sh | 77 | kept | none (run by hand) | Agent-input refusal check; named in AGENTS.md Dev ops as the check for refused values. |
| clientview.sh | 37 | kept | none (run by hand) | Writes clientview.jsonl (Java client render state) for snapshot recordings; play.c names it as the producer of the file it checks. |
| diverge.sh | 170 | kept | csrc/Makefile:$(OUT)/diverge | First-divergence report tool; built into out/native/diverge by Makefile, used by AGENTS.md. |
| divergences.sh | 596 | kept | flywheel:divgen-post.sh | Generates divergences.md from the ledgers; documented in AGENTS.md and run by flywheel divgen-post.sh. |
| dragonrun.sh | 340 | kept | none (run by hand) | Seed-1 chain to the dragon on Java, C and GPU in one table; no make caller, but pipeline_gate_chain.list names it as part of the batch run and it is a standing whole-engine check. |
| frame_judge.sh | 109 | kept | csrc/Makefile:mob-cull-check, csrc/Makefile:worldconf-check, csrc/tests/dragonrun.sh, csrc/tests/testmap.c | Frame-judge gate; called by Makefile test jobs and oracle/tests/obsframes.sh and coverage.sh. |
| genahead_check.sh | 39 | kept | csrc/cuda/mirror.tsv, csrc/cuda/worldgen/worldgen.mk:gpu-worldgen-ahead-check | Generation-ahead device check; called by worldgen.mk and mirror.tsv. |
| globals.sh | 30 | kept | csrc/Makefile:globals, csrc/tests/smoke.sh | Writable-globals check; called by Makefile globals and smoke.sh. |
| gui_judge.sh | 56 | kept | csrc/Makefile:worldconf-check, csrc/tests/testmap.c | GUI judge job; scheduled by the Makefile test job list for every guijudge directory. |
| hud_frame_gate.sh | 43 | kept | none (run by hand) | HUD pixel gate; cited as a proving test in index/csrc/engine/raster*.json records that index_check validates; no other caller. |
| light_check.sh | 141 | kept | csrc/cuda/lighting/lighting.mk:gpu-lighting-check, csrc/cuda/mirror.tsv | Light engine device check; called by lighting.mk gpu-lighting-check and mirror.tsv. |
| memsample.sh | 19 | kept | none (run by hand) | Memory-budget sampler; documented in AGENTS.md (memory rule re-measure). |
| memslot.sh | 257 | kept | csrc/cuda/lighting/client_check.sh, csrc/cuda/tick/build.sh, csrc/cuda/tick/sweep.sh, csrc/cuda/tick/tick.mk:gpu-tick-batch, csrc/Makefile:clean, csrc/Makefile:phase-check, csrc/Makefile:phase-kernel-cost, csrc/Makefile:phase-profile, csrc/Makefile:test, csrc/play/chain.sh, csrc/play/check.sh, csrc/play/rawjudge.sh, csrc/play/session.sh, csrc/runtime/pool.mk:pool-refs, csrc/tests/fncov_recs.c, csrc/tests/genahead_check.sh, csrc/tests/light_check.sh, csrc/tests/memsample.sh, csrc/tests/phase_kernel_check.sh, csrc/tests/playfuzz/agentrun.sh, csrc/tests/playfuzz/run.sh, csrc/tests/recs.sh, csrc/tests/smoke_select.c, csrc/tests/smoke.sh, csrc/tests/testmap.c, csrc/tests/testsel_history.c, csrc/tests/wldepth.sh, oracle/tests/headless.sh, flywheel:boot.sh,slothold.sh,suitetimes.sh | Machine-wide memory budget slots; called by many Makefile targets and scripts. |
| memstate.awk | 166 | kept | csrc/tests/memsample.sh, csrc/tests/memslot.sh | awk helper of memsample.sh and memslot.sh. |
| par.sh | 52 | kept | csrc/Makefile:$(OUT)/par | Parallel oracle-run tool; built into out/native/par by Makefile, documented in AGENTS.md. |
| phase_kernel_check.sh | 29 | kept | csrc/Makefile:phase-kernel-check | Called by Makefile phase-kernel-check. |
| phase_kernel_table.sh | 24 | kept | csrc/Makefile:phase-kernel-cost | Called by Makefile phase-kernel-cost. |
| phase_profile_table.sh | 50 | kept | csrc/Makefile:phase-profile | Called by Makefile phase-profile (documented in AGENTS.md). |
| play_check.sh | 56 | kept | csrc/Makefile:$(OUT)/play-dev, csrc/Makefile:test, csrc/Makefile:worldconf-check, csrc/tests/fncov_recs.c, csrc/tests/play_check_sample.c, csrc/tests/playfuzz/agentrun.sh, csrc/tests/smoke.sh, csrc/tests/sweep/sweep.sh, csrc/tests/testmap.c | Play check job; called by Makefile test jobs, smoke.sh, sweep.sh and play_check.list. |
| rawjudge_plant.sh | 33 | kept | none (run by hand) | Planted-mapping-bug proof of the raw judge; documented in AGENTS.md. |
| recs.sh | 81 | kept | csrc/Makefile:$(OUT)/recs | Parallel recording replayer; built into out/native/recs by Makefile, documented in AGENTS.md. |
| segments.sh | 35 | kept | csrc/Makefile:worldconf-check, csrc/tests/diverge.sh | Keyframe SEGMENTS check; called by Makefile worldconf-check and oracle keyframes.sh. |
| smoke.sh | 50 | kept | csrc/Makefile:smoke, oracle/tests/gate.sh | Smoke suite; called by Makefile smoke, documented in AGENTS.md. |
| wldepth.sh | 42 | kept | csrc/Makefile:worklist-depth | Worklist-depth reader; called by Makefile worklist-depth, documented in AGENTS.md. |

## csrc/tests/divergences/recheck

| script | lines | status | callers | why |
|---|---|---|---|---|
| rawdrive.sh | 10 | kept | none (run by hand) | Self-described throwaway re-check, but the divgen.tsv ledger cites it by path as a repro helper, so removing it breaks that pointer; weakest KEEP here. |

## csrc/tests/playfuzz

| script | lines | status | callers | why |
|---|---|---|---|---|
| agentrun.sh | 108 | kept | csrc/tests/sweep/sweep.sh | Agent-input fuzz runner; called by sweep.sh, re-record command in agentpins.txt. |
| chain.sh | 186 | kept | csrc/tests/sweep/sweep.sh | Chain fuzz runner; called by sweep.sh and listed in chains.txt. |
| pin.sh | 56 | kept | none (run by hand) | Pins a failing fuzz session as a recording; named as the pin route in kept chain.sh/run.sh and pins.txt; no direct caller. |
| run.sh | 163 | kept | none (run by hand) | Playfuzz runner for cases.txt/pins.txt; called by sweep.sh paths and kept scripts. |

## csrc/tests/rawfuzz

| script | lines | status | callers | why |
|---|---|---|---|---|
| keep.sh | 32 | kept | none (run by hand) | Keeps a rawfuzz session as a rawjudge directory; documented in AGENTS.md. |
| review.sh | 37 | kept | none (run by hand) | Surveys rawfuzz frames with pxdiff; documented in AGENTS.md. |
| run.sh | 103 | kept | csrc/tests/sweep/sweep.sh | Raw-event fuzz runner; documented in AGENTS.md and called by sweep.sh. |

## csrc/tests/session-proof

| script | lines | status | callers | why |
|---|---|---|---|---|
| run.sh | 25 | kept | none (run by hand) | Headless proof of the session chain; documented in AGENTS.md. |

## csrc/tests/sweep

| script | lines | status | callers | why |
|---|---|---|---|---|
| history.sh | 98 | deleted | none | One-time retrospective scoring of sig.sh clusters against past fixed failures; no caller, only DEVLOG history. |
| sig.sh | 138 | kept | csrc/tests/sweep/history.sh, csrc/tests/sweep/sweep.sh | Failure signature used by sweep.sh clustering (history.sh also uses it but is DELETE). |
| sweep.sh | 653 | kept | csrc/tests/divergences.sh | The per-master fuzz sweep (writes sweep.tsv ledger); divergences.sh documents it as running on every master. |

## oracle/decomp

| script | lines | status | callers | why |
|---|---|---|---|---|
| decompile.sh | 28 | kept | oracle/decomp/oracle-src.sh | Provenance decompile; documented in AGENTS.md Commands, used by oracle-src.sh. |
| determinize.sh | 27 | kept | oracle/decomp/mkpatch.sh, oracle/decomp/oracle-src-check.sh, oracle/decomp/oracle-src.sh, oracle/decomp/src.sh | Mechanical RNG/math pass of the oracle source; called by src.sh, oracle-src.sh, mkpatch.sh. |
| mkpatch.sh | 28 | kept | oracle/decomp/oraclepatch.pl, oracle/Makefile:oracle-patch, flywheel:divgen-post.sh | Builds oracle.patch; called by Makefile oracle-patch. |
| oracle-src-check.sh | 47 | kept | oracle/Makefile:oracle-src-check | Fresh-directory oracle-src proof; called by Makefile oracle-src-check. |
| oracle-src.sh | 135 | kept | oracle/decomp/oracle-src-check.sh, oracle/decomp/src.sh, oracle/Makefile:oracle-src | Rebuilds oracle/src from public inputs; Makefile oracle-src, CLI setup. |
| oraclepatch.pl | 232 | kept | oracle/decomp/mkpatch.sh, oracle/decomp/oracle-src-check.sh, oracle/decomp/oracle-src.sh, oracle/decomp/src.sh | Applies oracle.patch; called by src.sh, mkpatch.sh and oracle-src*.sh. |
| src.sh | 81 | kept | oracle/decomp/mkpatch.sh, oracle/decomp/oracle-src-check.sh, oracle/decomp/oracle-src.sh, oracle/Makefile:oracle-src, oracle/Makefile:pristine-diff, oracle/Makefile:src, tools/export.sh | Makes oracle/src on every build; Makefile src, CLI and export.sh. |
| treehash.sh | 11 | kept | oracle/decomp/oracle-src-check.sh | Tree hash helper of oracle-src-check.sh. |

## oracle/tests

| script | lines | status | callers | why |
|---|---|---|---|---|
| anytick.sh | 111 | kept | oracle/Makefile:anytick | Called by Makefile anytick. |
| classtore.sh | 47 | kept | oracle/Makefile:$(CLASSES)/.stamp, oracle/Makefile:full-build, flywheel:trymerge.sh | Class store; called by Makefile and flywheel trymerge.sh, in AGENTS.md. |
| coverage.sh | 180 | kept | oracle/Makefile:coverage, flywheel:trymerge.sh | JaCoCo coverage runner; Makefile coverage and flywheel trymerge.sh. |
| gate.sh | 61 | kept | oracle/Makefile:gate | Oracle gate suite; Makefile gate, CLI gate, AGENTS.md. |
| gold-chain.sh | 71 | kept | csrc/runtime/train/run.mk:train-prep, csrc/tests/dragonrun.sh | Golden chain recorder; called by run.mk train-prep and dragonrun.sh. |
| harness.sh | 22 | kept | oracle/Makefile:.SUFFIXES | Harness build helper; Makefile and classtore.sh. |
| headless.sh | 57 | kept | oracle/Makefile:.SUFFIXES, oracle/tests/poolauto.sh, oracle/tests/rawplay.sh, oracle/tests/rs.sh, oracle/tests/selhist.sh, oracle/tests/selinputs.sh | Headless Xvfb wrapper used by Makefile, poolauto.sh, rawplay.sh and others. |
| keyframes.sh | 168 | kept | oracle/Makefile:keyframes | Makefile keyframes. |
| obsframes.sh | 64 | kept | none (run by hand) | Oracle frame goldens maker; called from frame_judge.sh and Makefile test jobs (out of delete scope). |
| oraclecache.sh | 323 | kept | oracle/Makefile:rerecord | Oracle result cache; Makefile rerecord and documented in AGENTS.md. |
| play_synth.sh | 47 | kept | oracle/tests/gate.sh | Called by gate.sh; SPEC.md mentions it (out of delete scope). |
| pool-glproof.sh | 52 | kept | none (run by hand) | Pool GL proof; DEVLOG only but oracle/tests is outside the delete scope. |
| pool-proof.sh | 335 | kept | oracle/Makefile:rerecord | Pool proof; Makefile rerecord. |
| pool.sh | 92 | kept | oracle/Makefile:anytick, oracle/tests/poolauto.sh, oracle/tests/selinputs.sh | Warm oracle pool client; Makefile and poolauto.sh, in AGENTS.md. |
| poolauto.sh | 311 | kept | oracle/Makefile:anytick, oracle/Makefile:pool-shared-stop, oracle/Makefile:pool-status, oracle/Makefile:pool-warm, oracle/tests/selinputs.sh, flywheel:watchdog.sh | Machine-wide pool members; Makefile pool-* targets and flywheel watchdog.sh. |
| rawplay.sh | 29 | kept | csrc/tests/divergences/recheck/rawdrive.sh, csrc/tests/rawfuzz/run.sh | Scripted raw drive player; documented in AGENTS.md, used by rawfuzz/run.sh. |
| rs.sh | 31 | kept | none (run by hand) | Render-state helper referenced by test_renderstate.c and frame budget; oracle/tests outside delete scope. |
| selfcheck.sh | 169 | kept | oracle/Makefile:selfcheck | Makefile selfcheck, documented in AGENTS.md. |
| selhist-eval.sh | 61 | kept | none (run by hand) | DEVLOG only, but oracle/tests is outside the delete scope; candidate for a later pass (shares selinputs.sh). |
| selhist.sh | 146 | kept | none (run by hand) | DEVLOG only, but oracle/tests is outside the delete scope; candidate for a later pass. |
| selinputs.sh | 19 | kept | oracle/tests/coverage.sh, oracle/tests/selfcheck.sh, oracle/tests/selhist.sh, flywheel:covmap.sh | Selection inputs; called by coverage.sh, selfcheck.sh and flywheel covmap.sh. |
| tcp_smoke.sh | 16 | kept | oracle/tests/gate.sh | TCP control smoke; called by gate.sh, documented in AGENTS.md. |

## tools

| script | lines | status | callers | why |
|---|---|---|---|---|
| export.sh | 130 | kept | none (run by hand) | Public copy export and checks; documented in AGENTS.md. |
| langcheck.pl | 61 | kept | tools/export.sh | Language check run by export.sh. |

