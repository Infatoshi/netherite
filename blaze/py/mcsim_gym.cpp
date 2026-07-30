#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

extern "C" {
#include "py_gym_env_smoke.h"
void pges_bridge_reset(PgesEnv *g, u64 seed);
void pges_bridge_obs_after_reset(PgesEnv *g, PgesObs *obs);
void pges_bridge_step(PgesEnv *g, const PgesAction *action, PgesObs *obs,
                      float *reward, int *done);
const PgesAction *pges_bridge_replay_actions(int *n);
}

static py::dict obs_to_dict(const PgesObs &obs) {
    py::dict look;
    if (obs.look_type == 1) {
        look["type"] = "block";
        look["bx"] = obs.look_bx;
        look["by"] = obs.look_by;
        look["bz"] = obs.look_bz;
    } else {
        look["type"] = "miss";
        look["bx"] = 0;
        look["by"] = 0;
        look["bz"] = 0;
    }
    py::dict d;
    d["ok"] = true;
    d["x"] = obs.x;
    d["y"] = obs.y;
    d["z"] = obs.z;
    d["yaw"] = obs.yaw;
    d["pitch"] = obs.pitch;
    d["vx"] = obs.vx;
    d["vy"] = obs.vy;
    d["vz"] = obs.vz;
    d["on_ground"] = obs.on_ground != 0;
    d["look"] = look;
    d["tick"] = obs.tick;
    d["combined_hash"] = obs.combined_hash;
    return d;
}

static PgesAction dict_to_action(const py::dict &a) {
    PgesAction act{};
    auto bit = [&](const char *k) {
        return a.contains(k) && py::cast<int>(a[k]) != 0;
    };
    act.forward = bit("forward");
    act.back = bit("back");
    act.left = bit("left");
    act.right = bit("right");
    act.jump = bit("jump");
    act.sneak = bit("sneak");
    act.yaw = a.contains("yaw") ? py::cast<int>(a["yaw"]) : 0;
    act.pitch = a.contains("pitch") ? py::cast<int>(a["pitch"]) : 0;
    return act;
}

struct McSimEnv {
    PgesEnv env{};
    bool active = false;

    py::dict reset(u64 seed) {
        pges_bridge_reset(&env, seed);
        PgesObs obs{};
        pges_bridge_obs_after_reset(&env, &obs);
        active = true;
        return obs_to_dict(obs);
    }

    py::tuple step(const py::dict &action) {
        if (!active)
            throw std::runtime_error("call reset() before step()");
        PgesAction act = dict_to_action(action);
        PgesObs obs{};
        float reward = 0.0f;
        int done = 0;
        pges_bridge_step(&env, &act, &obs, &reward, &done);
        py::dict info;
        info["tick"] = obs.tick;
        info["combined_hash"] = obs.combined_hash;
        return py::make_tuple(obs_to_dict(obs), reward, done != 0, info);
    }
};

static py::list replay_actions_py() {
    int n = 0;
    const PgesAction *seq = pges_bridge_replay_actions(&n);
    py::list out;
    for (int i = 0; i < n; ++i) {
        py::dict a;
        a["forward"] = seq[i].forward;
        a["back"] = seq[i].back;
        a["left"] = seq[i].left;
        a["right"] = seq[i].right;
        a["jump"] = seq[i].jump;
        a["sneak"] = seq[i].sneak;
        a["yaw"] = seq[i].yaw;
        a["pitch"] = seq[i].pitch;
        out.append(a);
    }
    return out;
}

PYBIND11_MODULE(mcsim_gym, m) {
    m.doc() = "blaze gym.Env smoke wrapper (tick_compose_full)";

    py::class_<McSimEnv>(m, "McSimEnv")
        .def(py::init<>())
        .def("reset", &McSimEnv::reset, py::arg("seed") = 12345ULL)
        .def("step", &McSimEnv::step);

    m.def("replay_actions", &replay_actions_py);
}
