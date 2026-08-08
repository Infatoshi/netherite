#pragma once

#include <torch/torch.h>

#include <cstdlib>

namespace netherite::ppo {

using torch::indexing::Slice;

struct RewardSpec {
  float time_cost = 0.01F;
  float death_penalty = 5.0F;
  float w_log_per = 1.0F;
  float log_clamp = 5.0F;
  float w_plank_first = 2.0F;
  float w_stick_first = 2.0F;
  float w_table_first = 3.0F;
  float w_container_open = 4.0F;
  float w_wpick_first = 6.0F;
  float w_cobble_per = 1.0F;
  float cobble_clamp = 4.0F;
  float w_spick_first = 0.0F;
  float w_coal_first = 6.0F;
  float w_torch_first = 12.0F;
  float chop_dist_coef = 0.5F;
  float chop_dist_clamp = 1.0F;
  float chop_crosshair = 0.03F;
  float dig_descend_coef = 0.25F;
  float dig_stone_atk = 0.02F;
  float dig_hold_pick = 0.005F;
  float digprog_coef = 0.0015F;
  float coal_dist_coef = 0.5F;
  float coal_dist_clamp = 1.0F;
  float coal_crosshair = 0.03F;
  float coal_crosshair_maxd = 3.5F;
  float coal_hold_pick = 0.005F;
  float coal_chew = 0.0F;
  float hunt_desc = 0.0F;

  static RewardSpec resolve() {
    RewardSpec spec;
    if (const char *value = std::getenv("COAL_CHEW")) {
      spec.coal_chew = std::strtof(value, nullptr);
    }
    if (const char *value = std::getenv("HUNT_DESC")) {
      spec.hunt_desc = std::strtof(value, nullptr);
    }
    return spec;
  }
};

// Fully GPU-side reward. No .item() / host-side any() syncs on the hot path.
// Stage masks select which shaping terms apply; inactive lanes get zero terms
// and sentinel prev_* state identical to the previous branched implementation.
class NativeReward {
public:
  NativeReward(int count, torch::Device device, RewardSpec spec)
      : count_(count), spec_(spec) {
    auto i32 = torch::TensorOptions().dtype(torch::kInt32).device(device);
    auto f32 = torch::TensorOptions().dtype(torch::kFloat32).device(device);
    auto boolean = torch::TensorOptions().dtype(torch::kBool).device(device);
    best = torch::zeros({count, 9}, i32);
    flag_cont_ = torch::zeros({count}, boolean);
    prev_logd_ = torch::full({count}, -1.0, f32);
    prev_coald_ = torch::full({count}, -1.0, f32);
    prev_y_ = torch::zeros({count}, f32);
    prev_digp_ = torch::zeros({count}, f32);
    min_y_ = torch::full({count}, 1.0e9, f32);
  }

  void reset(const torch::Tensor &indices) {
    best.index_put_({indices}, 0);
    flag_cont_.index_put_({indices}, false);
    prev_logd_.index_put_({indices}, -1.0);
    prev_coald_.index_put_({indices}, -1.0);
    prev_y_.index_put_({indices}, -1.0e9);
    prev_digp_.index_put_({indices}, 1.0e9);
    min_y_.index_put_({indices}, 1.0e9);
  }

  torch::Tensor step(const torch::Tensor &status, const torch::Tensor &cam,
                     const torch::Tensor &actions, const torch::Tensor &pose,
                     const torch::Tensor &scal, const torch::Tensor &done,
                     const torch::Tensor &lane_seed,
                     const torch::Tensor &log_positions) {
    auto newmax = torch::maximum(best, status.index({Slice(), Slice(0, 9)}));
    auto delta = (newmax - best).to(torch::kFloat32);
    auto reward = torch::full(
        {count_}, -spec_.time_cost,
        torch::TensorOptions().dtype(torch::kFloat32).device(status.device()));
    reward +=
        delta.index({Slice(), 0}).clamp_max(spec_.log_clamp) * spec_.w_log_per;
    reward += first_gain(best, newmax, 1) * spec_.w_plank_first;
    reward += first_gain(best, newmax, 2) * spec_.w_stick_first;
    reward += first_gain(best, newmax, 4) * spec_.w_table_first;
    auto container_now = status.index({Slice(), 11}).eq(1);
    reward += (container_now & ~flag_cont_).to(torch::kFloat32) *
              spec_.w_container_open;
    flag_cont_ |= container_now;
    reward += first_gain(best, newmax, 5) * spec_.w_wpick_first;
    reward += delta.index({Slice(), 3}).clamp_max(spec_.cobble_clamp) *
              spec_.w_cobble_per;
    if (spec_.w_spick_first != 0.0F) {
      reward += first_gain(best, newmax, 6) * spec_.w_spick_first;
    }
    reward += first_gain(best, newmax, 7) * spec_.w_coal_first;
    reward += first_gain(best, newmax, 8) * spec_.w_torch_first;
    best = newmax;

    auto center = cam.index({Slice(), 18, 32});
    auto attack = actions.index({Slice(), 4}).eq(1);
    auto chopping = best.index({Slice(), 0}).lt(3) &
                    best.index({Slice(), 1}).eq(0) &
                    best.index({Slice(), 5}).eq(0);

    // Chop distance shaping for every lane; non-chopping lanes get zero and
    // their prev_logd sentinel is cleared without a host-side any().
    {
      auto xyz = pose.index({Slice(), Slice(0, 3)}).clone();
      xyz.index({Slice(), 1}) += 1.62F;
      auto distances =
          std::get<0>((log_positions.index({lane_seed}) - xyz.unsqueeze(1))
                          .square()
                          .sum(-1)
                          .min(1))
              .sqrt();
      auto had_previous = prev_logd_.ge(0);
      auto shaping = torch::where(
          had_previous, (prev_logd_ - distances) * spec_.chop_dist_coef,
          torch::zeros_like(distances));
      auto crosshair =
          (attack & center.eq(17)).to(torch::kFloat32) * spec_.chop_crosshair;
      reward += chopping.to(torch::kFloat32) *
                (shaping.clamp(-spec_.chop_dist_clamp, spec_.chop_dist_clamp) +
                 crosshair);
      prev_logd_ =
          torch::where(chopping, distances, torch::full_like(prev_logd_, -1.0));
    }

    auto digging =
        best.index({Slice(), 5}).gt(0) & best.index({Slice(), 3}).lt(3);
    {
      auto vertical = (prev_y_ - pose.index({Slice(), 1})).clamp(0.0, 2.0);
      reward += digging.to(torch::kFloat32) * spec_.dig_descend_coef * vertical;
      auto held_pick = status.index({Slice(), 10}).eq(270);
      auto stone = center.eq(1) | center.eq(4) | center.eq(3) | center.eq(2);
      reward += (digging & attack & held_pick & stone).to(torch::kFloat32) *
                spec_.dig_stone_atk;
      reward += (digging & held_pick).to(torch::kFloat32) * spec_.dig_hold_pick;
    }
    prev_y_ = pose.index({Slice(), 1}).clone();

    auto has_pick = best.index({Slice(), 5}).gt(0);
    auto dig_progress = status.index({Slice(), 12}).to(torch::kFloat32);
    auto progress_delta = (dig_progress - prev_digp_).clamp_min(0.0);
    auto diggable_pixel = center.eq(1) | center.eq(16);
    reward += (has_pick & status.index({Slice(), 10}).eq(270) & diggable_pixel)
                  .to(torch::kFloat32) *
              spec_.digprog_coef * progress_delta;
    prev_digp_ = dig_progress;

    auto hunting = best.index({Slice(), 5}).gt(0) &
                   best.index({Slice(), 3}).ge(3) &
                   best.index({Slice(), 7}).eq(0);
    auto no_coal_scan =
        scal.index({Slice(), 0}).eq(0) & scal.index({Slice(), 1}).eq(0) &
        scal.index({Slice(), 2}).eq(0) & scal.index({Slice(), 3}).eq(1);
    {
      auto coal_distance = scal.index({Slice(), 3}) * 24.0;
      auto usable = hunting & ~no_coal_scan;
      auto had_previous = usable & prev_coald_.ge(0);
      auto shaping = torch::where(
          had_previous, (prev_coald_ - coal_distance) * spec_.coal_dist_coef,
          torch::zeros({count_}, reward.options()));
      reward += shaping.clamp(-spec_.coal_dist_clamp, spec_.coal_dist_clamp);
      prev_coald_ = torch::where(usable, coal_distance,
                                 torch::full_like(prev_coald_, -1.0));
      reward += (hunting & attack & center.eq(16) &
                 coal_distance.le(spec_.coal_crosshair_maxd))
                    .to(torch::kFloat32) *
                spec_.coal_crosshair;
      auto any_pick = status.index({Slice(), 10}).eq(270) |
                      status.index({Slice(), 10}).eq(274);
      reward += (hunting & any_pick).to(torch::kFloat32) * spec_.coal_hold_pick;
      if (spec_.coal_chew > 0.0F) {
        reward += (hunting & center.eq(16) & any_pick).to(torch::kFloat32) *
                  spec_.coal_chew * progress_delta;
      }
    }

    if (spec_.hunt_desc > 0.0F) {
      auto record_gain = (min_y_ - pose.index({Slice(), 1})).clamp(0.0, 2.0);
      record_gain = torch::where(min_y_.gt(1.0e8),
                                 torch::zeros_like(record_gain), record_gain);
      reward += (hunting & no_coal_scan).to(torch::kFloat32) * spec_.hunt_desc *
                record_gain;
    }
    min_y_ = torch::minimum(min_y_, pose.index({Slice(), 1}));
    reward += done.eq(2).to(torch::kFloat32) * -spec_.death_penalty;
    return reward;
  }

  torch::Tensor best;

private:
  static torch::Tensor first_gain(const torch::Tensor &old_best,
                                  const torch::Tensor &new_best, int column) {
    return (old_best.index({Slice(), column}).eq(0) &
            new_best.index({Slice(), column}).gt(0))
        .to(torch::kFloat32);
  }

  int count_;
  RewardSpec spec_;
  torch::Tensor flag_cont_;
  torch::Tensor prev_logd_;
  torch::Tensor prev_coald_;
  torch::Tensor prev_y_;
  torch::Tensor prev_digp_;
  torch::Tensor min_y_;
};

} // namespace netherite::ppo
