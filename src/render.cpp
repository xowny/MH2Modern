#include "render.h"

#include "iat_hook.h"
#include "logger.h"

#include <d3d9.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

namespace mh2modern::render {

IDirect3D9* WINAPI hooked_direct3d_create9(UINT sdk_version);
HRESULT STDMETHODCALLTYPE hooked_create_device(IDirect3D9* self, UINT adapter, D3DDEVTYPE device_type,
                                               HWND focus_window, DWORD behavior_flags,
                                               D3DPRESENT_PARAMETERS* params,
                                               IDirect3DDevice9** out_device);
HRESULT STDMETHODCALLTYPE hooked_test_cooperative_level(IDirect3DDevice9* self);
HRESULT STDMETHODCALLTYPE hooked_reset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* params);

namespace {

constexpr std::size_t kDirect3d9CreateDeviceIndex = 16;
constexpr std::size_t kDirect3dDevice9TestCooperativeLevelIndex = 3;
constexpr std::size_t kDirect3dDevice9ResetIndex = 16;

using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using TestCooperativeLevelFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

GetProcAddressFn g_original_get_proc_address = nullptr;
Direct3DCreate9Fn g_original_direct3d_create9 = nullptr;
CreateDeviceFn g_original_create_device = nullptr;
TestCooperativeLevelFn g_original_test_cooperative_level = nullptr;
ResetFn g_original_reset = nullptr;

bool g_log_render_hooks = false;
bool g_enable_presentation_policy = true;
bool g_force_fullscreen_interval_one = true;
bool g_enable_reset_sanitation = true;
bool g_enable_device_lost_throttle = true;
std::uint32_t g_device_lost_sleep_ms = 1;
std::uint32_t g_device_lost_streak = 0;
std::mutex g_patch_mutex;
bool g_direct3d9_vtable_hooked = false;
bool g_device9_vtable_hooked = false;
HMODULE g_d3d9_module = nullptr;
HWND g_last_device_window = nullptr;

void log_info(std::string_view message);
void log_error(std::string_view message);

std::string filename_only(const char* path) {
    if (path == nullptr || *path == '\0') {
        return {};
    }

    std::string value(path);
    const auto slash = value.find_last_of("\\/");
    return slash == std::string::npos ? value : value.substr(slash + 1);
}

bool equals_ascii_insensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto l = static_cast<unsigned char>(left[i]);
        const auto r = static_cast<unsigned char>(right[i]);
        if (std::tolower(l) != std::tolower(r)) {
            return false;
        }
    }

    return true;
}

bool is_d3d9_module(HMODULE module) {
    if (module == nullptr) {
        return false;
    }

    char module_path[MAX_PATH]{};
    if (GetModuleFileNameA(module, module_path, MAX_PATH) == 0) {
        return false;
    }

    return equals_ascii_insensitive(filename_only(module_path), "d3d9.dll");
}

void log_info(std::string_view message) {
    if (g_log_render_hooks) {
        logger::info(message);
    }
}

void log_error(std::string_view message) {
    if (g_log_render_hooks) {
        logger::error(message);
    }
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

    DWORD restored = 0;
    VirtualProtect(&vtable[index], sizeof(void*), old_protect, &restored);
    FlushInstructionCache(GetCurrentProcess(), &vtable[index], sizeof(void*));
    return true;
}

const char* device_type_to_string(D3DDEVTYPE type) {
    switch (type) {
    case D3DDEVTYPE_HAL:
        return "HAL";
    case D3DDEVTYPE_REF:
        return "REF";
    case D3DDEVTYPE_SW:
        return "SW";
    case D3DDEVTYPE_NULLREF:
        return "NULLREF";
    default:
        return "UNKNOWN";
    }
}

const char* swap_effect_to_string(D3DSWAPEFFECT effect) {
    switch (effect) {
    case D3DSWAPEFFECT_DISCARD:
        return "discard";
    case D3DSWAPEFFECT_FLIP:
        return "flip";
    case D3DSWAPEFFECT_COPY:
        return "copy";
    case D3DSWAPEFFECT_OVERLAY:
        return "overlay";
    case D3DSWAPEFFECT_FLIPEX:
        return "flipex";
    default:
        return "unknown";
    }
}

const char* present_interval_to_string(UINT interval) {
    switch (interval) {
    case D3DPRESENT_INTERVAL_DEFAULT:
        return "default";
    case D3DPRESENT_INTERVAL_ONE:
        return "one";
    case D3DPRESENT_INTERVAL_TWO:
        return "two";
    case D3DPRESENT_INTERVAL_THREE:
        return "three";
    case D3DPRESENT_INTERVAL_FOUR:
        return "four";
    case D3DPRESENT_INTERVAL_IMMEDIATE:
        return "immediate";
    default:
        return "custom";
    }
}

std::string format_to_string(D3DFORMAT format) {
    switch (format) {
    case D3DFMT_UNKNOWN:
        return "UNKNOWN";
    case D3DFMT_A8R8G8B8:
        return "A8R8G8B8";
    case D3DFMT_X8R8G8B8:
        return "X8R8G8B8";
    case D3DFMT_R5G6B5:
        return "R5G6B5";
    case D3DFMT_D16:
        return "D16";
    case D3DFMT_D24S8:
        return "D24S8";
    case D3DFMT_D24X8:
        return "D24X8";
    case D3DFMT_D32:
        return "D32";
    default: {
        std::ostringstream oss;
        oss << "0x" << std::hex << static_cast<std::uint32_t>(format) << std::dec;
        return oss.str();
    }
    }
}

std::string behavior_flags_to_string(DWORD flags) {
    std::ostringstream oss;
    bool first = true;
    const auto append = [&](const char* label) {
        if (!first) {
            oss << '|';
        }
        oss << label;
        first = false;
    };

    if ((flags & D3DCREATE_HARDWARE_VERTEXPROCESSING) != 0) append("hwvp");
    if ((flags & D3DCREATE_MIXED_VERTEXPROCESSING) != 0) append("mixedvp");
    if ((flags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0) append("swvp");
    if ((flags & D3DCREATE_PUREDEVICE) != 0) append("pure");
    if ((flags & D3DCREATE_MULTITHREADED) != 0) append("mt");
    if ((flags & D3DCREATE_FPU_PRESERVE) != 0) append("fpu-preserve");
    if ((flags & D3DCREATE_NOWINDOWCHANGES) != 0) append("no-window-changes");

    if (first) {
        oss << '0';
    }
    return oss.str();
}

void log_present_parameters(const char* label, HWND focus_window, DWORD behavior_flags,
                            const D3DPRESENT_PARAMETERS* params) {
    if (!g_log_render_hooks) {
        return;
    }

    std::ostringstream oss;
    oss << label
        << ": hwnd=0x" << std::hex << reinterpret_cast<std::uintptr_t>(focus_window) << std::dec
        << ", flags=" << behavior_flags_to_string(behavior_flags);
    if (params == nullptr) {
        oss << ", params=<null>";
        logger::info(oss.str());
        return;
    }

    oss << ", backbuffer=" << params->BackBufferWidth << 'x' << params->BackBufferHeight
        << " fmt=" << format_to_string(params->BackBufferFormat)
        << ", count=" << params->BackBufferCount
        << ", multisample=" << static_cast<std::uint32_t>(params->MultiSampleType)
        << ':' << params->MultiSampleQuality
        << ", swap=" << swap_effect_to_string(params->SwapEffect)
        << ", windowed=" << (params->Windowed ? "yes" : "no")
        << ", device_window=0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(params->hDeviceWindow) << std::dec
        << ", autodepth=" << (params->EnableAutoDepthStencil ? "yes" : "no")
        << " fmt=" << format_to_string(params->AutoDepthStencilFormat)
        << ", flags=0x" << std::hex << params->Flags << std::dec
        << ", refresh=" << params->FullScreen_RefreshRateInHz
        << ", interval=" << present_interval_to_string(params->PresentationInterval);
    logger::info(oss.str());
}

bool apply_presentation_policy(D3DPRESENT_PARAMETERS* params, const char* label) {
    if (!g_enable_presentation_policy || params == nullptr) {
        return false;
    }

    const auto requested = static_cast<std::uint32_t>(params->PresentationInterval);
    const auto chosen =
        choose_present_interval(params->Windowed != FALSE, requested, g_force_fullscreen_interval_one);
    if (chosen == requested) {
        return false;
    }

    std::ostringstream oss;
    oss << label << " presentation policy: " << present_interval_to_string(requested) << " -> "
        << present_interval_to_string(chosen);
    log_info(oss.str());
    params->PresentationInterval = chosen;
    return true;
}

void remember_device_window(HWND focus_window, const D3DPRESENT_PARAMETERS* params) {
    const auto chosen = choose_reset_device_window(
        reinterpret_cast<std::uintptr_t>(params != nullptr ? params->hDeviceWindow : nullptr),
        reinterpret_cast<std::uintptr_t>(focus_window));
    if (chosen != 0U) {
        g_last_device_window = reinterpret_cast<HWND>(chosen);
    }
}

bool apply_reset_sanitation(D3DPRESENT_PARAMETERS* params, const char* label) {
    if (!g_enable_reset_sanitation || params == nullptr) {
        return false;
    }

    bool changed = false;

    const auto requested_refresh = params->FullScreen_RefreshRateInHz;
    const auto chosen_refresh =
        choose_reset_refresh_rate(params->Windowed != FALSE, requested_refresh);
    if (chosen_refresh != requested_refresh) {
        std::ostringstream oss;
        oss << label << " reset sanitation: windowed refresh " << requested_refresh << " -> "
            << chosen_refresh;
        log_info(oss.str());
        params->FullScreen_RefreshRateInHz = chosen_refresh;
        changed = true;
    }

    const auto requested_window =
        reinterpret_cast<std::uintptr_t>(params->hDeviceWindow);
    const auto chosen_window =
        choose_reset_device_window(requested_window,
                                   reinterpret_cast<std::uintptr_t>(g_last_device_window));
    if (chosen_window != requested_window) {
        std::ostringstream oss;
        oss << label << " reset sanitation: null device window -> 0x" << std::hex
            << chosen_window << std::dec;
        log_info(oss.str());
        params->hDeviceWindow = reinterpret_cast<HWND>(chosen_window);
        changed = true;
    }

    return changed;
}

const char* device_loss_result_to_string(HRESULT hr) {
    switch (hr) {
    case D3DERR_DEVICELOST:
        return "DEVICELOST";
    case D3DERR_DEVICENOTRESET:
        return "DEVICENOTRESET";
    default:
        return "other";
    }
}

void maybe_throttle_device_loss(const char* label, HRESULT hr) {
    if (!g_enable_device_lost_throttle) {
        return;
    }

    if (is_device_loss_result(static_cast<std::uint32_t>(hr))) {
        ++g_device_lost_streak;
        if (g_log_render_hooks && (g_device_lost_streak == 1U || (g_device_lost_streak % 120U) == 0U)) {
            std::ostringstream oss;
            oss << label << " lost-device throttle: hr=" << device_loss_result_to_string(hr)
                << ", streak=" << g_device_lost_streak << ", sleep_ms=" << g_device_lost_sleep_ms;
            logger::info(oss.str());
        }
        Sleep(g_device_lost_sleep_ms);
        return;
    }

    if (g_device_lost_streak != 0U) {
        if (g_log_render_hooks) {
            std::ostringstream oss;
            oss << label << " cleared lost-device streak after " << g_device_lost_streak
                << " poll(s)";
            logger::info(oss.str());
        }
        g_device_lost_streak = 0U;
    }
}

void install_device9_hooks(IDirect3DDevice9* device) {
    if (device == nullptr) {
        return;
    }

    std::scoped_lock lock(g_patch_mutex);
    if (g_device9_vtable_hooked) {
        return;
    }

    bool installed_any = false;
    if (patch_vtable_entry(
            device, kDirect3dDevice9TestCooperativeLevelIndex,
            reinterpret_cast<void*>(&hooked_test_cooperative_level),
            reinterpret_cast<void**>(&g_original_test_cooperative_level))) {
        installed_any = true;
        log_info("Patched IDirect3DDevice9::TestCooperativeLevel for lost-device throttle");
    }

    if (patch_vtable_entry(
            device, kDirect3dDevice9ResetIndex, reinterpret_cast<void*>(&hooked_reset),
            reinterpret_cast<void**>(&g_original_reset))) {
        installed_any = true;
        log_info("Patched IDirect3DDevice9::Reset for present-parameter logging");
    }

    if (installed_any) {
        g_device9_vtable_hooked = true;
    }
}

void install_direct3d9_hooks(IDirect3D9* d3d9) {
    if (d3d9 == nullptr) {
        return;
    }

    std::scoped_lock lock(g_patch_mutex);
    if (g_direct3d9_vtable_hooked) {
        return;
    }

    if (patch_vtable_entry(
            d3d9, kDirect3d9CreateDeviceIndex, reinterpret_cast<void*>(&hooked_create_device),
            reinterpret_cast<void**>(&g_original_create_device))) {
        g_direct3d9_vtable_hooked = true;
        log_info("Patched IDirect3D9::CreateDevice for render probe");
    }
}

}  // namespace

bool is_supported_d3d9_proc_name(std::string_view proc_name) {
    return proc_name == "Direct3DCreate9";
}

std::uint32_t choose_present_interval(bool windowed, std::uint32_t requested,
                                      bool force_fullscreen_interval_one) {
    if (!force_fullscreen_interval_one || windowed ||
        requested != static_cast<std::uint32_t>(D3DPRESENT_INTERVAL_DEFAULT)) {
        return requested;
    }

    return static_cast<std::uint32_t>(D3DPRESENT_INTERVAL_ONE);
}

std::uint32_t choose_reset_refresh_rate(bool windowed, std::uint32_t requested) {
    return windowed ? 0U : requested;
}

std::uintptr_t choose_reset_device_window(std::uintptr_t requested, std::uintptr_t fallback) {
    return requested != 0U ? requested : fallback;
}

std::uint32_t normalize_device_lost_sleep_ms(std::uint32_t value) {
    if (value == 0U) {
        return 1U;
    }
    return value > 16U ? 16U : value;
}

bool is_device_loss_result(std::uint32_t hr) {
    const auto result = static_cast<HRESULT>(hr);
    return result == D3DERR_DEVICELOST || result == D3DERR_DEVICENOTRESET;
}

void set_original_get_proc_address(GetProcAddressFn fn) {
    g_original_get_proc_address = fn;
}

void set_render_logging_enabled(bool enabled) {
    g_log_render_hooks = enabled;
}

HRESULT install_d3d9_probe(HMODULE game_module, bool log_render_hooks,
                           bool enable_presentation_policy,
                           bool force_fullscreen_interval_one,
                           bool enable_reset_sanitation,
                           bool enable_device_lost_throttle,
                           std::uint32_t device_lost_sleep_ms) {
    set_render_logging_enabled(log_render_hooks);
    g_enable_presentation_policy = enable_presentation_policy;
    g_force_fullscreen_interval_one = force_fullscreen_interval_one;
    g_enable_reset_sanitation = enable_reset_sanitation;
    g_enable_device_lost_throttle = enable_device_lost_throttle;
    g_device_lost_sleep_ms = normalize_device_lost_sleep_ms(device_lost_sleep_ms);
    g_device_lost_streak = 0U;
    g_d3d9_module = nullptr;
    g_last_device_window = nullptr;

    if (game_module == nullptr) {
        return E_HANDLE;
    }

    void* original = nullptr;
    if (!iat_hook::patch_import(
            game_module, "KERNEL32.dll", "GetProcAddress",
            reinterpret_cast<void*>(&hooked_get_proc_address), &original)) {
        log_error("Failed to patch KERNEL32.dll!GetProcAddress for D3D9 probe");
        return E_FAIL;
    }

    set_original_get_proc_address(reinterpret_cast<GetProcAddressFn>(original));
    log_info("Patched KERNEL32.dll!GetProcAddress for D3D9 probe");
    if (g_enable_presentation_policy && g_force_fullscreen_interval_one) {
        log_info("Enabled render presentation policy: fullscreen default interval -> ONE");
    }
    if (g_enable_reset_sanitation) {
        log_info("Enabled render reset sanitation: windowed refresh -> 0, null device window -> last known");
    }
    if (g_enable_device_lost_throttle) {
        std::ostringstream oss;
        oss << "Enabled render lost-device throttle: sleep_ms=" << g_device_lost_sleep_ms;
        log_info(oss.str());
    }
    return S_OK;
}

FARPROC WINAPI hooked_get_proc_address(HMODULE module, LPCSTR proc_name) {
    if (g_original_get_proc_address == nullptr) {
        return nullptr;
    }

    const auto original = g_original_get_proc_address(module, proc_name);
    if (proc_name == nullptr || IS_INTRESOURCE(proc_name) || !is_d3d9_module(module) ||
        !is_supported_d3d9_proc_name(proc_name)) {
        return original;
    }

    g_d3d9_module = module;

    if (std::strcmp(proc_name, "Direct3DCreate9") == 0) {
        g_original_direct3d_create9 = reinterpret_cast<Direct3DCreate9Fn>(original);
        log_info("Intercepted d3d9!Direct3DCreate9 export resolution");
        return reinterpret_cast<FARPROC>(&hooked_direct3d_create9);
    }

    return original;
}

IDirect3D9* WINAPI hooked_direct3d_create9(UINT sdk_version) {
    if (g_original_direct3d_create9 == nullptr) {
        return nullptr;
    }

    auto* d3d9 = g_original_direct3d_create9(sdk_version);
    std::ostringstream oss;
    oss << "Direct3DCreate9(sdk=0x" << std::hex << sdk_version << std::dec << ") -> 0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(d3d9) << std::dec;
    log_info(oss.str());
    install_direct3d9_hooks(d3d9);
    return d3d9;
}

HRESULT STDMETHODCALLTYPE hooked_create_device(IDirect3D9* self, UINT adapter, D3DDEVTYPE device_type,
                                               HWND focus_window, DWORD behavior_flags,
                                               D3DPRESENT_PARAMETERS* params,
                                               IDirect3DDevice9** out_device) {
    if (g_original_create_device == nullptr) {
        return E_FAIL;
    }

    D3DPRESENT_PARAMETERS original_params{};
    const auto has_params = params != nullptr;
    if (has_params) {
        original_params = *params;
    }
    const auto applied_interval =
        apply_presentation_policy(params, "IDirect3D9::CreateDevice");

    std::ostringstream oss;
    oss << "IDirect3D9::CreateDevice(adapter=" << adapter << ", type="
        << device_type_to_string(device_type) << ")";
    log_present_parameters(oss.str().c_str(), focus_window, behavior_flags, params);

    auto hr = g_original_create_device(
        self, adapter, device_type, focus_window, behavior_flags, params, out_device);

    if (FAILED(hr) && has_params && applied_interval) {
        logger::info(
            "IDirect3D9::CreateDevice failed with MH2Modern render policy; retrying original request");
        *params = original_params;
        hr = g_original_create_device(
            self, adapter, device_type, focus_window, behavior_flags, params, out_device);
    }

    std::ostringstream result_oss;
    result_oss << "IDirect3D9::CreateDevice result hr=0x" << std::hex
               << static_cast<std::uint32_t>(hr) << std::dec << ", device=0x" << std::hex
               << reinterpret_cast<std::uintptr_t>(out_device != nullptr ? *out_device : nullptr)
               << std::dec;
    log_info(result_oss.str());

    if (SUCCEEDED(hr) && out_device != nullptr) {
        remember_device_window(focus_window, params);
        install_device9_hooks(*out_device);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hooked_test_cooperative_level(IDirect3DDevice9* self) {
    if (g_original_test_cooperative_level == nullptr) {
        return E_FAIL;
    }

    const auto hr = g_original_test_cooperative_level(self);
    maybe_throttle_device_loss("IDirect3DDevice9::TestCooperativeLevel", hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE hooked_reset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* params) {
    if (g_original_reset == nullptr) {
        return E_FAIL;
    }

    D3DPRESENT_PARAMETERS original_params{};
    const auto has_params = params != nullptr;
    if (has_params) {
        original_params = *params;
    }
    const auto applied_interval =
        apply_presentation_policy(params, "IDirect3DDevice9::Reset");
    const auto applied_sanitation =
        apply_reset_sanitation(params, "IDirect3DDevice9::Reset");

    log_present_parameters("IDirect3DDevice9::Reset", nullptr, 0U, params);
    auto hr = g_original_reset(self, params);

    if (FAILED(hr) && has_params && applied_interval) {
        logger::info(
            "IDirect3DDevice9::Reset failed with MH2Modern render policy; retrying sanitized original request");
        D3DPRESENT_PARAMETERS retry_params = original_params;
        apply_reset_sanitation(&retry_params, "IDirect3DDevice9::Reset retry");
        hr = g_original_reset(self, &retry_params);
        if (SUCCEEDED(hr)) {
            *params = retry_params;
        }
    }

    std::ostringstream oss;
    oss << "IDirect3DDevice9::Reset result hr=0x" << std::hex << static_cast<std::uint32_t>(hr)
        << std::dec;
    log_info(oss.str());
    if (SUCCEEDED(hr) && has_params) {
        remember_device_window(nullptr, params);
    } else {
        (void)applied_sanitation;
    }
    maybe_throttle_device_loss("IDirect3DDevice9::Reset", hr);
    return hr;
}

}  // namespace mh2modern::render
