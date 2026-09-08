/* Real Java bridge transport. Semantic observations, not framebuffer RGB.
 * strict_ticks requires authoritative player_tick/server_tick added by the
 * clock owner; old bridge responses remain diagnostic-only. */
#pragma once
#include "eval_magma.h"
#include <stdint.h>
typedef struct EvalOracle EvalOracle;
typedef struct {
  EvalMagmaObs obs;
  uint64_t action_seq, action_fnv;
  int64_t world_time, player_tick, server_tick;
  int have_ticks;
  int policy_locked, inventory_total;
  int inventory[36][3];
  int64_t world_seed;
  int have_world_seed;
  double vx, vy, vz;
  float health, fall_distance;
  int food, on_ground, have_physics;
} EvalOracleReceipt;
/* Exposed for replay/fixture validation; never accepts partial JSON. */
int eval_oracle_parse(const char *json, EvalOracleReceipt *out, char *err,
                      int cap);
/* Numeric IPv4 only. timeout_ms is a whole connect/request deadline.
 * prefix.requests.jsonl and prefix.responses.jsonl created exclusively.
 * Every complete raw response, including malformed JSON, is persisted.
 * Partial failed responses are saved in prefix.partial (exclusive). */
EvalOracle *eval_oracle_open(const char *ipv4, int port, int timeout_ms,
                             int strict_ticks, const char *prefix, char *err,
                             int cap);
void eval_oracle_close(EvalOracle *o);
/* Select step or policy_step before the first action. */
int eval_oracle_set_step_command(EvalOracle *o, const char *command);
/* Exact newline JSON control request; observation_reply requires the full
 * semantic schema, otherwise requires complete {ok:true,...}. */
int eval_oracle_command(EvalOracle *o, const char *request,
                        int observation_reply, char *err, int cap);
/* Integer field from the last successful non-observation control response. */
int eval_oracle_control_integer(const EvalOracle *o, const char *key,
                                int64_t *out);
int eval_oracle_observe(EvalOracle *o, char *err, int cap);
/* Exactly one simulation tick. Caller expands repeat with the same first-tick
 * primitive rule as eval_magma_step. Failure poisons transport: do not retry.
 */
int eval_oracle_step(EvalOracle *o, const double act13[13], char *err, int cap);
const EvalOracleReceipt *eval_oracle_receipt(const EvalOracle *o);
/* Parse one exact raw request for same-action replay. Returns 1 for a fully
 * specified policy_step, 0 for an explicitly allowed non-step command, -1
 * for malformed/unknown requests. Never guesses omitted action fields. */
int eval_oracle_parse_request(const char *json, double act13[13], char *err,
                              int cap);
