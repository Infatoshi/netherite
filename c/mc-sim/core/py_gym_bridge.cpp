/* C++ bridge: expose PgesEnv to pybind11 (host-only, no CUDA). */
#include "py_gym_env_smoke.h"

#include <cstring>

extern "C" {

void pges_bridge_reset(PgesEnv *g, u64 seed) { pges_reset(g, seed); }

void pges_bridge_obs_after_reset(PgesEnv *g, PgesObs *obs) { pges_obs_after_reset(g, obs); }

void pges_bridge_step(PgesEnv *g, const PgesAction *action, PgesObs *obs,
                      float *reward, int *done) {
    pges_step(g, action, obs, reward, done);
}

const PgesAction *pges_bridge_replay_actions(int *n) {
    *n = PGES_N_TICKS;
    return PGES_REPLAY;
}

} /* extern "C" */
