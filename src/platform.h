#pragma once

#include <cstdint>
#include <windows.h>

namespace mh2modern::platform {

using TimeBeginPeriodFn = UINT(WINAPI*)(UINT);
using TimeEndPeriodFn = UINT(WINAPI*)(UINT);

std::uint32_t normalize_timer_resolution_ms(std::uint32_t requested);
bool should_preserve_timer_resolution(std::uint32_t requested_period,
                                      std::uint32_t protected_period);
std::uint32_t execution_speed_throttling_mask(bool disable_power_throttling);
bool is_power_throttling_unsupported_error(std::uint32_t error_code);
void set_original_time_begin_period(TimeBeginPeriodFn fn);
void set_original_time_end_period(TimeEndPeriodFn fn);
UINT WINAPI hooked_time_begin_period(UINT period);
UINT WINAPI hooked_time_end_period(UINT period);

void install(bool enable_dpi_awareness, bool enable_timer_resolution,
             std::uint32_t timer_resolution_ms, bool disable_power_throttling,
             bool log_platform_modernization);

}  // namespace mh2modern::platform
