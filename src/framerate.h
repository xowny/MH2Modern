#pragma once

#include <cstdint>
#include <windows.h>

namespace mh2modern::framerate {

struct PacingStatsSnapshot {
    std::uint32_t frame_count{};
    std::uint32_t slept_frames{};
    std::uint32_t late_frames{};
    std::uint32_t min_final_elapsed_us{};
    std::uint32_t max_final_elapsed_us{};
    std::uint32_t average_pre_elapsed_us{};
    std::uint32_t average_final_elapsed_us{};
    std::uint32_t average_sleep_ms{};
    std::uint32_t average_overshoot_us{};
    std::uint32_t max_overshoot_us{};
};

class PacingStatsAccumulator {
public:
    PacingStatsAccumulator(std::uint32_t flush_interval_frames, std::uint32_t late_threshold_us);

    void configure(std::uint32_t flush_interval_frames, std::uint32_t late_threshold_us);
    void record_frame(std::uint32_t pre_elapsed_us, std::uint32_t final_elapsed_us,
                      std::uint32_t sleep_ms, std::uint32_t target_interval_us);
    bool should_flush() const;
    PacingStatsSnapshot snapshot_and_reset();
    std::uint32_t late_threshold_us() const;

private:
    std::uint32_t flush_interval_frames_{};
    std::uint32_t late_threshold_us_{};
    std::uint32_t frame_count_{};
    std::uint32_t slept_frames_{};
    std::uint32_t late_frames_{};
    std::uint32_t min_final_elapsed_us_{};
    std::uint32_t max_final_elapsed_us_{};
    std::uint32_t max_overshoot_us_{};
    std::uint64_t pre_elapsed_sum_us_{};
    std::uint64_t final_elapsed_sum_us_{};
    std::uint64_t sleep_sum_ms_{};
    std::uint64_t overshoot_sum_us_{};
};

std::uint32_t clamp_frame_limit_hz(std::uint32_t hz);
std::uint32_t frame_limit_hz_to_interval_us(std::uint32_t hz);
std::uint32_t compute_sleep_milliseconds(std::uint32_t remaining_us, std::uint32_t spin_threshold_us);
std::uint32_t select_frame_interval_us(bool window_active, bool enable_background_limit,
                                       std::uint32_t foreground_interval_us,
                                       std::uint32_t background_interval_us);
bool install_frame_limit_patch(HMODULE game_module, std::uint32_t hz,
                               bool enable_hybrid_frame_pacing, std::uint32_t spin_threshold_us,
                               bool enable_frame_pacing_stats, std::uint32_t stats_interval_frames,
                               std::uint32_t late_threshold_us,
                               bool enable_background_limit, std::uint32_t background_hz);

}  // namespace mh2modern::framerate
