#include "config.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <string>

namespace mh2modern::config {
namespace {

std::filesystem::path ini_path_for_module(HMODULE module) {
    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(module, module_path, MAX_PATH);
    return std::filesystem::path(module_path).replace_filename(L"MH2Modern.ini");
}

std::wstring read_ini_string(const std::filesystem::path& ini_path, const wchar_t* key,
                             const wchar_t* fallback) {
    std::array<wchar_t, 64> buffer{};
    GetPrivateProfileStringW(
        L"MH2Modern", key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), ini_path.c_str());
    return std::wstring(buffer.data());
}

bool read_ini_bool(const std::filesystem::path& ini_path, const wchar_t* key, bool fallback) {
    return GetPrivateProfileIntW(L"MH2Modern", key, fallback ? 1 : 0, ini_path.c_str()) != 0;
}

std::uint32_t read_ini_uint(const std::filesystem::path& ini_path, const wchar_t* key,
                            std::uint32_t fallback) {
    return static_cast<std::uint32_t>(
        GetPrivateProfileIntW(L"MH2Modern", key, static_cast<int>(fallback), ini_path.c_str()));
}

std::uint32_t read_ini_u32_flexible(const std::filesystem::path& ini_path, const wchar_t* key,
                                    std::uint32_t fallback) {
    const auto raw = read_ini_string(ini_path, key, L"");
    if (raw.empty()) {
        return fallback;
    }

    wchar_t* end = nullptr;
    const auto parsed = std::wcstoul(raw.c_str(), &end, 0);
    if (end == raw.c_str()) {
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

std::string narrow(std::wstring value) {
    std::string result;
    result.reserve(value.size());
    std::transform(value.begin(), value.end(), std::back_inserter(result), [](wchar_t ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });
    return result;
}

}  // namespace

PrecisionMode parse_precision_mode(std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "single" || normalized == "single24" || normalized == "24") {
        return PrecisionMode::Single24;
    }
    if (normalized == "double" || normalized == "double53" || normalized == "53") {
        return PrecisionMode::Double53;
    }
    if (normalized == "extended" || normalized == "extended64" || normalized == "64") {
        return PrecisionMode::Extended64;
    }
    return PrecisionMode::Unchanged;
}

const char* to_string(PrecisionMode mode) {
    switch (mode) {
    case PrecisionMode::Single24:
        return "single24";
    case PrecisionMode::Double53:
        return "double53";
    case PrecisionMode::Extended64:
        return "extended64";
    case PrecisionMode::Unchanged:
    default:
        return "unchanged";
    }
}

Settings load(HMODULE module) {
    const auto ini_path = ini_path_for_module(module);

    Settings settings{};
    settings.enable_get_tick_count_hook = read_ini_bool(ini_path, L"EnableGetTickCountHook", false);
    settings.enable_sleep_spin_fix = read_ini_bool(ini_path, L"EnableSleepSpinFix", false);
    settings.enable_crash_dumps = read_ini_bool(ini_path, L"EnableCrashDumps", true);
    settings.log_timer_calls = read_ini_bool(ini_path, L"LogTimerCalls", false);
    settings.log_version_calls = read_ini_bool(ini_path, L"LogVersionCalls", false);
    settings.log_startup_details = read_ini_bool(ini_path, L"LogStartupDetails", false);
    settings.startup_slow_io_threshold_ms =
        read_ini_uint(ini_path, L"StartupSlowIoThresholdMs", 3);
    settings.enable_affinity_modernization =
        read_ini_bool(ini_path, L"EnableAffinityModernization", true);
    settings.log_affinity_patch = read_ini_bool(ini_path, L"LogAffinityPatch", false);
    settings.enable_dpi_awareness = read_ini_bool(ini_path, L"EnableDpiAwareness", true);
    settings.enable_timer_resolution = read_ini_bool(ini_path, L"EnableTimerResolution", true);
    settings.timer_resolution_ms = read_ini_uint(ini_path, L"TimerResolutionMs", 1);
    settings.disable_power_throttling =
        read_ini_bool(ini_path, L"DisablePowerThrottling", true);
    settings.log_platform_modernization =
        read_ini_bool(ini_path, L"LogPlatformModernization", false);
    settings.enable_d3d9_probe = read_ini_bool(ini_path, L"EnableD3D9Probe", true);
    settings.log_render_hooks = read_ini_bool(ini_path, L"LogRenderHooks", false);
    settings.enable_render_presentation_policy =
        read_ini_bool(ini_path, L"EnableRenderPresentationPolicy", true);
    settings.render_force_fullscreen_interval_one =
        read_ini_bool(ini_path, L"RenderForceFullscreenIntervalOne", true);
    settings.enable_render_reset_sanitation =
        read_ini_bool(ini_path, L"EnableRenderResetSanitation", true);
    settings.enable_render_device_lost_throttle =
        read_ini_bool(ini_path, L"EnableRenderDeviceLostThrottle", true);
    settings.render_device_lost_sleep_ms =
        read_ini_uint(ini_path, L"RenderDeviceLostSleepMs", 1);
    settings.enable_raw_mouse_input = read_ini_bool(ini_path, L"EnableRawMouseInput", true);
    settings.enable_cursor_clip_modernization =
        read_ini_bool(ini_path, L"EnableCursorClipModernization", true);
    settings.enable_mouse_spi_guard =
        read_ini_bool(ini_path, L"EnableMouseSpiGuard", true);
    settings.log_input_hooks = read_ini_bool(ini_path, L"LogInputHooks", false);
    settings.enable_audio_modernization = read_ini_bool(ini_path, L"EnableAudioModernization", true);
    settings.enable_direct_audio_init_patch =
        read_ini_bool(ini_path, L"EnableDirectAudioInitPatch", true);
    settings.log_audio_init_calls = read_ini_bool(ini_path, L"LogAudioInitCalls", false);
    settings.log_audio_runtime_calls = read_ini_bool(ini_path, L"LogAudioRuntimeCalls", false);
    settings.enable_audio_setter_elision =
        read_ini_bool(ini_path, L"EnableAudioSetterElision", true);
    settings.log_audio_setter_elision =
        read_ini_bool(ini_path, L"LogAudioSetterElision", false);
    settings.audio_prefer_stereo_fallback =
        read_ini_bool(ini_path, L"AudioPreferStereoFallback", true);
    settings.audio_fix_missing_dsp_buffer =
        read_ini_bool(ini_path, L"AudioFixMissingDspBuffer", true);
    settings.audio_force_sample_rate_hz = read_ini_uint(ini_path, L"AudioForceSampleRateHz", 0);
    settings.audio_force_reported_fmod_version =
        read_ini_u32_flexible(ini_path, L"AudioForceReportedFmodVersion", 0);
    settings.audio_min_dsp_buffer_length =
        read_ini_uint(ini_path, L"AudioMinDspBufferLength", 512);
    settings.audio_min_dsp_buffer_count = read_ini_uint(ini_path, L"AudioMinDspBufferCount", 4);
    settings.audio_runtime_log_sample_limit =
        read_ini_uint(ini_path, L"AudioRuntimeLogSampleLimit", 16);
    settings.enable_frame_limiter_patch = read_ini_bool(ini_path, L"EnableFrameLimiterPatch", true);
    settings.enable_hybrid_frame_pacing = read_ini_bool(ini_path, L"EnableHybridFramePacing", true);
    settings.enable_frame_pacing_stats = read_ini_bool(ini_path, L"EnableFramePacingStats", false);
    settings.enable_background_frame_limit =
        read_ini_bool(ini_path, L"EnableBackgroundFrameLimit", true);
    settings.frame_limit_hz = read_ini_uint(ini_path, L"FrameLimitHz", 60);
    settings.background_frame_limit_hz =
        read_ini_uint(ini_path, L"BackgroundFrameLimitHz", 15);
    settings.frame_pacing_spin_threshold_us =
        read_ini_uint(ini_path, L"FramePacingSpinThresholdUs", 1500);
    settings.frame_pacing_stats_interval_frames =
        read_ini_uint(ini_path, L"FramePacingStatsIntervalFrames", 600);
    settings.frame_pacing_late_threshold_us =
        read_ini_uint(ini_path, L"FramePacingLateThresholdUs", 500);
    settings.enable_fpu_precision_hook = read_ini_bool(ini_path, L"EnableFpuPrecisionHook", false);
    settings.precision_mode =
        parse_precision_mode(narrow(read_ini_string(ini_path, L"PrecisionMode", L"unchanged")));
    return settings;
}

}  // namespace mh2modern::config
