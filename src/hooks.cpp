#include "hooks.h"

#include "affinity.h"
#include "audio.h"
#include "config.h"
#include "crash.h"
#include "framerate.h"
#include "iat_hook.h"
#include "input.h"
#include "logger.h"
#include "platform.h"
#include "render.h"
#include "startup.h"
#include "timing.h"
#include "version.h"

#include <float.h>

#include <sstream>

namespace mh2modern::hooks {
namespace {

void apply_precision_mode(config::PrecisionMode mode) {
    unsigned int control_word = 0;
    switch (mode) {
    case config::PrecisionMode::Single24:
        control_word = _PC_24;
        break;
    case config::PrecisionMode::Double53:
        control_word = _PC_53;
        break;
    case config::PrecisionMode::Extended64:
        control_word = _PC_64;
        break;
    case config::PrecisionMode::Unchanged:
    default:
        return;
    }

    _controlfp(control_word, _MCW_PC);

    std::ostringstream oss;
    oss << "Applied main-thread FPU precision mode: " << config::to_string(mode)
        << " (startup only; game code may override it later)";
    logger::info(oss.str());
}

void install_import_hook(HMODULE game_module, const char* imported_module, const char* proc_name,
                         void* replacement, void** original_out) {
    if (!iat_hook::patch_import(game_module, imported_module, proc_name, replacement, original_out)) {
        std::ostringstream oss;
        oss << "Failed to patch " << imported_module << "!" << proc_name;
        logger::error(oss.str());
        return;
    }

    std::ostringstream oss;
    oss << "Patched " << imported_module << "!" << proc_name;
    logger::info(oss.str());
}

}  // namespace

void install_all(HMODULE plugin_module, DWORD main_thread_id) {
    logger::init(plugin_module);
    logger::info("MH2Modern bootstrap started");

    const auto settings = config::load(plugin_module);
    const auto game_module = GetModuleHandleW(nullptr);
    if (game_module == nullptr) {
        logger::error("Failed to resolve Manhunt2.exe module handle");
        return;
    }

    platform::install(
        settings.enable_dpi_awareness, settings.enable_timer_resolution,
        settings.timer_resolution_ms, settings.disable_power_throttling,
        settings.log_platform_modernization);

    timing::initialize();
    timing::set_main_thread_id(main_thread_id);
    timing::set_sleep_spin_fix_enabled(settings.enable_sleep_spin_fix);
    timing::set_timer_logging_enabled(settings.log_timer_calls);
    input::set_raw_mouse_enabled(settings.enable_raw_mouse_input);
    input::set_input_logging_enabled(settings.log_input_hooks);
    input::set_cursor_clip_modernization_enabled(settings.enable_cursor_clip_modernization);
    input::set_mouse_spi_guard_enabled(settings.enable_mouse_spi_guard);

    if (settings.enable_crash_dumps) {
        crash::install(plugin_module);
    }

    startup::finalize_install(
        game_module, settings.log_startup_details, settings.startup_slow_io_threshold_ms);
    version::finalize_install(game_module, settings.log_version_calls);

    if (settings.enable_affinity_modernization) {
        affinity::install_affinity_patch(game_module, settings.log_affinity_patch);
    }

    if (settings.enable_audio_modernization) {
        audio::install_fmod_hooks(
            game_module, settings.enable_direct_audio_init_patch, settings.log_audio_init_calls,
            settings.log_audio_runtime_calls,
            settings.enable_audio_setter_elision, settings.log_audio_setter_elision,
            settings.audio_prefer_stereo_fallback, settings.audio_fix_missing_dsp_buffer,
            settings.audio_force_sample_rate_hz, settings.audio_force_reported_fmod_version,
            settings.audio_min_dsp_buffer_length, settings.audio_min_dsp_buffer_count,
            settings.audio_runtime_log_sample_limit);
    }

    if (settings.enable_frame_limiter_patch) {
        framerate::install_frame_limit_patch(
            game_module, settings.frame_limit_hz, settings.enable_hybrid_frame_pacing,
            settings.frame_pacing_spin_threshold_us, settings.enable_frame_pacing_stats,
            settings.frame_pacing_stats_interval_frames, settings.frame_pacing_late_threshold_us,
            settings.enable_background_frame_limit, settings.background_frame_limit_hz);
    }

    if (settings.enable_d3d9_probe) {
        render::install_d3d9_probe(
            game_module, settings.log_render_hooks, settings.enable_render_presentation_policy,
            settings.render_force_fullscreen_interval_one,
            settings.enable_render_reset_sanitation,
            settings.enable_render_device_lost_throttle, settings.render_device_lost_sleep_ms);
    }

    if (settings.enable_raw_mouse_input) {
        void* original_direct_input8_create = nullptr;
        install_import_hook(
            game_module, "DINPUT8.dll", "DirectInput8Create",
            reinterpret_cast<void*>(&input::hooked_direct_input8_create),
            &original_direct_input8_create);
        input::set_original_direct_input8_create(
            reinterpret_cast<input::DirectInput8CreateFn>(original_direct_input8_create));
    }

    if (settings.enable_cursor_clip_modernization) {
        void* original_clip_cursor = nullptr;
        install_import_hook(
            game_module, "USER32.dll", "ClipCursor",
            reinterpret_cast<void*>(&input::hooked_clip_cursor), &original_clip_cursor);
        input::set_original_clip_cursor(reinterpret_cast<input::ClipCursorFn>(original_clip_cursor));
    }

    if (settings.enable_mouse_spi_guard) {
        void* original_system_parameters_info_a = nullptr;
        install_import_hook(
            game_module, "USER32.dll", "SystemParametersInfoA",
            reinterpret_cast<void*>(&input::hooked_system_parameters_info_a),
            &original_system_parameters_info_a);
        input::set_original_system_parameters_info_a(
            reinterpret_cast<input::SystemParametersInfoAFn>(original_system_parameters_info_a));

        void* original_system_parameters_info_w = nullptr;
        install_import_hook(
            game_module, "USER32.dll", "SystemParametersInfoW",
            reinterpret_cast<void*>(&input::hooked_system_parameters_info_w),
            &original_system_parameters_info_w);
        input::set_original_system_parameters_info_w(
            reinterpret_cast<input::SystemParametersInfoWFn>(original_system_parameters_info_w));
    }

    if (settings.enable_timer_resolution) {
        void* original_time_begin_period = nullptr;
        install_import_hook(
            game_module, "WINMM.dll", "timeBeginPeriod",
            reinterpret_cast<void*>(&platform::hooked_time_begin_period),
            &original_time_begin_period);
        platform::set_original_time_begin_period(
            reinterpret_cast<platform::TimeBeginPeriodFn>(original_time_begin_period));

        void* original_time_end_period = nullptr;
        install_import_hook(
            game_module, "WINMM.dll", "timeEndPeriod",
            reinterpret_cast<void*>(&platform::hooked_time_end_period),
            &original_time_end_period);
        platform::set_original_time_end_period(
            reinterpret_cast<platform::TimeEndPeriodFn>(original_time_end_period));
    }

    if (settings.enable_get_tick_count_hook) {
        void* original_get_tick_count = nullptr;
        install_import_hook(
            game_module, "KERNEL32.dll", "GetTickCount",
            reinterpret_cast<void*>(&timing::hooked_get_tick_count), &original_get_tick_count);
        timing::set_original_get_tick_count(
            reinterpret_cast<timing::GetTickCountFn>(original_get_tick_count));
    }

    if (settings.enable_crash_dumps) {
        void* original_set_unhandled_exception_filter = nullptr;
        install_import_hook(
            game_module, "KERNEL32.dll", "SetUnhandledExceptionFilter",
            reinterpret_cast<void*>(&crash::hooked_set_unhandled_exception_filter),
            &original_set_unhandled_exception_filter);
        crash::set_original_set_unhandled_exception_filter(
            reinterpret_cast<crash::SetUnhandledExceptionFilterFn>(
                original_set_unhandled_exception_filter));
    }

    if (settings.enable_sleep_spin_fix) {
        void* original_sleep = nullptr;
        install_import_hook(
            game_module, "KERNEL32.dll", "Sleep", reinterpret_cast<void*>(&timing::hooked_sleep),
            &original_sleep);
        timing::set_original_sleep(reinterpret_cast<timing::SleepFn>(original_sleep));
    }

    if (settings.enable_fpu_precision_hook) {
        apply_precision_mode(settings.precision_mode);
    } else if (settings.precision_mode != config::PrecisionMode::Unchanged) {
        logger::info("PrecisionMode is configured but EnableFpuPrecisionHook is false; skipping");
    }

    logger::info("MH2Modern install completed");
}

}  // namespace mh2modern::hooks
