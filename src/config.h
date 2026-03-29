#pragma once

#include <cstdint>
#include <string_view>
#include <windows.h>

namespace mh2modern::config {

enum class PrecisionMode {
    Unchanged,
    Single24,
    Double53,
    Extended64,
};

struct Settings {
    bool enable_get_tick_count_hook{false};
    bool enable_sleep_spin_fix{false};
    bool enable_crash_dumps{true};
    bool log_timer_calls{false};
    bool log_version_calls{false};
    bool log_startup_details{false};
    std::uint32_t startup_slow_io_threshold_ms{3};
    bool enable_affinity_modernization{true};
    bool log_affinity_patch{false};
    bool enable_dpi_awareness{true};
    bool enable_timer_resolution{true};
    std::uint32_t timer_resolution_ms{1};
    bool disable_power_throttling{true};
    bool log_platform_modernization{false};
    bool enable_d3d9_probe{true};
    bool log_render_hooks{false};
    bool enable_render_presentation_policy{true};
    bool render_force_fullscreen_interval_one{true};
    bool enable_render_reset_sanitation{true};
    bool enable_render_device_lost_throttle{true};
    std::uint32_t render_device_lost_sleep_ms{1};
    bool enable_raw_mouse_input{true};
    bool enable_cursor_clip_modernization{true};
    bool enable_mouse_spi_guard{true};
    bool log_input_hooks{false};
    bool enable_audio_modernization{true};
    bool enable_direct_audio_init_patch{true};
    bool log_audio_init_calls{false};
    bool log_audio_runtime_calls{false};
    bool enable_audio_setter_elision{true};
    bool log_audio_setter_elision{false};
    bool audio_prefer_stereo_fallback{true};
    bool audio_fix_missing_dsp_buffer{true};
    std::uint32_t audio_force_sample_rate_hz{0};
    std::uint32_t audio_force_reported_fmod_version{0};
    std::uint32_t audio_min_dsp_buffer_length{512};
    std::uint32_t audio_min_dsp_buffer_count{4};
    std::uint32_t audio_runtime_log_sample_limit{16};
    bool enable_frame_limiter_patch{true};
    bool enable_hybrid_frame_pacing{true};
    bool enable_frame_pacing_stats{false};
    bool enable_background_frame_limit{true};
    std::uint32_t frame_limit_hz{60};
    std::uint32_t background_frame_limit_hz{15};
    std::uint32_t frame_pacing_spin_threshold_us{1500};
    std::uint32_t frame_pacing_stats_interval_frames{600};
    std::uint32_t frame_pacing_late_threshold_us{500};
    bool enable_fpu_precision_hook{false};
    PrecisionMode precision_mode{PrecisionMode::Unchanged};
};

PrecisionMode parse_precision_mode(std::string_view value);
const char* to_string(PrecisionMode mode);
Settings load(HMODULE module);

}  // namespace mh2modern::config
