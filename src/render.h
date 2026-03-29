#pragma once

#include <cstdint>
#include <string_view>
#include <windows.h>

namespace mh2modern::render {

bool is_supported_d3d9_proc_name(std::string_view proc_name);
std::uint32_t choose_present_interval(bool windowed, std::uint32_t requested,
                                      bool force_fullscreen_interval_one);
std::uint32_t choose_reset_refresh_rate(bool windowed, std::uint32_t requested);
std::uintptr_t choose_reset_device_window(std::uintptr_t requested, std::uintptr_t fallback);
std::uint32_t normalize_device_lost_sleep_ms(std::uint32_t value);
bool is_device_loss_result(std::uint32_t hr);

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);

void set_original_get_proc_address(GetProcAddressFn fn);
void set_render_logging_enabled(bool enabled);

HRESULT install_d3d9_probe(HMODULE game_module, bool log_render_hooks,
                           bool enable_presentation_policy,
                           bool force_fullscreen_interval_one,
                           bool enable_reset_sanitation,
                           bool enable_device_lost_throttle,
                           std::uint32_t device_lost_sleep_ms);
FARPROC WINAPI hooked_get_proc_address(HMODULE module, LPCSTR proc_name);

}  // namespace mh2modern::render
