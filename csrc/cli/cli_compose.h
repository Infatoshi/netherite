/* nether CLI: the three-layer registry (csrc/cli/cli_compose.c). A scenario (configs/envs/NAME.yaml), a simulator
 * profile (configs/sim/NAME.yaml) and a learning algorithm (configs/algo/NAME.yaml), each named by --env, --sim and
 * --algo, composed in C into the files the verbs already read: a run file (sim: and policy:, what ntloop reads), a
 * gym (nether flywheel) or a bank (nether harvest). The composed files go to out/compose/, named by the layers and
 * their content's hash; configs/ never collects one. */
#ifndef NETH_CLI_COMPOSE_H
#define NETH_CLI_COMPOSE_H

#include "cli.h"
#include "cli_yaml.h"

/* train, eval, flywheel and harvest with --env/--sim/--algo: the layers composed and the verb run on what they make
 * (run(argc, argv) as given when no layer is named) */
int cc_dispatch(const char *verb, int argc, char **argv, int (*run)(int, char **));

/* nether list: every entry of configs/envs, configs/sim and configs/algo with its description */
int v_list(int argc, char **argv);

#endif
