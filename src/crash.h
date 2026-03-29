#pragma once

#include <cstdint>
#include <windows.h>

namespace mh2modern::crash {

using SetUnhandledExceptionFilterFn =
    LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER);
using RuntimeCrashHandler = void(__cdecl*)(void);
using InvalidParameterHandler =
    void(__cdecl*)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, std::uintptr_t);

bool should_chain_exception_filter(LPTOP_LEVEL_EXCEPTION_FILTER current_handler,
                                   LPTOP_LEVEL_EXCEPTION_FILTER candidate);
bool should_chain_runtime_handler(void* current_handler, void* candidate);
void set_original_set_unhandled_exception_filter(SetUnhandledExceptionFilterFn fn);
void install(HMODULE module);
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI hooked_set_unhandled_exception_filter(
    LPTOP_LEVEL_EXCEPTION_FILTER filter);

}  // namespace mh2modern::crash
