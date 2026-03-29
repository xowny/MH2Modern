#include "platform.h"

#include "logger.h"

#include <mmsystem.h>
#include <sstream>
#include <string_view>
#include <windows.h>

namespace mh2modern::platform {
namespace {

constexpr std::uint32_t kMinTimerResolutionMs = 1;
constexpr std::uint32_t kMaxTimerResolutionMs = 16;
constexpr ULONG kProcessPowerThrottlingCurrentVersion = 1;
constexpr ULONG kProcessPowerThrottlingExecutionSpeed = 0x1;
constexpr int kProcessPowerThrottlingInformationClass = 4;

struct PowerThrottlingStateCompat {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
};

using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
using SetProcessDPIAwareFn = BOOL(WINAPI*)();
using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);

bool g_log_platform_modernization = false;
TimeBeginPeriodFn g_original_time_begin_period = nullptr;
TimeEndPeriodFn g_original_time_end_period = nullptr;
std::uint32_t g_protected_timer_resolution_ms = 0;

void log_info(std::string_view message) {
    if (g_log_platform_modernization) {
        logger::info(message);
    }
}

void log_error(std::string_view message) {
    if (g_log_platform_modernization) {
        logger::error(message);
    }
}

void log_last_error(const char* label) {
    if (!g_log_platform_modernization) {
        return;
    }

    std::ostringstream oss;
    oss << label << " failed, gle=" << GetLastError();
    logger::error(oss.str());
}

bool install_dpi_awareness() {
    auto* user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        user32 = LoadLibraryW(L"user32.dll");
    }
    if (user32 == nullptr) {
        log_last_error("LoadLibraryW(user32.dll)");
        return false;
    }

    if (const auto set_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
        if (set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            log_info("Enabled Per-Monitor V2 DPI awareness");
            return true;
        }

        const auto error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            log_info("Per-Monitor V2 DPI awareness already configured by the process");
            return true;
        }

        std::ostringstream oss;
        oss << "SetProcessDpiAwarenessContext failed, gle=" << error;
        log_error(oss.str());
    }

    if (const auto set_legacy =
            reinterpret_cast<SetProcessDPIAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"))) {
        if (set_legacy()) {
            log_info("Enabled legacy system DPI awareness fallback");
            return true;
        }

        const auto error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            log_info("Legacy DPI awareness already configured by the process");
            return true;
        }

        std::ostringstream oss;
        oss << "SetProcessDPIAware failed, gle=" << error;
        log_error(oss.str());
    }

    return false;
}

bool install_timer_resolution(std::uint32_t timer_resolution_ms) {
    auto* winmm = GetModuleHandleW(L"winmm.dll");
    if (winmm == nullptr) {
        winmm = LoadLibraryW(L"winmm.dll");
    }
    if (winmm == nullptr) {
        log_last_error("LoadLibraryW(winmm.dll)");
        return false;
    }

    const auto time_begin_period =
        reinterpret_cast<TimeBeginPeriodFn>(GetProcAddress(winmm, "timeBeginPeriod"));
    if (time_begin_period == nullptr) {
        log_error("timeBeginPeriod export was not found");
        return false;
    }

    const auto normalized = normalize_timer_resolution_ms(timer_resolution_ms);
    const auto result = time_begin_period(normalized);
    if (result != 0U) {
        std::ostringstream oss;
        oss << "timeBeginPeriod(" << normalized << ") failed, mmresult=" << result;
        log_error(oss.str());
        return false;
    }

    std::ostringstream oss;
    oss << "Requested " << normalized << " ms timer resolution";
    log_info(oss.str());
    g_protected_timer_resolution_ms = normalized;
    return true;
}

bool install_power_throttling_fix() {
    auto* kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        return false;
    }

    const auto set_process_information = reinterpret_cast<SetProcessInformationFn>(
        GetProcAddress(kernel32, "SetProcessInformation"));
    if (set_process_information == nullptr) {
        log_info("SetProcessInformation is unavailable on this OS");
        return false;
    }

    PowerThrottlingStateCompat state{};
    state.Version = kProcessPowerThrottlingCurrentVersion;
    state.ControlMask = execution_speed_throttling_mask(true);
    state.StateMask = 0;
    if (!set_process_information(
            GetCurrentProcess(), kProcessPowerThrottlingInformationClass, &state, sizeof(state))) {
        const auto error = static_cast<std::uint32_t>(GetLastError());
        if (is_power_throttling_unsupported_error(error)) {
            log_info("Process power throttling control is unsupported on this platform");
            return false;
        }

        std::ostringstream oss;
        oss << "SetProcessInformation(ProcessPowerThrottling) failed, gle=" << error;
        log_error(oss.str());
        return false;
    }

    log_info("Disabled process execution-speed power throttling");
    return true;
}

}  // namespace

std::uint32_t normalize_timer_resolution_ms(std::uint32_t requested) {
    if (requested < kMinTimerResolutionMs) {
        return kMinTimerResolutionMs;
    }
    if (requested > kMaxTimerResolutionMs) {
        return kMaxTimerResolutionMs;
    }
    return requested;
}

bool should_preserve_timer_resolution(std::uint32_t requested_period,
                                      std::uint32_t protected_period) {
    return protected_period != 0 && requested_period == protected_period;
}

std::uint32_t execution_speed_throttling_mask(bool disable_power_throttling) {
    return disable_power_throttling ? kProcessPowerThrottlingExecutionSpeed : 0U;
}

bool is_power_throttling_unsupported_error(std::uint32_t error_code) {
    return error_code == ERROR_INVALID_FUNCTION || error_code == ERROR_NOT_SUPPORTED;
}

void set_original_time_begin_period(TimeBeginPeriodFn fn) {
    g_original_time_begin_period = fn;
}

void set_original_time_end_period(TimeEndPeriodFn fn) {
    g_original_time_end_period = fn;
}

UINT WINAPI hooked_time_begin_period(UINT period) {
    if (g_original_time_begin_period == nullptr) {
        return TIMERR_NOCANDO;
    }

    const auto result = g_original_time_begin_period(period);
    if (g_log_platform_modernization) {
        std::ostringstream oss;
        oss << "timeBeginPeriod(" << period << ") -> mmresult=" << result;
        logger::info(oss.str());
    }
    return result;
}

UINT WINAPI hooked_time_end_period(UINT period) {
    if (g_original_time_end_period == nullptr) {
        return TIMERR_NOCANDO;
    }

    if (should_preserve_timer_resolution(period, g_protected_timer_resolution_ms)) {
        if (g_log_platform_modernization) {
            std::ostringstream oss;
            oss << "Blocked timeEndPeriod(" << period
                << ") to preserve MH2Modern timer resolution";
            logger::info(oss.str());
        }
        return TIMERR_NOERROR;
    }

    const auto result = g_original_time_end_period(period);
    if (g_log_platform_modernization) {
        std::ostringstream oss;
        oss << "timeEndPeriod(" << period << ") -> mmresult=" << result;
        logger::info(oss.str());
    }
    return result;
}

void install(bool enable_dpi_awareness, bool enable_timer_resolution,
             std::uint32_t timer_resolution_ms, bool disable_power_throttling,
             bool log_platform_modernization) {
    g_log_platform_modernization = log_platform_modernization;

    if (enable_dpi_awareness) {
        install_dpi_awareness();
    }

    if (enable_timer_resolution) {
        install_timer_resolution(timer_resolution_ms);
    }

    if (disable_power_throttling) {
        install_power_throttling_fix();
    }
}

}  // namespace mh2modern::platform
