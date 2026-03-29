#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "affinity.h"
#include "audio.h"
#include "config.h"
#include "crash.h"
#include "framerate.h"
#include "input.h"
#include "platform.h"
#include "render.h"
#include "startup.h"
#include "timing.h"
#include "version.h"

namespace {

bool expect_equal(const char* label, std::uint32_t actual, std::uint32_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected " << expected << " but got " << actual << '\n';
    return false;
}

bool expect_equal(const char* label, mh2modern::config::PrecisionMode actual,
                  mh2modern::config::PrecisionMode expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " precision mode mismatch\n";
    return false;
}

bool expect_equal(const char* label, std::string_view actual, std::string_view expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected \"" << expected << "\" but got \"" << actual << "\"\n";
    return false;
}

}  // namespace

int main() {
    bool ok = true;

    {
        const GUID di8a{0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
        const GUID unknown{};
        ok &= expect_equal(
            "input-di8-interface",
            mh2modern::input::is_direct_input_8_interface(di8a) ? 1U : 0U,
            1U);
        ok &= expect_equal(
            "input-di8-interface-no",
            mh2modern::input::is_direct_input_8_interface(unknown) ? 1U : 0U,
            0U);
    }

    {
        const GUID sys_mouse{
            0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
        const GUID keyboard{
            0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
        ok &= expect_equal(
            "input-wrap-sysmouse",
            mh2modern::input::should_wrap_mouse_guid(sys_mouse) ? 1U : 0U,
            1U);
        ok &= expect_equal(
            "input-wrap-keyboard-no",
            mh2modern::input::should_wrap_mouse_guid(keyboard) ? 1U : 0U,
            0U);
    }

    ok &= expect_equal(
        "input-clamp-positive",
        static_cast<std::uint32_t>(
            mh2modern::input::saturating_long_from_delta(0x7fffffffLL + 42LL)),
        0x7fffffffU);

    ok &= expect_equal(
        "input-clamp-negative",
        static_cast<std::uint32_t>(
            mh2modern::input::saturating_long_from_delta(-0x80000000LL - 42LL)),
        0x80000000U);

    {
        const auto raw = mh2modern::input::choose_mouse_delta(true, 25, -12, 4, 3);
        ok &= expect_equal("input-raw-x", static_cast<std::uint32_t>(raw.x), 25U);
        ok &= expect_equal("input-raw-y", static_cast<std::uint32_t>(raw.y), 0xfffffff4U);
    }

    {
        const auto fallback = mh2modern::input::choose_mouse_delta(true, 0, 0, 7, -9);
        ok &= expect_equal("input-fallback-x", static_cast<std::uint32_t>(fallback.x), 7U);
        ok &= expect_equal("input-fallback-y", static_cast<std::uint32_t>(fallback.y), 0xfffffff7U);
    }

    ok &= expect_equal(
        "input-release-clip-activateapp",
        mh2modern::input::should_release_cursor_clip(WM_ACTIVATEAPP, FALSE) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-release-clip-killfocus",
        mh2modern::input::should_release_cursor_clip(WM_KILLFOCUS, 0U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-release-clip-no",
        mh2modern::input::should_release_cursor_clip(WM_ACTIVATEAPP, TRUE) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "input-restore-clip-activateapp",
        mh2modern::input::should_restore_cursor_clip(WM_ACTIVATEAPP, TRUE) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-restore-clip-setfocus",
        mh2modern::input::should_restore_cursor_clip(WM_SETFOCUS, 0U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-restore-clip-no",
        mh2modern::input::should_restore_cursor_clip(WM_ACTIVATEAPP, FALSE) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "input-focus-loss-release-first",
        mh2modern::input::should_issue_focus_loss_release(false) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-focus-loss-release-suppressed",
        mh2modern::input::should_issue_focus_loss_release(true) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "input-preserve-clip-on-game-release",
        mh2modern::input::should_preserve_saved_cursor_clip_on_game_release(true, true, true) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-clear-clip-on-game-release-no-focus-state",
        mh2modern::input::should_preserve_saved_cursor_clip_on_game_release(false, true, true) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "input-block-spi-setmouse",
        mh2modern::input::is_mutating_mouse_spi_action(SPI_SETMOUSE) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-block-spi-setmousespeed",
        mh2modern::input::is_mutating_mouse_spi_action(SPI_SETMOUSESPEED) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-allow-spi-getmouse",
        mh2modern::input::is_mutating_mouse_spi_action(SPI_GETMOUSE) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "input-block-spi-setfilterkeys",
        mh2modern::input::is_mutating_accessibility_spi_action(SPI_SETFILTERKEYS) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-block-spi-settogglekeys",
        mh2modern::input::is_mutating_accessibility_spi_action(SPI_SETTOGGLEKEYS) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-block-spi-setstickykeys",
        mh2modern::input::is_mutating_accessibility_spi_action(SPI_SETSTICKYKEYS) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "input-allow-spi-getstickykeys",
        mh2modern::input::is_mutating_accessibility_spi_action(SPI_GETSTICKYKEYS) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "crash-chain-filter-null",
        mh2modern::crash::should_chain_exception_filter(
            reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(0x1000), nullptr) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "crash-chain-filter-same",
        mh2modern::crash::should_chain_exception_filter(
            reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(0x1000),
            reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(0x1000)) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "crash-chain-filter-other",
        mh2modern::crash::should_chain_exception_filter(
            reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(0x1000),
            reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(0x2000)) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "crash-chain-runtime-handler-null",
        mh2modern::crash::should_chain_runtime_handler(
            reinterpret_cast<void*>(0x1000), nullptr) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "crash-chain-runtime-handler-same",
        mh2modern::crash::should_chain_runtime_handler(
            reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x1000)) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "crash-chain-runtime-handler-other",
        mh2modern::crash::should_chain_runtime_handler(
            reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000)) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "platform-timer-period-zero",
        mh2modern::platform::normalize_timer_resolution_ms(0U),
        1U);

    ok &= expect_equal(
        "platform-timer-period-pass",
        mh2modern::platform::normalize_timer_resolution_ms(4U),
        4U);

    ok &= expect_equal(
        "platform-timer-period-clamp",
        mh2modern::platform::normalize_timer_resolution_ms(99U),
        16U);

    ok &= expect_equal(
        "platform-preserve-timer-resolution-hit",
        mh2modern::platform::should_preserve_timer_resolution(1U, 1U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "platform-preserve-timer-resolution-no",
        mh2modern::platform::should_preserve_timer_resolution(2U, 1U) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "platform-power-mask-on",
        mh2modern::platform::execution_speed_throttling_mask(true),
        1U);

    ok &= expect_equal(
        "platform-power-mask-off",
        mh2modern::platform::execution_speed_throttling_mask(false),
        0U);

    ok &= expect_equal(
        "platform-power-unsupported-invalid-function",
        mh2modern::platform::is_power_throttling_unsupported_error(1U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "platform-power-unsupported-access-denied",
        mh2modern::platform::is_power_throttling_unsupported_error(5U) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "render-d3d9-proc",
        mh2modern::render::is_supported_d3d9_proc_name("Direct3DCreate9") ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "render-d3d8-proc-no",
        mh2modern::render::is_supported_d3d9_proc_name("Direct3DCreate8") ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "render-device-lost-sleep-default",
        mh2modern::render::normalize_device_lost_sleep_ms(0U),
        1U);

    ok &= expect_equal(
        "render-device-lost-sleep-clamp",
        mh2modern::render::normalize_device_lost_sleep_ms(99U),
        16U);

    ok &= expect_equal(
        "render-device-lost-true",
        mh2modern::render::is_device_loss_result(0x88760868U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "render-device-not-reset-true",
        mh2modern::render::is_device_loss_result(0x88760869U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "render-device-ok-false",
        mh2modern::render::is_device_loss_result(0U) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "render-interval-policy-fullscreen-default",
        mh2modern::render::choose_present_interval(false, 0U, true),
        1U);

    ok &= expect_equal(
        "render-interval-policy-windowed-default",
        mh2modern::render::choose_present_interval(true, 0U, true),
        0U);

    ok &= expect_equal(
        "render-interval-policy-explicit-immediate",
        mh2modern::render::choose_present_interval(false, 0x80000000U, true),
        0x80000000U);

    ok &= expect_equal(
        "render-interval-policy-disabled",
        mh2modern::render::choose_present_interval(false, 0U, false),
        0U);

    ok &= expect_equal(
        "render-reset-refresh-windowed",
        mh2modern::render::choose_reset_refresh_rate(true, 60U),
        0U);

    ok &= expect_equal(
        "render-reset-refresh-fullscreen",
        mh2modern::render::choose_reset_refresh_rate(false, 60U),
        60U);

    ok &= expect_equal(
        "render-reset-device-window-keep",
        static_cast<std::uint32_t>(
            mh2modern::render::choose_reset_device_window(0x1234U, 0x5678U)),
        0x1234U);

    ok &= expect_equal(
        "render-reset-device-window-fallback",
        static_cast<std::uint32_t>(
            mh2modern::render::choose_reset_device_window(0U, 0x5678U)),
        0x5678U);

    ok &= expect_equal(
        "qpc-250ms",
        mh2modern::timing::qpc_delta_to_milliseconds(10'000'000ULL, 500ULL, 2'500'500ULL),
        250U);

    ok &= expect_equal(
        "tickcount-wrap",
        mh2modern::timing::qpc_delta_to_milliseconds(1'000ULL, 0ULL, 4'294'967'297ULL),
        1U);

    ok &= expect_equal(
        "precision-single",
        mh2modern::config::parse_precision_mode("single"),
        mh2modern::config::PrecisionMode::Single24);

    ok &= expect_equal(
        "precision-default",
        mh2modern::config::parse_precision_mode("garbage"),
        mh2modern::config::PrecisionMode::Unchanged);

    ok &= expect_equal(
        "config-render-reset-sanitation-default",
        mh2modern::config::Settings{}.enable_render_reset_sanitation ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "config-render-logging-default",
        mh2modern::config::Settings{}.log_render_hooks ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "config-audio-runtime-logging-default",
        mh2modern::config::Settings{}.log_audio_runtime_calls ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "config-frame-pacing-stats-default",
        mh2modern::config::Settings{}.enable_frame_pacing_stats ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "version-size-ansi",
        mh2modern::version::is_supported_version_info_size(sizeof(OSVERSIONINFOA), false) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "version-size-wide-ex",
        mh2modern::version::is_supported_version_info_size(sizeof(OSVERSIONINFOEXW), true) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "version-size-invalid",
        mh2modern::version::is_supported_version_info_size(12U, false) ? 1U : 0U,
        0U);

    {
        const mh2modern::version::VersionDescriptor reported{6U, 2U, 9200U, VER_PLATFORM_WIN32_NT};
        const mh2modern::version::VersionDescriptor actual{10U, 0U, 26100U, VER_PLATFORM_WIN32_NT};
        const auto chosen = mh2modern::version::choose_version_descriptor(reported, actual);
        ok &= expect_equal("version-upgrade-major", chosen.major, 10U);
        ok &= expect_equal("version-upgrade-minor", chosen.minor, 0U);
        ok &= expect_equal("version-upgrade-build", chosen.build, 26100U);
    }

    {
        const mh2modern::version::VersionDescriptor reported{10U, 0U, 19045U, VER_PLATFORM_WIN32_NT};
        const mh2modern::version::VersionDescriptor actual{6U, 2U, 9200U, VER_PLATFORM_WIN32_NT};
        const auto chosen = mh2modern::version::choose_version_descriptor(reported, actual);
        ok &= expect_equal("version-keep-reported-major", chosen.major, 10U);
        ok &= expect_equal("version-keep-reported-build", chosen.build, 19045U);
    }

    {
        const auto counters =
            mh2modern::version::enqueue_pending_log_counters(0U, 0U, 8U);
        ok &= expect_equal("version-logqueue-count-first", counters.count, 1U);
        ok &= expect_equal("version-logqueue-dropped-first", counters.dropped, 0U);
    }

    {
        const auto counters =
            mh2modern::version::enqueue_pending_log_counters(8U, 0U, 8U);
        ok &= expect_equal("version-logqueue-count-saturated", counters.count, 8U);
        ok &= expect_equal("version-logqueue-dropped-saturated", counters.dropped, 1U);
    }

    {
        const auto initial = mh2modern::startup::EventCounters{};
        const auto opened = mh2modern::startup::observe_event(
            initial, mh2modern::startup::EventKind::FileOpen);
        const auto read = mh2modern::startup::observe_event(
            opened, mh2modern::startup::EventKind::Read, 4096U);
        const auto loaded = mh2modern::startup::observe_event(
            read, mh2modern::startup::EventKind::DllLoad);
        const auto windowed = mh2modern::startup::observe_event(
            loaded, mh2modern::startup::EventKind::WindowCreate);
        ok &= expect_equal("startup-count-file", windowed.file_opens, 1U);
        ok &= expect_equal("startup-count-read", windowed.read_calls, 1U);
        ok &= expect_equal("startup-count-read-bytes",
                           static_cast<std::uint32_t>(windowed.read_bytes), 4096U);
        ok &= expect_equal("startup-count-dll", windowed.dll_loads, 1U);
        ok &= expect_equal("startup-count-window", windowed.window_creates, 1U);
    }

    ok &= expect_equal(
        "startup-max-duration",
        mh2modern::startup::max_duration_ms(2U, 9U),
        9U);

    {
        const auto counters =
            mh2modern::startup::enqueue_pending_detail_counters(0U, 0U, 4U);
        ok &= expect_equal("startup-detailqueue-count-first", counters.count, 1U);
        ok &= expect_equal("startup-detailqueue-dropped-first", counters.dropped, 0U);
    }

    {
        const auto counters =
            mh2modern::startup::enqueue_pending_detail_counters(4U, 1U, 4U);
        ok &= expect_equal("startup-detailqueue-count-saturated", counters.count, 4U);
        ok &= expect_equal("startup-detailqueue-dropped-saturated", counters.dropped, 2U);
    }

    ok &= expect_equal(
        "startup-detail-threshold-hit",
        mh2modern::startup::should_log_detail_event(true, 3U, 3U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "startup-detail-threshold-miss",
        mh2modern::startup::should_log_detail_event(true, 3U, 2U) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "startup-filekind-shader",
        static_cast<std::uint32_t>(mh2modern::startup::classify_file_stack_path(".\\shaders\\PostProcess.fxo")),
        static_cast<std::uint32_t>(mh2modern::startup::FileStackKind::ShaderBlob));

    ok &= expect_equal(
        "startup-filekind-gxt",
        static_cast<std::uint32_t>(mh2modern::startup::classify_file_stack_path("GLOBAL/GAME.GXT")),
        static_cast<std::uint32_t>(mh2modern::startup::FileStackKind::GameText));

    ok &= expect_equal(
        "startup-filekind-globaltex",
        static_cast<std::uint32_t>(mh2modern::startup::classify_file_stack_path("global/gmodelspc.tex")),
        static_cast<std::uint32_t>(mh2modern::startup::FileStackKind::GlobalTexture));

    ok &= expect_equal(
        "startup-filekind-none",
        static_cast<std::uint32_t>(mh2modern::startup::classify_file_stack_path("global/pictures/legal_fr.tex")),
        static_cast<std::uint32_t>(mh2modern::startup::FileStackKind::None));

    ok &= expect_equal(
        "frame-limit-60hz",
        mh2modern::framerate::frame_limit_hz_to_interval_us(60U),
        16667U);

    ok &= expect_equal(
        "frame-limit-144hz",
        mh2modern::framerate::frame_limit_hz_to_interval_us(144U),
        6944U);

    ok &= expect_equal(
        "frame-limit-clamp-low",
        mh2modern::framerate::clamp_frame_limit_hz(5U),
        15U);

    ok &= expect_equal(
        "frame-limit-clamp-high",
        mh2modern::framerate::clamp_frame_limit_hz(1000U),
        360U);

    ok &= expect_equal(
        "frame-pacing-sleep-60hz",
        mh2modern::framerate::compute_sleep_milliseconds(16'667U, 1'500U),
        15U);

    ok &= expect_equal(
        "frame-pacing-sleep-tight",
        mh2modern::framerate::compute_sleep_milliseconds(2'200U, 1'500U),
        0U);

    ok &= expect_equal(
        "frame-interval-foreground",
        mh2modern::framerate::select_frame_interval_us(true, true, 16'667U, 66'667U),
        16'667U);

    ok &= expect_equal(
        "frame-interval-background",
        mh2modern::framerate::select_frame_interval_us(false, true, 16'667U, 66'667U),
        66'667U);

    ok &= expect_equal(
        "frame-interval-background-disabled",
        mh2modern::framerate::select_frame_interval_us(false, false, 16'667U, 66'667U),
        16'667U);

    ok &= expect_equal(
        "audio-force-sample-rate",
        mh2modern::audio::resolve_sample_rate_hz(22'050U, 48'000U),
        48'000U);

    ok &= expect_equal(
        "audio-force-reported-version",
        mh2modern::audio::resolve_reported_fmod_version(0x00040805U, 0x00040910U),
        0x00040910U);

    ok &= expect_equal(
        "audio-keep-reported-version",
        mh2modern::audio::resolve_reported_fmod_version(0x00040805U, 0U),
        0x00040805U);

    ok &= expect_equal(
        "audio-keep-sample-rate",
        mh2modern::audio::resolve_sample_rate_hz(44'100U, 0U),
        44'100U);

    ok &= expect_equal(
        "audio-clamp-buffer-length",
        mh2modern::audio::normalize_dsp_buffer_length(256U, 512U),
        512U);

    ok &= expect_equal(
        "audio-keep-buffer-length",
        mh2modern::audio::normalize_dsp_buffer_length(1024U, 512U),
        1024U);

    ok &= expect_equal(
        "audio-clamp-buffer-count",
        mh2modern::audio::normalize_dsp_buffer_count(2U, 4U),
        4U);

    ok &= expect_equal(
        "audio-keep-buffer-count",
        mh2modern::audio::normalize_dsp_buffer_count(6U, 4U),
        6U);

    ok &= expect_equal(
        "audio-runtime-log-first",
        mh2modern::audio::should_log_bounded_audio_sample(0U, 4U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "audio-runtime-log-last-allowed",
        mh2modern::audio::should_log_bounded_audio_sample(3U, 4U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "audio-runtime-log-suppressed",
        mh2modern::audio::should_log_bounded_audio_sample(4U, 4U) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "audio-optional-bool-null",
        static_cast<std::uint32_t>(mh2modern::audio::describe_optional_fmod_bool(nullptr)),
        0xffffffffU);

    {
        const bool starving = false;
        ok &= expect_equal(
            "audio-optional-bool-false",
            static_cast<std::uint32_t>(mh2modern::audio::describe_optional_fmod_bool(&starving)),
            0U);
    }

    {
        const bool starving = true;
        ok &= expect_equal(
            "audio-optional-bool-true",
            static_cast<std::uint32_t>(mh2modern::audio::describe_optional_fmod_bool(&starving)),
            1U);
    }

    ok &= expect_equal(
        "audio-channel-index-free",
        mh2modern::audio::describe_fmod_channel_index(-1),
        "free");

    ok &= expect_equal(
        "audio-channel-index-bus",
        mh2modern::audio::describe_fmod_channel_index(3),
        "3");

    ok &= expect_equal(
        "audio-state-value-zero",
        mh2modern::audio::format_fmod_state_value(0U),
        "0x0");

    ok &= expect_equal(
        "audio-state-value-hex",
        mh2modern::audio::format_fmod_state_value(0x2aU),
        "0x2a");

    ok &= expect_equal(
        "audio-known-redundant-setter-callsite",
        mh2modern::audio::is_known_redundant_audio_setter_callsite(0xCE322U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "audio-known-redundant-setter-callsite-no",
        mh2modern::audio::is_known_redundant_audio_setter_callsite(0xCE26AU) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "audio-elide-redundant-value",
        mh2modern::audio::should_elide_redundant_audio_value(true, 0x12345678U, 0x12345678U) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "audio-keep-distinct-value",
        mh2modern::audio::should_elide_redundant_audio_value(true, 0x12345678U, 0x12345679U) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "audio-keep-uncached-value",
        mh2modern::audio::should_elide_redundant_audio_value(false, 0x12345678U, 0x12345678U) ? 1U : 0U,
        0U);

    {
        const auto selection = mh2modern::audio::choose_speaker_mode_request(2, true);
        ok &= expect_equal(
            "audio-speaker-selection-effective",
            static_cast<std::uint32_t>(selection.effective_mode),
            3U);
        ok &= expect_equal(
            "audio-speaker-selection-changed",
            selection.changed ? 1U : 0U,
            1U);
        ok &= expect_equal(
            "audio-speaker-selection-consumed",
            selection.consumed_pending_force_stereo ? 1U : 0U,
            1U);
    }

    {
        const auto selection = mh2modern::audio::choose_speaker_mode_request(3, true);
        ok &= expect_equal(
            "audio-speaker-selection-keep-stereo",
            static_cast<std::uint32_t>(selection.effective_mode),
            3U);
        ok &= expect_equal(
            "audio-speaker-selection-unchanged",
            selection.changed ? 1U : 0U,
            0U);
        ok &= expect_equal(
            "audio-speaker-selection-consumed-stereo",
            selection.consumed_pending_force_stereo ? 1U : 0U,
            1U);
    }

    ok &= expect_equal(
        "audio-format-version",
        mh2modern::audio::format_fmod_version(0x00040805U),
        "4.8.5");

    ok &= expect_equal(
        "audio-force-stereo-fallback",
        mh2modern::audio::should_force_stereo_fallback(0, 0U, 2, true) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "audio-no-stereo-fallback-caps",
        mh2modern::audio::should_force_stereo_fallback(0, 1U, 2, true) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "audio-no-stereo-fallback-disabled",
        mh2modern::audio::should_force_stereo_fallback(0, 0U, 2, false) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "audio-inject-dsp-missing",
        mh2modern::audio::should_inject_missing_dsp_buffer_fix(0, 0U, true) ? 1U : 0U,
        1U);

    ok &= expect_equal(
        "audio-no-inject-dsp-cap-present",
        mh2modern::audio::should_inject_missing_dsp_buffer_fix(0, 2U, true) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "audio-no-inject-dsp-disabled",
        mh2modern::audio::should_inject_missing_dsp_buffer_fix(0, 0U, false) ? 1U : 0U,
        0U);

    ok &= expect_equal(
        "audio-rva-basic",
        mh2modern::audio::relative_virtual_address(0x400000U, 0x200000U, 0x401234U),
        0x1234U);

    ok &= expect_equal(
        "audio-rva-outside-module-low",
        mh2modern::audio::relative_virtual_address(0x400000U, 0x200000U, 0x3ffff0U),
        0U);

    ok &= expect_equal(
        "audio-rva-outside-module-high",
        mh2modern::audio::relative_virtual_address(0x400000U, 0x200000U, 0x700000U),
        0U);

    ok &= expect_equal(
        "affinity-keep-process-mask",
        static_cast<std::uint32_t>(mh2modern::affinity::choose_affinity_mask(0x1U, 0xFU)),
        0xFU);

    ok &= expect_equal(
        "affinity-keep-single-core-system",
        static_cast<std::uint32_t>(mh2modern::affinity::choose_affinity_mask(0x1U, 0x1U)),
        0x1U);

    ok &= expect_equal(
        "affinity-keep-nonstartup-mask",
        static_cast<std::uint32_t>(mh2modern::affinity::choose_affinity_mask(0x4U, 0xFU)),
        0x4U);

    mh2modern::framerate::PacingStatsAccumulator stats(3U, 500U);
    stats.record_frame(15'100U, 16'700U, 1U, 16'667U);
    stats.record_frame(15'050U, 16'667U, 1U, 16'667U);
    stats.record_frame(16'300U, 17'400U, 0U, 16'667U);
    ok &= expect_equal("frame-stats-ready", stats.should_flush() ? 1U : 0U, 1U);
    const auto snapshot = stats.snapshot_and_reset();
    ok &= expect_equal("frame-stats-count", snapshot.frame_count, 3U);
    ok &= expect_equal("frame-stats-slept", snapshot.slept_frames, 2U);
    ok &= expect_equal("frame-stats-late", snapshot.late_frames, 1U);
    ok &= expect_equal("frame-stats-min", snapshot.min_final_elapsed_us, 16'667U);
    ok &= expect_equal("frame-stats-max", snapshot.max_final_elapsed_us, 17'400U);
    ok &= expect_equal("frame-stats-max-overshoot", snapshot.max_overshoot_us, 733U);
    ok &= expect_equal("frame-stats-reset", stats.should_flush() ? 1U : 0U, 0U);

    if (!ok) {
        return 1;
    }

    std::cout << "All MH2Modern tests passed.\n";
    return 0;
}
