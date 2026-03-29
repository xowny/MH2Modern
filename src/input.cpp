#include "input.h"

#include "logger.h"

#include <atomic>
#include <cstdint>
#include <limits.h>
#include <mutex>
#include <sstream>

namespace mh2modern::input {

HRESULT STDMETHODCALLTYPE hooked_create_device(void* self, REFGUID guid, void** out_device,
                                               void* outer);
HRESULT STDMETHODCALLTYPE hooked_set_cooperative_level(void* self, HWND hwnd, DWORD flags);
HRESULT STDMETHODCALLTYPE hooked_get_device_state(void* self, DWORD cb_data, LPVOID data);
BOOL WINAPI hooked_clip_cursor(const RECT* rect);

namespace {

constexpr GUID kIidDirectInput8A{
    0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID kIidDirectInput8W{
    0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID kGuidSysMouse{
    0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidSysMouseEm{
    0x6F1D2B80, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidSysMouseEm2{
    0x6F1D2B81, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

constexpr std::size_t kDi8CreateDeviceIndex = 3;
constexpr std::size_t kDeviceGetDeviceStateIndex = 9;
constexpr std::size_t kDeviceSetCooperativeLevelIndex = 13;

constexpr USHORT kUsagePageGenericDesktop = 0x01;
constexpr USHORT kUsageGenericMouse = 0x02;

struct MouseStatePrefix {
    LONG lX;
    LONG lY;
    LONG lZ;
};

using Di8CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(void* self, REFGUID guid, void** out_device,
                                                      void* outer);
using DeviceGetDeviceStateFn = HRESULT(STDMETHODCALLTYPE*)(void* self, DWORD cb_data, LPVOID data);
using DeviceSetCooperativeLevelFn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, HWND hwnd, DWORD flags);

DirectInput8CreateFn g_original_direct_input8_create = nullptr;
ClipCursorFn g_original_clip_cursor = nullptr;
SystemParametersInfoAFn g_original_system_parameters_info_a = nullptr;
SystemParametersInfoWFn g_original_system_parameters_info_w = nullptr;
Di8CreateDeviceFn g_original_di8_create_device = nullptr;
DeviceGetDeviceStateFn g_original_get_device_state = nullptr;
DeviceSetCooperativeLevelFn g_original_set_cooperative_level = nullptr;

std::atomic<bool> g_raw_mouse_enabled{false};
std::atomic<bool> g_cursor_clip_modernization_enabled{false};
std::atomic<bool> g_mouse_spi_guard_enabled{false};
std::atomic<bool> g_log_input_hooks{false};
std::atomic<bool> g_direct_input_vtable_hooked{false};
std::atomic<bool> g_mouse_device_vtable_hooked{false};
std::atomic<std::int64_t> g_pending_raw_x{0};
std::atomic<std::int64_t> g_pending_raw_y{0};

std::mutex g_patch_mutex;
std::mutex g_window_mutex;
HWND g_input_window = nullptr;
WNDPROC g_original_wndproc = nullptr;
RECT g_saved_clip_rect{};
bool g_has_saved_clip_rect = false;
bool g_restore_clip_on_focus = false;
bool g_focus_loss_clip_released = false;

bool should_log() {
    return g_log_input_hooks.load();
}

void log_info(std::string_view message) {
    if (should_log()) {
        logger::info(message);
    }
}

void log_last_error(const char* label) {
    if (!should_log()) {
        return;
    }

    std::ostringstream oss;
    oss << label << " failed, gle=" << GetLastError();
    logger::error(oss.str());
}

const char* spi_action_name(UINT action) {
    switch (action) {
    case SPI_GETMOUSE:
        return "SPI_GETMOUSE";
    case SPI_SETMOUSE:
        return "SPI_SETMOUSE";
    case SPI_GETFILTERKEYS:
        return "SPI_GETFILTERKEYS";
    case SPI_SETFILTERKEYS:
        return "SPI_SETFILTERKEYS";
    case SPI_GETTOGGLEKEYS:
        return "SPI_GETTOGGLEKEYS";
    case SPI_SETTOGGLEKEYS:
        return "SPI_SETTOGGLEKEYS";
    case SPI_GETSTICKYKEYS:
        return "SPI_GETSTICKYKEYS";
    case SPI_SETSTICKYKEYS:
        return "SPI_SETSTICKYKEYS";
    case SPI_GETMOUSESPEED:
        return "SPI_GETMOUSESPEED";
    case SPI_SETMOUSESPEED:
        return "SPI_SETMOUSESPEED";
    case SPI_SETMOUSEBUTTONSWAP:
        return "SPI_SETMOUSEBUTTONSWAP";
    default:
        return "SPI_UNKNOWN";
    }
}

BOOL forward_system_parameters_info_call(SystemParametersInfoAFn fn, UINT action, UINT param,
                                         PVOID pv_param, UINT win_ini, const char* api_name) {
    if (fn == nullptr) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    const auto result = fn(action, param, pv_param, win_ini);
    if (!should_log()) {
        return result;
    }

    std::ostringstream oss;
    oss << api_name << '(' << spi_action_name(action);
    if (spi_action_name(action) == std::string_view{"SPI_UNKNOWN"}) {
        oss << "/0x" << std::hex << action << std::dec;
    }
    oss << ") -> result=" << result;
    logger::info(oss.str());
    return result;
}

BOOL maybe_block_mouse_spi_action(UINT action, const char* api_name) {
    if (!g_mouse_spi_guard_enabled.load() ||
        (!is_mutating_mouse_spi_action(action) &&
         !is_mutating_accessibility_spi_action(action))) {
        return FALSE;
    }

    std::ostringstream oss;
    oss << "Blocked " << api_name << " input/accessibility mutation: "
        << spi_action_name(action);
    if (spi_action_name(action) == std::string_view{"SPI_UNKNOWN"}) {
        oss << " (0x" << std::hex << action << std::dec << ')';
    }
    logger::info(oss.str());
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

bool patch_vtable_entry(void* instance, std::size_t index, void* replacement, void** original_out) {
    if (instance == nullptr || replacement == nullptr) {
        return false;
    }

    auto** vtable = *reinterpret_cast<void***>(instance);
    if (vtable == nullptr) {
        return false;
    }

    if (vtable[index] == replacement) {
        return true;
    }

    DWORD old_protect = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    if (original_out != nullptr && *original_out == nullptr) {
        *original_out = vtable[index];
    }
    vtable[index] = replacement;

    DWORD restored_protect = 0;
    VirtualProtect(&vtable[index], sizeof(void*), old_protect, &restored_protect);
    FlushInstructionCache(GetCurrentProcess(), &vtable[index], sizeof(void*));
    return true;
}

void release_cursor_clip_for_focus_loss() {
    if (!g_cursor_clip_modernization_enabled.load() || g_original_clip_cursor == nullptr) {
        return;
    }

    {
        std::scoped_lock lock(g_window_mutex);
        if (!should_issue_focus_loss_release(g_focus_loss_clip_released)) {
            return;
        }
        g_restore_clip_on_focus = g_has_saved_clip_rect;
        g_focus_loss_clip_released = true;
    }

    g_original_clip_cursor(nullptr);
    log_info("Released cursor clip on focus loss");
}

void restore_cursor_clip_for_focus_gain() {
    if (!g_cursor_clip_modernization_enabled.load() || g_original_clip_cursor == nullptr) {
        return;
    }

    RECT rect{};
    {
        std::scoped_lock lock(g_window_mutex);
        if (!g_restore_clip_on_focus || !g_has_saved_clip_rect) {
            g_focus_loss_clip_released = false;
            return;
        }
        rect = g_saved_clip_rect;
    }

    if (g_original_clip_cursor(&rect)) {
        std::scoped_lock lock(g_window_mutex);
        g_restore_clip_on_focus = false;
        g_focus_loss_clip_released = false;
        log_info("Restored saved cursor clip on focus gain");
        return;
    }

    log_last_error("ClipCursor(restore)");
}

LRESULT CALLBACK raw_mouse_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_INPUT && g_raw_mouse_enabled.load()) {
        RAWINPUT raw_input{};
        UINT size = sizeof(raw_input);
        if (GetRawInputData(
                reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw_input, &size,
                sizeof(RAWINPUTHEADER)) == size &&
            raw_input.header.dwType == RIM_TYPEMOUSE &&
            (raw_input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            if (raw_input.data.mouse.lLastX != 0) {
                g_pending_raw_x.fetch_add(static_cast<std::int64_t>(raw_input.data.mouse.lLastX));
            }
            if (raw_input.data.mouse.lLastY != 0) {
                g_pending_raw_y.fetch_add(static_cast<std::int64_t>(raw_input.data.mouse.lLastY));
            }
        }
    }

    WNDPROC original = nullptr;
    {
        std::scoped_lock lock(g_window_mutex);
        original = g_original_wndproc;
    }

    const auto result = original != nullptr ? CallWindowProcW(original, hwnd, msg, wparam, lparam)
                                            : DefWindowProcW(hwnd, msg, wparam, lparam);

    if (should_release_cursor_clip(msg, wparam)) {
        release_cursor_clip_for_focus_loss();
    } else if (should_restore_cursor_clip(msg, wparam)) {
        restore_cursor_clip_for_focus_gain();
    }

    if (msg == WM_NCDESTROY) {
        std::scoped_lock lock(g_window_mutex);
        if (g_input_window == hwnd) {
            g_input_window = nullptr;
            g_original_wndproc = nullptr;
            g_has_saved_clip_rect = false;
            g_restore_clip_on_focus = false;
        }
    }

    return result;
}

bool install_window_raw_input(HWND hwnd) {
    if (hwnd == nullptr || !g_raw_mouse_enabled.load()) {
        return false;
    }

    std::scoped_lock lock(g_window_mutex);
    if (g_input_window == hwnd && g_original_wndproc != nullptr) {
        return true;
    }

    if (g_input_window != nullptr && g_original_wndproc != nullptr) {
        const auto current = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrW(g_input_window, GWLP_WNDPROC));
        if (current == raw_mouse_wndproc) {
            SetWindowLongPtrW(
                g_input_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc));
        }
        g_input_window = nullptr;
        g_original_wndproc = nullptr;
    }

    SetLastError(0);
    const auto previous = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&raw_mouse_wndproc)));
    if (previous == nullptr && GetLastError() != 0) {
        log_last_error("SetWindowLongPtrW(GWLP_WNDPROC)");
        return false;
    }

    RAWINPUTDEVICE device{};
    device.usUsagePage = kUsagePageGenericDesktop;
    device.usUsage = kUsageGenericMouse;
    device.dwFlags = 0;
    device.hwndTarget = hwnd;
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        log_last_error("RegisterRawInputDevices");
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
        return false;
    }

    g_input_window = hwnd;
    g_original_wndproc = previous;

    std::ostringstream oss;
    oss << "Installed raw mouse WM_INPUT hook on hwnd=0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(hwnd) << std::dec;
    log_info(oss.str());
    return true;
}

bool install_mouse_device_hooks(void* device) {
    if (device == nullptr) {
        return false;
    }

    std::scoped_lock lock(g_patch_mutex);
    bool ok = true;
    ok &= patch_vtable_entry(
        device, kDeviceGetDeviceStateIndex, reinterpret_cast<void*>(&hooked_get_device_state),
        reinterpret_cast<void**>(&g_original_get_device_state));
    ok &= patch_vtable_entry(
        device, kDeviceSetCooperativeLevelIndex,
        reinterpret_cast<void*>(&hooked_set_cooperative_level),
        reinterpret_cast<void**>(&g_original_set_cooperative_level));
    if (ok) {
        g_mouse_device_vtable_hooked.store(true);
        log_info("Patched DirectInput mouse device vtable for raw input");
    }
    return ok;
}

bool install_direct_input_hooks(void* direct_input) {
    if (direct_input == nullptr) {
        return false;
    }

    std::scoped_lock lock(g_patch_mutex);
    const auto ok = patch_vtable_entry(
        direct_input, kDi8CreateDeviceIndex, reinterpret_cast<void*>(&hooked_create_device),
        reinterpret_cast<void**>(&g_original_di8_create_device));
    if (ok) {
        g_direct_input_vtable_hooked.store(true);
        log_info("Patched DirectInput8 CreateDevice for mouse wrapping");
    }
    return ok;
}

}  // namespace

bool is_direct_input_8_interface(REFIID riid) {
    return IsEqualGUID(riid, kIidDirectInput8A) || IsEqualGUID(riid, kIidDirectInput8W);
}

bool should_wrap_mouse_guid(const GUID& guid) {
    return IsEqualGUID(guid, kGuidSysMouse) || IsEqualGUID(guid, kGuidSysMouseEm) ||
           IsEqualGUID(guid, kGuidSysMouseEm2);
}

bool should_release_cursor_clip(UINT msg, WPARAM wparam) {
    return (msg == WM_ACTIVATEAPP && wparam == FALSE) || msg == WM_KILLFOCUS;
}

bool should_restore_cursor_clip(UINT msg, WPARAM wparam) {
    return (msg == WM_ACTIVATEAPP && wparam == TRUE) || msg == WM_SETFOCUS;
}

bool should_issue_focus_loss_release(bool already_released) {
    return !already_released;
}

bool should_preserve_saved_cursor_clip_on_game_release(bool focus_release_active,
                                                       bool restore_pending, bool has_saved_clip) {
    return focus_release_active && restore_pending && has_saved_clip;
}

bool is_mutating_mouse_spi_action(UINT action) {
    switch (action) {
    case SPI_SETMOUSE:
    case SPI_SETMOUSESPEED:
    case SPI_SETMOUSEBUTTONSWAP:
        return true;
    default:
        return false;
    }
}

bool is_mutating_accessibility_spi_action(UINT action) {
    switch (action) {
    case SPI_SETFILTERKEYS:
    case SPI_SETTOGGLEKEYS:
    case SPI_SETSTICKYKEYS:
        return true;
    default:
        return false;
    }
}

LONG saturating_long_from_delta(std::int64_t value) {
    if (value > static_cast<std::int64_t>(LONG_MAX)) {
        return LONG_MAX;
    }
    if (value < static_cast<std::int64_t>(LONG_MIN)) {
        return LONG_MIN;
    }
    return static_cast<LONG>(value);
}

MouseDelta choose_mouse_delta(bool enable_raw_mouse, std::int64_t raw_x, std::int64_t raw_y,
                              LONG fallback_x, LONG fallback_y) {
    if (!enable_raw_mouse) {
        return MouseDelta{fallback_x, fallback_y};
    }

    MouseDelta delta{};
    delta.x = raw_x != 0 ? saturating_long_from_delta(raw_x) : fallback_x;
    delta.y = raw_y != 0 ? saturating_long_from_delta(raw_y) : fallback_y;
    return delta;
}

void set_original_direct_input8_create(DirectInput8CreateFn fn) {
    g_original_direct_input8_create = fn;
}

void set_original_clip_cursor(ClipCursorFn fn) {
    g_original_clip_cursor = fn;
}

void set_original_system_parameters_info_a(SystemParametersInfoAFn fn) {
    g_original_system_parameters_info_a = fn;
}

void set_original_system_parameters_info_w(SystemParametersInfoWFn fn) {
    g_original_system_parameters_info_w = fn;
}

void set_raw_mouse_enabled(bool enabled) {
    g_raw_mouse_enabled.store(enabled);
    if (!enabled) {
        g_pending_raw_x.store(0);
        g_pending_raw_y.store(0);
    }
}

void set_input_logging_enabled(bool enabled) {
    g_log_input_hooks.store(enabled);
}

void set_cursor_clip_modernization_enabled(bool enabled) {
    g_cursor_clip_modernization_enabled.store(enabled);
}

void set_mouse_spi_guard_enabled(bool enabled) {
    g_mouse_spi_guard_enabled.store(enabled);
}

bool is_game_window_active() {
    std::scoped_lock lock(g_window_mutex);
    if (g_input_window == nullptr) {
        return true;
    }

    return GetForegroundWindow() == g_input_window;
}

HRESULT STDMETHODCALLTYPE hooked_create_device(void* self, REFGUID guid, void** out_device,
                                               void* outer) {
    if (g_original_di8_create_device == nullptr) {
        return E_FAIL;
    }

    const auto result = g_original_di8_create_device(self, guid, out_device, outer);
    if (FAILED(result) || out_device == nullptr || *out_device == nullptr || !g_raw_mouse_enabled.load() ||
        !should_wrap_mouse_guid(guid)) {
        return result;
    }

    if (!install_mouse_device_hooks(*out_device) && should_log()) {
        logger::error("Failed to patch DirectInput mouse device hooks");
    }
    return result;
}

HRESULT STDMETHODCALLTYPE hooked_set_cooperative_level(void* self, HWND hwnd, DWORD flags) {
    if (g_original_set_cooperative_level == nullptr) {
        return E_FAIL;
    }

    const auto result = g_original_set_cooperative_level(self, hwnd, flags);
    if (SUCCEEDED(result) && g_raw_mouse_enabled.load()) {
        if (!install_window_raw_input(hwnd) && should_log()) {
            logger::error("Failed to install raw mouse window hook");
        }
    }
    return result;
}

HRESULT STDMETHODCALLTYPE hooked_get_device_state(void* self, DWORD cb_data, LPVOID data) {
    if (g_original_get_device_state == nullptr) {
        return E_FAIL;
    }

    const auto result = g_original_get_device_state(self, cb_data, data);
    if (FAILED(result) || data == nullptr || cb_data < sizeof(MouseStatePrefix)) {
        return result;
    }

    auto* mouse_state = static_cast<MouseStatePrefix*>(data);
    const auto delta = choose_mouse_delta(
        g_raw_mouse_enabled.load(), g_pending_raw_x.exchange(0), g_pending_raw_y.exchange(0),
        mouse_state->lX, mouse_state->lY);
    mouse_state->lX = delta.x;
    mouse_state->lY = delta.y;
    return result;
}

HRESULT WINAPI hooked_direct_input8_create(HINSTANCE instance, DWORD version, REFIID riid,
                                           LPVOID* out, void* outer) {
    if (g_original_direct_input8_create == nullptr) {
        return E_FAIL;
    }

    const auto result = g_original_direct_input8_create(instance, version, riid, out, outer);
    if (FAILED(result) || out == nullptr || *out == nullptr || !g_raw_mouse_enabled.load() ||
        !is_direct_input_8_interface(riid)) {
        return result;
    }

    if (!install_direct_input_hooks(*out) && should_log()) {
        logger::error("Failed to patch DirectInput8 hooks");
    }
    return result;
}

BOOL WINAPI hooked_clip_cursor(const RECT* rect) {
    if (g_original_clip_cursor == nullptr) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    const auto result = g_original_clip_cursor(rect);
    if (!result || !g_cursor_clip_modernization_enabled.load()) {
        return result;
    }

    std::scoped_lock lock(g_window_mutex);
    if (rect != nullptr) {
        g_saved_clip_rect = *rect;
        g_has_saved_clip_rect = true;
        g_restore_clip_on_focus = false;
        g_focus_loss_clip_released = false;
        if (should_log()) {
            std::ostringstream oss;
            oss << "Captured game cursor clip rect: (" << rect->left << ',' << rect->top << ")-("
                << rect->right << ',' << rect->bottom << ')';
            logger::info(oss.str());
        }
    } else {
        if (!should_preserve_saved_cursor_clip_on_game_release(
                g_focus_loss_clip_released, g_restore_clip_on_focus, g_has_saved_clip_rect)) {
            g_has_saved_clip_rect = false;
            g_restore_clip_on_focus = false;
            g_focus_loss_clip_released = false;
        }
        log_info("Game released cursor clip");
    }
    return result;
}

BOOL WINAPI hooked_system_parameters_info_a(UINT action, UINT param, PVOID pv_param, UINT win_ini) {
    if (maybe_block_mouse_spi_action(action, "SystemParametersInfoA")) {
        return TRUE;
    }

    return forward_system_parameters_info_call(
        g_original_system_parameters_info_a, action, param, pv_param, win_ini,
        "SystemParametersInfoA");
}

BOOL WINAPI hooked_system_parameters_info_w(UINT action, UINT param, PVOID pv_param, UINT win_ini) {
    if (maybe_block_mouse_spi_action(action, "SystemParametersInfoW")) {
        return TRUE;
    }

    if (g_original_system_parameters_info_w == nullptr) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    const auto result = g_original_system_parameters_info_w(action, param, pv_param, win_ini);
    if (!should_log()) {
        return result;
    }

    std::ostringstream oss;
    oss << "SystemParametersInfoW(" << spi_action_name(action);
    if (spi_action_name(action) == std::string_view{"SPI_UNKNOWN"}) {
        oss << "/0x" << std::hex << action << std::dec;
    }
    oss << ") -> result=" << result;
    logger::info(oss.str());
    return result;
}

}  // namespace mh2modern::input
