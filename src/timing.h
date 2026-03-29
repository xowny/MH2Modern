#pragma once

#include <cstdint>
#include <windows.h>

namespace mh2modern::timing {

using GetTickCountFn = DWORD(WINAPI*)();
using SleepFn = VOID(WINAPI*)(DWORD);

std::uint32_t qpc_delta_to_milliseconds(std::uint64_t frequency, std::uint64_t start,
                                        std::uint64_t now);
void initialize();
void set_original_get_tick_count(GetTickCountFn fn);
void set_original_sleep(SleepFn fn);
void set_main_thread_id(DWORD thread_id);
void set_sleep_spin_fix_enabled(bool enabled);
void set_timer_logging_enabled(bool enabled);
DWORD WINAPI hooked_get_tick_count();
VOID WINAPI hooked_sleep(DWORD milliseconds);

}  // namespace mh2modern::timing
