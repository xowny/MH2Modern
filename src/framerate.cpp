#include "framerate.h"

#include "input.h"
#include "logger.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <sstream>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace mh2modern::framerate {
namespace {

constexpr std::uint32_t kMinFrameLimitHz = 15;
constexpr std::uint32_t kMaxFrameLimitHz = 360;
constexpr std::uint32_t kDefaultStatsIntervalFrames = 600;
constexpr std::uint32_t kDefaultLateThresholdUs = 500;
constexpr std::uintptr_t kFrameLimiterImmediateRva = 0xD2A3;
constexpr std::uintptr_t kFrameLimiterElapsedCallRva = 0xD29A;

using ElapsedUsFn = std::uint32_t(__cdecl*)(void*);

std::atomic<std::uint32_t> g_frame_interval_us{};
std::atomic<std::uint32_t> g_background_frame_interval_us{};
std::atomic<std::uint32_t> g_spin_threshold_us{};
std::atomic<bool> g_hybrid_frame_pacing_enabled{};
std::atomic<bool> g_frame_pacing_stats_enabled{};
std::atomic<bool> g_background_frame_limit_enabled{};
std::atomic<int> g_last_focus_mode{-1};
ElapsedUsFn g_original_elapsed_us{};
PacingStatsAccumulator g_stats{kDefaultStatsIntervalFrames, kDefaultLateThresholdUs};

bool write_u32(std::uintptr_t address, std::uint32_t value) {
    DWORD old_protect{};
    auto* target = reinterpret_cast<std::uint32_t*>(address);
    if (!VirtualProtect(target, sizeof(value), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    *target = value;

    DWORD restored_protect{};
    VirtualProtect(target, sizeof(value), old_protect, &restored_protect);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(value));
    return true;
}

bool patch_relative_call(std::uintptr_t address, void* replacement, void** original_target_out) {
    auto* code = reinterpret_cast<std::uint8_t*>(address);
    if (code[0] != 0xE8) {
        return false;
    }

    const auto original_rel = *reinterpret_cast<std::int32_t*>(code + 1);
    const auto original_target =
        reinterpret_cast<void*>(address + 5 + static_cast<std::intptr_t>(original_rel));
    const auto replacement_rel =
        reinterpret_cast<std::intptr_t>(replacement) - static_cast<std::intptr_t>(address + 5);

    if (replacement_rel < INT32_MIN || replacement_rel > INT32_MAX) {
        return false;
    }

    DWORD old_protect{};
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    code[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(code + 1) = static_cast<std::int32_t>(replacement_rel);

    DWORD restored_protect{};
    VirtualProtect(code, 5, old_protect, &restored_protect);
    FlushInstructionCache(GetCurrentProcess(), code, 5);

    if (original_target_out != nullptr) {
        *original_target_out = original_target;
    }

    return true;
}

std::uint32_t read_elapsed_us(void* frame_timer_state) {
    if (g_original_elapsed_us == nullptr) {
        return 0;
    }

    return g_original_elapsed_us(frame_timer_state);
}

void log_pacing_stats(const PacingStatsSnapshot& snapshot, std::uint32_t late_threshold_us) {
    if (snapshot.frame_count == 0) {
        return;
    }

    std::ostringstream oss;
    oss << "Frame pacing sample " << snapshot.frame_count << "f"
        << ": pre_avg=" << snapshot.average_pre_elapsed_us << "us"
        << ", final_avg=" << snapshot.average_final_elapsed_us << "us"
        << ", min=" << snapshot.min_final_elapsed_us << "us"
        << ", max=" << snapshot.max_final_elapsed_us << "us"
        << ", sleep_avg=" << snapshot.average_sleep_ms << "ms"
        << ", slept=" << snapshot.slept_frames
        << ", late=" << snapshot.late_frames << " (>" << late_threshold_us << "us)"
        << ", overshoot_avg=" << snapshot.average_overshoot_us << "us"
        << ", overshoot_max=" << snapshot.max_overshoot_us << "us";
    logger::info(oss.str());
}

extern "C" std::uint32_t __cdecl hooked_frame_elapsed_us(void* frame_timer_state) {
    auto elapsed_us = read_elapsed_us(frame_timer_state);
    const auto pre_elapsed_us = elapsed_us;
    const auto window_active = input::is_game_window_active();
    const auto target_interval_us = select_frame_interval_us(
        window_active, g_background_frame_limit_enabled.load(), g_frame_interval_us.load(),
        g_background_frame_interval_us.load());

    const auto focus_mode = window_active ? 1 : 0;
    const auto previous_focus_mode = g_last_focus_mode.exchange(focus_mode);
    if (previous_focus_mode != focus_mode) {
        std::ostringstream oss;
        oss << "Frame pacing focus state: " << (window_active ? "foreground" : "background")
            << ", target=" << target_interval_us << " us";
        logger::info(oss.str());
    }

    std::uint32_t sleep_ms = 0;
    if (g_hybrid_frame_pacing_enabled.load() && target_interval_us != 0 && elapsed_us < target_interval_us) {
        const auto spin_threshold_us = g_spin_threshold_us.load();
        const auto remaining_us = target_interval_us - elapsed_us;
        sleep_ms = compute_sleep_milliseconds(remaining_us, spin_threshold_us);
        if (sleep_ms > 0) {
            ::Sleep(sleep_ms);
            elapsed_us = read_elapsed_us(frame_timer_state);
        }

        while (elapsed_us < target_interval_us) {
            YieldProcessor();
            elapsed_us = read_elapsed_us(frame_timer_state);
        }
    }

    if (g_frame_pacing_stats_enabled.load()) {
        g_stats.record_frame(pre_elapsed_us, elapsed_us, sleep_ms, target_interval_us);
        if (g_stats.should_flush()) {
            log_pacing_stats(g_stats.snapshot_and_reset(), g_stats.late_threshold_us());
        }
    }

    return elapsed_us;
}

}  // namespace

PacingStatsAccumulator::PacingStatsAccumulator(std::uint32_t flush_interval_frames,
                                               std::uint32_t late_threshold_us) {
    configure(flush_interval_frames, late_threshold_us);
}

void PacingStatsAccumulator::configure(std::uint32_t flush_interval_frames,
                                       std::uint32_t late_threshold_us) {
    flush_interval_frames_ = (std::max)(1U, flush_interval_frames);
    late_threshold_us_ = late_threshold_us;
    snapshot_and_reset();
}

void PacingStatsAccumulator::record_frame(std::uint32_t pre_elapsed_us, std::uint32_t final_elapsed_us,
                                          std::uint32_t sleep_ms, std::uint32_t target_interval_us) {
    ++frame_count_;
    pre_elapsed_sum_us_ += pre_elapsed_us;
    final_elapsed_sum_us_ += final_elapsed_us;
    sleep_sum_ms_ += sleep_ms;

    if (sleep_ms > 0) {
        ++slept_frames_;
    }

    min_final_elapsed_us_ = (std::min)(min_final_elapsed_us_, final_elapsed_us);
    max_final_elapsed_us_ = (std::max)(max_final_elapsed_us_, final_elapsed_us);

    const auto overshoot_us = final_elapsed_us > target_interval_us ? final_elapsed_us - target_interval_us : 0U;
    overshoot_sum_us_ += overshoot_us;
    max_overshoot_us_ = (std::max)(max_overshoot_us_, overshoot_us);
    if (overshoot_us > late_threshold_us_) {
        ++late_frames_;
    }
}

bool PacingStatsAccumulator::should_flush() const {
    return frame_count_ >= flush_interval_frames_;
}

PacingStatsSnapshot PacingStatsAccumulator::snapshot_and_reset() {
    PacingStatsSnapshot snapshot{};
    snapshot.frame_count = frame_count_;
    snapshot.slept_frames = slept_frames_;
    snapshot.late_frames = late_frames_;
    snapshot.min_final_elapsed_us =
        frame_count_ == 0 ? 0U : min_final_elapsed_us_;
    snapshot.max_final_elapsed_us = max_final_elapsed_us_;
    if (frame_count_ != 0) {
        snapshot.average_pre_elapsed_us =
            static_cast<std::uint32_t>(pre_elapsed_sum_us_ / frame_count_);
        snapshot.average_final_elapsed_us =
            static_cast<std::uint32_t>(final_elapsed_sum_us_ / frame_count_);
        snapshot.average_sleep_ms =
            static_cast<std::uint32_t>(sleep_sum_ms_ / frame_count_);
        snapshot.average_overshoot_us =
            static_cast<std::uint32_t>(overshoot_sum_us_ / frame_count_);
        snapshot.max_overshoot_us = max_overshoot_us_;
    }

    frame_count_ = 0;
    slept_frames_ = 0;
    late_frames_ = 0;
    min_final_elapsed_us_ = std::numeric_limits<std::uint32_t>::max();
    max_final_elapsed_us_ = 0;
    max_overshoot_us_ = 0;
    pre_elapsed_sum_us_ = 0;
    final_elapsed_sum_us_ = 0;
    sleep_sum_ms_ = 0;
    overshoot_sum_us_ = 0;

    return snapshot;
}

std::uint32_t PacingStatsAccumulator::late_threshold_us() const {
    return late_threshold_us_;
}

std::uint32_t clamp_frame_limit_hz(std::uint32_t hz) {
    return std::clamp(hz, kMinFrameLimitHz, kMaxFrameLimitHz);
}

std::uint32_t frame_limit_hz_to_interval_us(std::uint32_t hz) {
    if (hz == 0) {
        return 0;
    }

    const auto clamped_hz = clamp_frame_limit_hz(hz);
    return (1'000'000U + (clamped_hz / 2U)) / clamped_hz;
}

std::uint32_t compute_sleep_milliseconds(std::uint32_t remaining_us, std::uint32_t spin_threshold_us) {
    if (remaining_us <= spin_threshold_us) {
        return 0;
    }

    return (remaining_us - spin_threshold_us) / 1000U;
}

std::uint32_t select_frame_interval_us(bool window_active, bool enable_background_limit,
                                       std::uint32_t foreground_interval_us,
                                       std::uint32_t background_interval_us) {
    if (!window_active && enable_background_limit && background_interval_us != 0U) {
        return background_interval_us;
    }
    return foreground_interval_us;
}

bool install_frame_limit_patch(HMODULE game_module, std::uint32_t hz,
                               bool enable_hybrid_frame_pacing, std::uint32_t spin_threshold_us,
                               bool enable_frame_pacing_stats, std::uint32_t stats_interval_frames,
                               std::uint32_t late_threshold_us,
                               bool enable_background_limit, std::uint32_t background_hz) {
    if (game_module == nullptr || hz == 0) {
        return false;
    }

    const auto clamped_hz = clamp_frame_limit_hz(hz);
    const auto interval_us = frame_limit_hz_to_interval_us(clamped_hz);
    const auto background_interval_us =
        enable_background_limit ? frame_limit_hz_to_interval_us(clamp_frame_limit_hz(background_hz)) : 0U;
    const auto immediate_address =
        reinterpret_cast<std::uintptr_t>(game_module) + kFrameLimiterImmediateRva;
    if (!write_u32(immediate_address, interval_us)) {
        logger::error("Failed to patch built-in frame limiter immediate");
        return false;
    }

    g_frame_interval_us.store(interval_us);
    g_background_frame_interval_us.store(background_interval_us);
    g_spin_threshold_us.store(spin_threshold_us);
    g_hybrid_frame_pacing_enabled.store(enable_hybrid_frame_pacing);
    g_frame_pacing_stats_enabled.store(enable_frame_pacing_stats);
    g_background_frame_limit_enabled.store(enable_background_limit);
    g_last_focus_mode.store(-1);
    g_stats.configure(stats_interval_frames, late_threshold_us);

    std::ostringstream oss;
    oss << "Patched frame limiter to " << clamped_hz << " Hz (" << interval_us << " us/frame)";
    logger::info(oss.str());

    if (!enable_hybrid_frame_pacing && !enable_frame_pacing_stats) {
        logger::info("Hybrid frame pacing disabled; using stock busy-wait limiter");
        return true;
    }

    void* original_target = nullptr;
    const auto callsite_address =
        reinterpret_cast<std::uintptr_t>(game_module) + kFrameLimiterElapsedCallRva;
    if (!patch_relative_call(
            callsite_address, reinterpret_cast<void*>(&hooked_frame_elapsed_us), &original_target)) {
        logger::error("Failed to patch frame limiter elapsed-time callsite");
        return false;
    }

    g_original_elapsed_us = reinterpret_cast<ElapsedUsFn>(original_target);

    if (enable_hybrid_frame_pacing) {
        std::ostringstream pacing_oss;
        pacing_oss << "Installed hybrid frame pacing with spin threshold " << spin_threshold_us << " us";
        logger::info(pacing_oss.str());
    } else {
        logger::info("Installed frame pacing stats hook without hybrid pacing");
    }

    if (enable_frame_pacing_stats) {
        std::ostringstream stats_oss;
        stats_oss << "Enabled frame pacing stats every " << (std::max)(1U, stats_interval_frames)
                  << " frames, late threshold " << late_threshold_us << " us";
        logger::info(stats_oss.str());
    }

    if (enable_background_limit && background_interval_us != 0U) {
        std::ostringstream bg_oss;
        bg_oss << "Enabled background frame limit: " << clamp_frame_limit_hz(background_hz)
               << " Hz (" << background_interval_us << " us/frame)";
        logger::info(bg_oss.str());
    }

    return true;
}

}  // namespace mh2modern::framerate
