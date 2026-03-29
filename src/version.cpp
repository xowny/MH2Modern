#include "version.h"

#include "iat_hook.h"
#include "logger.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace mh2modern::version {
namespace {

constexpr const char* kKernel32ModuleName = "KERNEL32.dll";
constexpr const char* kGetVersionExAProc = "GetVersionExA";
constexpr const char* kGetVersionExWProc = "GetVersionExW";
constexpr std::size_t kMaxPendingLogEntries = 16;

using GetVersionExAFn = BOOL(WINAPI*)(LPOSVERSIONINFOA);
using GetVersionExWFn = BOOL(WINAPI*)(LPOSVERSIONINFOW);

struct RtlOsVersionInfoExW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
    USHORT wServicePackMajor;
    USHORT wServicePackMinor;
    USHORT wSuiteMask;
    UCHAR wProductType;
    UCHAR wReserved;
};

using RtlGetVersionFn = LONG(WINAPI*)(RtlOsVersionInfoExW*);

GetVersionExAFn g_original_get_version_ex_a = nullptr;
GetVersionExWFn g_original_get_version_ex_w = nullptr;
std::atomic<bool> g_log_version_calls{false};
std::atomic<bool> g_hooked_get_version_ex_a{false};
std::atomic<bool> g_hooked_get_version_ex_w{false};
std::atomic<bool> g_runtime_logging_ready{false};
std::mutex g_pending_log_mutex;
std::array<std::string, kMaxPendingLogEntries> g_pending_log_messages{};
std::uint32_t g_pending_log_count = 0;
std::uint32_t g_pending_log_dropped = 0;

bool is_valid_descriptor(const VersionDescriptor& version) {
    return version.major != 0 || version.minor != 0 || version.build != 0 ||
           version.platform_id != 0 || version.product_type != 0 || version.suite_mask != 0;
}

bool descriptors_match(const VersionDescriptor& left, const VersionDescriptor& right) {
    return left.major == right.major && left.minor == right.minor && left.build == right.build &&
           left.platform_id == right.platform_id &&
           left.service_pack_major == right.service_pack_major &&
           left.service_pack_minor == right.service_pack_minor &&
           left.suite_mask == right.suite_mask && left.product_type == right.product_type;
}

int compare_version_triplet(const VersionDescriptor& left, const VersionDescriptor& right) {
    if (left.major != right.major) {
        return left.major < right.major ? -1 : 1;
    }
    if (left.minor != right.minor) {
        return left.minor < right.minor ? -1 : 1;
    }
    if (left.build != right.build) {
        return left.build < right.build ? -1 : 1;
    }
    return 0;
}

std::string filename_only(const char* path) {
    if (path == nullptr || *path == '\0') {
        return {};
    }

    std::string value(path);
    const auto slash = value.find_last_of("\\/");
    return slash == std::string::npos ? value : value.substr(slash + 1);
}

std::uint32_t module_size(HMODULE module) {
    if (module == nullptr) {
        return 0;
    }

    auto* base = reinterpret_cast<std::uint8_t*>(module);
    const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }

    const auto* nt_headers =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }

    return nt_headers->OptionalHeader.SizeOfImage;
}

std::string describe_address(std::uintptr_t address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &module)) {
        std::ostringstream oss;
        oss << "0x" << std::hex << address << std::dec;
        return oss.str();
    }

    char module_path[MAX_PATH]{};
    GetModuleFileNameA(module, module_path, MAX_PATH);

    std::ostringstream oss;
    oss << filename_only(module_path);
    const auto size = module_size(module);
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    if (size != 0 && address >= base && address < (base + size)) {
        oss << "+0x" << std::hex << (address - base) << std::dec;
    }
    return oss.str();
}

std::string caller_suffix() {
    void* frames[4]{};
    const auto captured =
        CaptureStackBackTrace(1, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    if (captured == 0) {
        return {};
    }

    std::ostringstream oss;
    oss << ", stack=";
    for (USHORT i = 0; i < captured; ++i) {
        if (i != 0) {
            oss << " <- ";
        }
        oss << describe_address(reinterpret_cast<std::uintptr_t>(frames[i]));
    }
    return oss.str();
}

std::string narrow(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const auto size = WideCharToMultiByte(
        CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_ACP, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

VersionDescriptor descriptor_from_info_a(const OSVERSIONINFOA& info) {
    VersionDescriptor version{};
    version.major = info.dwMajorVersion;
    version.minor = info.dwMinorVersion;
    version.build = info.dwBuildNumber;
    version.platform_id = info.dwPlatformId;
    if (info.dwOSVersionInfoSize >= sizeof(OSVERSIONINFOEXA)) {
        const auto* info_ex = reinterpret_cast<const OSVERSIONINFOEXA*>(&info);
        version.service_pack_major = info_ex->wServicePackMajor;
        version.service_pack_minor = info_ex->wServicePackMinor;
        version.suite_mask = info_ex->wSuiteMask;
        version.product_type = info_ex->wProductType;
    }
    return version;
}

VersionDescriptor descriptor_from_info_w(const OSVERSIONINFOW& info) {
    VersionDescriptor version{};
    version.major = info.dwMajorVersion;
    version.minor = info.dwMinorVersion;
    version.build = info.dwBuildNumber;
    version.platform_id = info.dwPlatformId;
    if (info.dwOSVersionInfoSize >= sizeof(OSVERSIONINFOEXW)) {
        const auto* info_ex = reinterpret_cast<const OSVERSIONINFOEXW*>(&info);
        version.service_pack_major = info_ex->wServicePackMajor;
        version.service_pack_minor = info_ex->wServicePackMinor;
        version.suite_mask = info_ex->wSuiteMask;
        version.product_type = info_ex->wProductType;
    }
    return version;
}

RtlGetVersionFn resolve_rtl_get_version() {
    static RtlGetVersionFn fn = []() -> RtlGetVersionFn {
        const auto module = GetModuleHandleW(L"ntdll.dll");
        if (module == nullptr) {
            return nullptr;
        }

        union ProcCast {
            FARPROC raw;
            RtlGetVersionFn typed;
        };

        ProcCast proc{};
        proc.raw = GetProcAddress(module, "RtlGetVersion");
        return proc.typed;
    }();

    return fn;
}

bool query_rtl_version(VersionDescriptor* version_out, std::wstring* csd_out) {
    if (version_out == nullptr || csd_out == nullptr) {
        return false;
    }

    const auto rtl_get_version = resolve_rtl_get_version();
    if (rtl_get_version == nullptr) {
        return false;
    }

    RtlOsVersionInfoExW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtl_get_version(&info) < 0) {
        return false;
    }

    VersionDescriptor version{};
    version.major = info.dwMajorVersion;
    version.minor = info.dwMinorVersion;
    version.build = info.dwBuildNumber;
    version.platform_id = info.dwPlatformId;
    version.service_pack_major = info.wServicePackMajor;
    version.service_pack_minor = info.wServicePackMinor;
    version.suite_mask = info.wSuiteMask;
    version.product_type = info.wProductType;

    *version_out = version;
    *csd_out = std::wstring(info.szCSDVersion);
    return true;
}

bool fill_version_info_a(LPOSVERSIONINFOA info, const VersionDescriptor& version,
                         std::wstring_view csd_version) {
    if (info == nullptr) {
        return false;
    }

    const auto size = info->dwOSVersionInfoSize;
    if (!is_supported_version_info_size(size, false)) {
        return false;
    }

    ZeroMemory(info, size);
    info->dwOSVersionInfoSize = size;
    info->dwMajorVersion = version.major;
    info->dwMinorVersion = version.minor;
    info->dwBuildNumber = version.build;
    info->dwPlatformId = version.platform_id;

    const auto ansi_csd = narrow(csd_version);
    lstrcpynA(info->szCSDVersion, ansi_csd.c_str(), static_cast<int>(std::size(info->szCSDVersion)));

    if (size >= sizeof(OSVERSIONINFOEXA)) {
        auto* info_ex = reinterpret_cast<OSVERSIONINFOEXA*>(info);
        info_ex->wServicePackMajor = version.service_pack_major;
        info_ex->wServicePackMinor = version.service_pack_minor;
        info_ex->wSuiteMask = version.suite_mask;
        info_ex->wProductType = version.product_type;
    }

    return true;
}

bool fill_version_info_w(LPOSVERSIONINFOW info, const VersionDescriptor& version,
                         std::wstring_view csd_version) {
    if (info == nullptr) {
        return false;
    }

    const auto size = info->dwOSVersionInfoSize;
    if (!is_supported_version_info_size(size, true)) {
        return false;
    }

    ZeroMemory(info, size);
    info->dwOSVersionInfoSize = size;
    info->dwMajorVersion = version.major;
    info->dwMinorVersion = version.minor;
    info->dwBuildNumber = version.build;
    info->dwPlatformId = version.platform_id;

    const auto wide_csd = std::wstring(csd_version);
    lstrcpynW(info->szCSDVersion, wide_csd.c_str(), static_cast<int>(std::size(info->szCSDVersion)));

    if (size >= sizeof(OSVERSIONINFOEXW)) {
        auto* info_ex = reinterpret_cast<OSVERSIONINFOEXW*>(info);
        info_ex->wServicePackMajor = version.service_pack_major;
        info_ex->wServicePackMinor = version.service_pack_minor;
        info_ex->wSuiteMask = version.suite_mask;
        info_ex->wProductType = version.product_type;
    }

    return true;
}

std::string describe_version(const VersionDescriptor& version) {
    std::ostringstream oss;
    oss << version.major << '.' << version.minor << '.' << version.build
        << " platform=" << version.platform_id
        << " sp=" << version.service_pack_major << '.' << version.service_pack_minor
        << " suite=0x" << std::hex << version.suite_mask << std::dec
        << " product=" << static_cast<unsigned int>(version.product_type);
    return oss.str();
}

void queue_pending_log(std::string message) {
    std::scoped_lock lock(g_pending_log_mutex);
    const auto counters = enqueue_pending_log_counters(
        g_pending_log_count, g_pending_log_dropped,
        static_cast<std::uint32_t>(g_pending_log_messages.size()));
    if (g_pending_log_count < g_pending_log_messages.size()) {
        g_pending_log_messages[g_pending_log_count] = std::move(message);
    } else if (!g_pending_log_messages.empty()) {
        for (std::size_t i = 1; i < g_pending_log_messages.size(); ++i) {
            g_pending_log_messages[i - 1] = std::move(g_pending_log_messages[i]);
        }
        g_pending_log_messages.back() = std::move(message);
    }

    g_pending_log_count = counters.count;
    g_pending_log_dropped = counters.dropped;
}

void flush_pending_logs(bool emit_logs) {
    std::array<std::string, kMaxPendingLogEntries> messages{};
    std::uint32_t count = 0;
    std::uint32_t dropped = 0;

    {
        std::scoped_lock lock(g_pending_log_mutex);
        count = g_pending_log_count;
        dropped = g_pending_log_dropped;
        for (std::uint32_t i = 0; i < count; ++i) {
            messages[i] = std::move(g_pending_log_messages[i]);
            g_pending_log_messages[i].clear();
        }
        g_pending_log_count = 0;
        g_pending_log_dropped = 0;
    }

    if (!emit_logs) {
        return;
    }

    if (count != 0) {
        std::ostringstream oss;
        oss << "Flushing " << count << " buffered early GetVersionEx event(s)";
        logger::info(oss.str());
    }

    if (dropped != 0) {
        std::ostringstream oss;
        oss << "Dropped " << dropped
            << " buffered early GetVersionEx event(s) due to queue saturation";
        logger::info(oss.str());
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        logger::info(messages[i]);
    }
}

void log_version_result(const char* proc_name, const VersionDescriptor& reported,
                        bool reported_ok, const VersionDescriptor& chosen, bool used_rtl,
                        std::uint32_t info_size) {
    std::ostringstream oss;
    oss << proc_name << "(size=" << info_size << ") -> " << describe_version(chosen);
    if (reported_ok) {
        oss << ", reported=" << describe_version(reported);
    } else {
        oss << ", reported=<unavailable>";
    }
    oss << ", source=" << (used_rtl ? "RtlGetVersion" : "kernel32");
    oss << caller_suffix();

    if (g_runtime_logging_ready.load()) {
        if (g_log_version_calls.load()) {
            logger::info(oss.str());
        }
        return;
    }

    queue_pending_log(oss.str() + ", phase=prelogger");
}

bool install_import_hook(HMODULE game_module, const char* proc_name, void* replacement,
                         void** original_out, std::atomic<bool>& installed_flag) {
    if (installed_flag.load()) {
        return true;
    }

    void* original_target = nullptr;
    if (!iat_hook::patch_import(
            game_module, kKernel32ModuleName, proc_name, replacement, &original_target)) {
        return false;
    }

    if (original_out != nullptr && *original_out == nullptr) {
        *original_out = original_target;
    }

    installed_flag.store(true);
    return true;
}

BOOL handle_version_query_a(LPOSVERSIONINFOA info) {
    const auto original = g_original_get_version_ex_a;
    const auto size = info != nullptr ? info->dwOSVersionInfoSize : 0U;
    if (info == nullptr || !is_supported_version_info_size(size, false)) {
        return original != nullptr ? original(info) : FALSE;
    }

    VersionDescriptor reported{};
    bool reported_ok = false;
    if (original != nullptr && original(info)) {
        reported = descriptor_from_info_a(*info);
        reported_ok = true;
    }

    VersionDescriptor actual{};
    std::wstring actual_csd;
    if (!query_rtl_version(&actual, &actual_csd)) {
        log_version_result(kGetVersionExAProc, reported, reported_ok, reported, false, size);
        return reported_ok ? TRUE : FALSE;
    }

    const auto chosen = choose_version_descriptor(reported, actual);
    const auto used_rtl = !reported_ok || !descriptors_match(chosen, reported);
    if (used_rtl && !fill_version_info_a(info, chosen, actual_csd)) {
        return reported_ok ? TRUE : FALSE;
    }

    log_version_result(kGetVersionExAProc, reported, reported_ok, chosen, used_rtl, size);
    return TRUE;
}

BOOL handle_version_query_w(LPOSVERSIONINFOW info) {
    const auto original = g_original_get_version_ex_w;
    const auto size = info != nullptr ? info->dwOSVersionInfoSize : 0U;
    if (info == nullptr || !is_supported_version_info_size(size, true)) {
        return original != nullptr ? original(info) : FALSE;
    }

    VersionDescriptor reported{};
    bool reported_ok = false;
    if (original != nullptr && original(info)) {
        reported = descriptor_from_info_w(*info);
        reported_ok = true;
    }

    VersionDescriptor actual{};
    std::wstring actual_csd;
    if (!query_rtl_version(&actual, &actual_csd)) {
        log_version_result(kGetVersionExWProc, reported, reported_ok, reported, false, size);
        return reported_ok ? TRUE : FALSE;
    }

    const auto chosen = choose_version_descriptor(reported, actual);
    const auto used_rtl = !reported_ok || !descriptors_match(chosen, reported);
    if (used_rtl && !fill_version_info_w(info, chosen, actual_csd)) {
        return reported_ok ? TRUE : FALSE;
    }

    log_version_result(kGetVersionExWProc, reported, reported_ok, chosen, used_rtl, size);
    return TRUE;
}

}  // namespace

bool is_supported_version_info_size(std::uint32_t size, bool wide) {
    if (wide) {
        return size == sizeof(OSVERSIONINFOW) || size == sizeof(OSVERSIONINFOEXW);
    }

    return size == sizeof(OSVERSIONINFOA) || size == sizeof(OSVERSIONINFOEXA);
}

PendingLogCounters enqueue_pending_log_counters(std::uint32_t current_count,
                                                std::uint32_t current_dropped,
                                                std::uint32_t capacity) {
    PendingLogCounters counters{current_count, current_dropped};
    if (capacity == 0) {
        ++counters.dropped;
        return counters;
    }

    if (counters.count < capacity) {
        ++counters.count;
        return counters;
    }

    counters.count = capacity;
    ++counters.dropped;
    return counters;
}

VersionDescriptor choose_version_descriptor(const VersionDescriptor& reported,
                                           const VersionDescriptor& actual) {
    if (!is_valid_descriptor(actual)) {
        return reported;
    }

    if (!is_valid_descriptor(reported)) {
        return actual;
    }

    const auto triplet_cmp = compare_version_triplet(actual, reported);
    if (triplet_cmp > 0) {
        return actual;
    }
    if (triplet_cmp < 0) {
        return reported;
    }

    if (actual.platform_id == VER_PLATFORM_WIN32_NT &&
        reported.platform_id != VER_PLATFORM_WIN32_NT) {
        return actual;
    }

    return reported;
}

extern "C" BOOL WINAPI hooked_get_version_ex_a(LPOSVERSIONINFOA info) {
    return handle_version_query_a(info);
}

extern "C" BOOL WINAPI hooked_get_version_ex_w(LPOSVERSIONINFOW info) {
    return handle_version_query_w(info);
}

void install_early(HMODULE game_module) {
    if (game_module == nullptr) {
        return;
    }

    install_import_hook(
        game_module, kGetVersionExAProc, reinterpret_cast<void*>(&hooked_get_version_ex_a),
        reinterpret_cast<void**>(&g_original_get_version_ex_a), g_hooked_get_version_ex_a);
    install_import_hook(
        game_module, kGetVersionExWProc, reinterpret_cast<void*>(&hooked_get_version_ex_w),
        reinterpret_cast<void**>(&g_original_get_version_ex_w), g_hooked_get_version_ex_w);
}

bool finalize_install(HMODULE game_module, bool log_version_calls) {
    g_log_version_calls.store(log_version_calls);
    g_runtime_logging_ready.store(true);

    if (game_module == nullptr) {
        flush_pending_logs(log_version_calls);
        return false;
    }

    const auto hooked_a = install_import_hook(
        game_module, kGetVersionExAProc, reinterpret_cast<void*>(&hooked_get_version_ex_a),
        reinterpret_cast<void**>(&g_original_get_version_ex_a), g_hooked_get_version_ex_a);
    const auto hooked_w = install_import_hook(
        game_module, kGetVersionExWProc, reinterpret_cast<void*>(&hooked_get_version_ex_w),
        reinterpret_cast<void**>(&g_original_get_version_ex_w), g_hooked_get_version_ex_w);

    if (!hooked_a || !hooked_w) {
        flush_pending_logs(log_version_calls);
        logger::error("Failed to install GetVersionEx modernization hooks");
        return false;
    }

    flush_pending_logs(log_version_calls);

    std::ostringstream oss;
    oss << "Installed GetVersionEx modernization hooks (RtlGetVersion-backed, log_calls="
        << (log_version_calls ? "on" : "off") << ')';
    logger::info(oss.str());
    return true;
}

}  // namespace mh2modern::version
