#pragma once

#include <cstdint>
#include <windows.h>

namespace mh2modern::input {

struct MouseDelta {
    LONG x{0};
    LONG y{0};
};

bool is_direct_input_8_interface(REFIID riid);
bool should_wrap_mouse_guid(const GUID& guid);
LONG saturating_long_from_delta(std::int64_t value);
MouseDelta choose_mouse_delta(bool enable_raw_mouse, std::int64_t raw_x, std::int64_t raw_y,
                              LONG fallback_x, LONG fallback_y);
bool should_release_cursor_clip(UINT msg, WPARAM wparam);
bool should_restore_cursor_clip(UINT msg, WPARAM wparam);
bool should_issue_focus_loss_release(bool already_released);
bool should_preserve_saved_cursor_clip_on_game_release(bool focus_release_active,
                                                       bool restore_pending, bool has_saved_clip);
bool is_mutating_mouse_spi_action(UINT action);
bool is_mutating_accessibility_spi_action(UINT action);

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using SystemParametersInfoAFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT);
using SystemParametersInfoWFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT);

void set_original_direct_input8_create(DirectInput8CreateFn fn);
void set_original_clip_cursor(ClipCursorFn fn);
void set_original_system_parameters_info_a(SystemParametersInfoAFn fn);
void set_original_system_parameters_info_w(SystemParametersInfoWFn fn);
void set_raw_mouse_enabled(bool enabled);
void set_input_logging_enabled(bool enabled);
void set_cursor_clip_modernization_enabled(bool enabled);
void set_mouse_spi_guard_enabled(bool enabled);
bool is_game_window_active();

HRESULT WINAPI hooked_direct_input8_create(HINSTANCE instance, DWORD version, REFIID riid,
                                           LPVOID* out, void* outer);
BOOL WINAPI hooked_clip_cursor(const RECT* rect);
BOOL WINAPI hooked_system_parameters_info_a(UINT action, UINT param, PVOID pv_param, UINT win_ini);
BOOL WINAPI hooked_system_parameters_info_w(UINT action, UINT param, PVOID pv_param, UINT win_ini);

}  // namespace mh2modern::input
