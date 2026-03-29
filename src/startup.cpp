#include "startup.h"

#include "iat_hook.h"
#include "logger.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace mh2modern::startup {
namespace {

constexpr const char* kKernel32ModuleName = "KERNEL32.dll";
constexpr const char* kUser32ModuleName = "USER32.dll";
constexpr const char* kCreateFileAProc = "CreateFileA";
constexpr const char* kCreateFileWProc = "CreateFileW";
constexpr const char* kReadFileProc = "ReadFile";
constexpr const char* kLoadLibraryAProc = "LoadLibraryA";
constexpr const char* kLoadLibraryWProc = "LoadLibraryW";
constexpr const char* kCreateWindowExAProc = "CreateWindowExA";
constexpr const char* kCreateWindowExWProc = "CreateWindowExW";
constexpr std::size_t kMaxPendingDetailEntries = 32;

using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using LoadLibraryAFn = HMODULE(WINAPI*)(LPCSTR);
using LoadLibraryWFn = HMODULE(WINAPI*)(LPCWSTR);
using CreateWindowExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU,
                                        HINSTANCE, LPVOID);
using CreateWindowExWFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND,
                                        HMENU, HINSTANCE, LPVOID);

CreateFileAFn g_original_create_file_a = nullptr;
CreateFileWFn g_original_create_file_w = nullptr;
ReadFileFn g_original_read_file = nullptr;
LoadLibraryAFn g_original_load_library_a = nullptr;
LoadLibraryWFn g_original_load_library_w = nullptr;
CreateWindowExAFn g_original_create_window_ex_a = nullptr;
CreateWindowExWFn g_original_create_window_ex_w = nullptr;

std::atomic<bool> g_hooked_create_file_a{false};
std::atomic<bool> g_hooked_create_file_w{false};
std::atomic<bool> g_hooked_read_file{false};
std::atomic<bool> g_hooked_load_library_a{false};
std::atomic<bool> g_hooked_load_library_w{false};
std::atomic<bool> g_hooked_create_window_ex_a{false};
std::atomic<bool> g_hooked_create_window_ex_w{false};

std::atomic<bool> g_logging_ready{false};
std::atomic<bool> g_log_startup_details{true};
std::atomic<std::uint32_t> g_slow_io_threshold_ms{3};
std::atomic<std::uint64_t> g_frequency{};
std::atomic<std::uint64_t> g_start_counter{};
std::atomic<std::uint32_t> g_file_open_count{};
std::atomic<std::uint32_t> g_read_call_count{};
std::atomic<std::uint64_t> g_total_read_bytes{};
std::atomic<std::uint32_t> g_dll_load_count{};
std::atomic<std::uint32_t> g_window_create_count{};
std::atomic<std::uint32_t> g_max_file_open_duration_ms{};
std::atomic<std::uint32_t> g_max_load_library_duration_ms{};
std::atomic<bool> g_first_window_seen{false};
std::atomic<bool> g_summary_logged{false};
std::atomic<std::uint32_t> g_first_window_elapsed_ms{};
std::atomic<bool> g_logged_shader_stack{false};
std::atomic<bool> g_logged_gxt_stack{false};
std::atomic<bool> g_logged_global_texture_stack{false};

std::mutex g_detail_mutex;
std::string g_slowest_file_path;
std::string g_slowest_dll_name;
std::string g_first_window_class_name;
std::string g_first_window_title;
std::array<std::string, kMaxPendingDetailEntries> g_pending_detail_messages{};
std::uint32_t g_pending_detail_count = 0;
std::uint32_t g_pending_detail_dropped = 0;

std::uint64_t read_counter() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<std::uint64_t>(counter.QuadPart);
}

void initialize_clock() {
    if (g_frequency.load() == 0) {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        g_frequency.store(static_cast<std::uint64_t>(frequency.QuadPart));
    }

    if (g_start_counter.load() == 0) {
        g_start_counter.store(read_counter());
    }
}

std::uint32_t counter_delta_to_ms(std::uint64_t start, std::uint64_t end) {
    const auto frequency = g_frequency.load();
    if (frequency == 0 || end < start) {
        return 0;
    }

    return static_cast<std::uint32_t>(((end - start) * 1000ULL) / frequency);
}

std::uint32_t elapsed_since_start_ms() {
    return counter_delta_to_ms(g_start_counter.load(), read_counter());
}

std::string filename_only(const char* path) {
    if (path == nullptr || *path == '\0') {
        return {};
    }

    std::string value(path);
    const auto slash = value.find_last_of("\\/");
    return slash == std::string::npos ? value : value.substr(slash + 1);
}

std::string narrow(LPCWSTR value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }

    const auto size = WideCharToMultiByte(CP_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, value, -1, result.data(), size, nullptr, nullptr);
    return result;
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

std::string caller_suffix(std::uint16_t skip_frames = 1, std::uint16_t max_frames = 4) {
    void* frames[16]{};
    if (max_frames > static_cast<std::uint16_t>(std::size(frames))) {
        max_frames = static_cast<std::uint16_t>(std::size(frames));
    }

    const auto captured = CaptureStackBackTrace(skip_frames, max_frames, frames, nullptr);
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

bool ends_with_ascii_insensitive(std::string_view value, std::string_view suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }

    return equals_ascii_insensitive(
        value.substr(value.size() - suffix.size()), suffix);
}

bool should_log_dll_stack(std::string_view library_name) {
    const auto name = filename_only(std::string(library_name).c_str());
    return equals_ascii_insensitive(name, "user32.dll") ||
           equals_ascii_insensitive(name, "d3d9.dll");
}

void queue_or_log_detail(std::string message);

const char* file_stack_kind_label(FileStackKind kind) {
    switch (kind) {
    case FileStackKind::ShaderBlob:
        return "shader blob";
    case FileStackKind::GameText:
        return "game text";
    case FileStackKind::GlobalTexture:
        return "global texture";
    case FileStackKind::None:
    default:
        return nullptr;
    }
}

bool should_emit_file_stack_once(FileStackKind kind) {
    bool expected = false;
    switch (kind) {
    case FileStackKind::ShaderBlob:
        return g_logged_shader_stack.compare_exchange_strong(expected, true);
    case FileStackKind::GameText:
        return g_logged_gxt_stack.compare_exchange_strong(expected, true);
    case FileStackKind::GlobalTexture:
        return g_logged_global_texture_stack.compare_exchange_strong(expected, true);
    case FileStackKind::None:
    default:
        return false;
    }
}

void maybe_log_file_stack(std::string_view path) {
    if (!g_log_startup_details.load()) {
        return;
    }

    const auto kind = classify_file_stack_path(path);
    if (kind == FileStackKind::None || !should_emit_file_stack_once(kind)) {
        return;
    }

    const auto* label = file_stack_kind_label(kind);
    if (label == nullptr) {
        return;
    }

    std::ostringstream oss;
    oss << "Startup file open stack for " << label << ": " << path << caller_suffix(3, 8);
    queue_or_log_detail(oss.str());
}

void update_slowest_detail(std::atomic<std::uint32_t>& current_max, std::string* stored_value,
                           std::string new_value, std::uint32_t candidate_ms) {
    const auto updated_max = max_duration_ms(current_max.load(), candidate_ms);
    current_max.store(updated_max);
    if (candidate_ms < updated_max || stored_value == nullptr) {
        return;
    }

    std::scoped_lock lock(g_detail_mutex);
    if (candidate_ms == current_max.load()) {
        *stored_value = std::move(new_value);
    }
}

void flush_pending_detail_logs() {
    std::array<std::string, kMaxPendingDetailEntries> pending{};
    std::uint32_t count = 0;
    std::uint32_t dropped = 0;
    {
        std::scoped_lock lock(g_detail_mutex);
        count = g_pending_detail_count;
        dropped = g_pending_detail_dropped;
        for (std::uint32_t i = 0; i < count; ++i) {
            pending[i] = g_pending_detail_messages[i];
        }
        g_pending_detail_count = 0;
        g_pending_detail_dropped = 0;
    }

    if (count == 0 && dropped == 0) {
        return;
    }

    std::ostringstream oss;
    oss << "Flushing " << count << " buffered startup detail event(s)";
    if (dropped != 0) {
        oss << " (" << dropped << " dropped)";
    }
    logger::info(oss.str());

    for (std::uint32_t i = 0; i < count; ++i) {
        logger::info(pending[i]);
    }
}

void queue_or_log_detail(std::string message) {
    if (!g_log_startup_details.load()) {
        return;
    }

    if (g_logging_ready.load()) {
        logger::info(message);
        return;
    }

    std::scoped_lock lock(g_detail_mutex);
    const auto counters = enqueue_pending_detail_counters(
        g_pending_detail_count, g_pending_detail_dropped,
        static_cast<std::uint32_t>(g_pending_detail_messages.size()));
    if (counters.count == g_pending_detail_count) {
        g_pending_detail_dropped = counters.dropped;
        return;
    }

    g_pending_detail_messages[g_pending_detail_count] = std::move(message);
    g_pending_detail_count = counters.count;
    g_pending_detail_dropped = counters.dropped;
}

void log_detail_if_needed(const char* label, std::string_view value, std::uint32_t duration_ms) {
    if (!should_log_detail_event(
            g_log_startup_details.load(), g_slow_io_threshold_ms.load(), duration_ms)) {
        return;
    }

    std::ostringstream oss;
    oss << "Startup " << label << " took " << duration_ms << " ms";
    if (!value.empty()) {
        oss << ": " << value;
    }
    queue_or_log_detail(oss.str());
}

void log_read_detail_if_needed(std::uint32_t amount) {
    if (!g_log_startup_details.load() || g_slow_io_threshold_ms.load() != 0) {
        return;
    }

    std::ostringstream oss;
    oss << "Startup read completed: " << amount << " bytes";
    queue_or_log_detail(oss.str());
}

bool install_import_hook(HMODULE game_module, const char* module_name, const char* proc_name,
                         void* replacement, void** original_out, std::atomic<bool>& flag) {
    if (game_module == nullptr) {
        return false;
    }

    if (flag.load()) {
        return true;
    }

    void* original = nullptr;
    if (!iat_hook::patch_import(game_module, module_name, proc_name, replacement, &original)) {
        return false;
    }

    if (original_out != nullptr && *original_out == nullptr) {
        *original_out = original;
    }

    flag.store(true);
    return true;
}

void maybe_log_startup_summary() {
    if (!g_logging_ready.load() || !g_first_window_seen.load()) {
        return;
    }

    bool expected = false;
    if (!g_summary_logged.compare_exchange_strong(expected, true)) {
        return;
    }

    std::string class_name;
    std::string title;
    std::string slowest_file;
    std::string slowest_dll;
    {
        std::scoped_lock lock(g_detail_mutex);
        class_name = g_first_window_class_name;
        title = g_first_window_title;
        slowest_file = g_slowest_file_path;
        slowest_dll = g_slowest_dll_name;
    }

    std::ostringstream oss;
    oss << "Startup summary before first window: "
        << g_first_window_elapsed_ms.load() << " ms"
        << ", file_opens=" << g_file_open_count.load()
        << ", read_calls=" << g_read_call_count.load()
        << ", read_bytes=" << g_total_read_bytes.load()
        << ", dll_loads=" << g_dll_load_count.load()
        << ", class=" << (class_name.empty() ? "<unknown>" : class_name)
        << ", title=" << (title.empty() ? "<untitled>" : title)
        << ", slowest_file_open_ms=" << g_max_file_open_duration_ms.load()
        << ", slowest_dll_load_ms=" << g_max_load_library_duration_ms.load();
    logger::info(oss.str());

    if (!slowest_file.empty()) {
        logger::info("Startup slowest file open: " + slowest_file);
    }
    if (!slowest_dll.empty()) {
        logger::info("Startup slowest dll load: " + slowest_dll);
    }
}

void record_first_window(LPCSTR class_name, LPCSTR window_name) {
    bool expected = false;
    if (!g_first_window_seen.compare_exchange_strong(expected, true)) {
        return;
    }

    g_first_window_elapsed_ms.store(elapsed_since_start_ms());
    {
        std::scoped_lock lock(g_detail_mutex);
        g_first_window_class_name = class_name != nullptr ? class_name : "";
        g_first_window_title = window_name != nullptr ? window_name : "";
    }
    if (g_logging_ready.load()) {
        logger::info("Startup first window call" + caller_suffix(2));
    } else {
        queue_or_log_detail("Startup first window call" + caller_suffix(2));
    }
    maybe_log_startup_summary();
}

void record_first_window(LPCWSTR class_name, LPCWSTR window_name) {
    bool expected = false;
    if (!g_first_window_seen.compare_exchange_strong(expected, true)) {
        return;
    }

    g_first_window_elapsed_ms.store(elapsed_since_start_ms());
    {
        std::scoped_lock lock(g_detail_mutex);
        g_first_window_class_name = narrow(class_name);
        g_first_window_title = narrow(window_name);
    }
    if (g_logging_ready.load()) {
        logger::info("Startup first window call" + caller_suffix(2));
    } else {
        queue_or_log_detail("Startup first window call" + caller_suffix(2));
    }
    maybe_log_startup_summary();
}

}  // namespace

EventCounters observe_event(const EventCounters& current, EventKind kind, std::uint32_t amount) {
    EventCounters updated = current;
    switch (kind) {
    case EventKind::FileOpen:
        ++updated.file_opens;
        break;
    case EventKind::Read:
        ++updated.read_calls;
        updated.read_bytes += amount;
        break;
    case EventKind::DllLoad:
        ++updated.dll_loads;
        break;
    case EventKind::WindowCreate:
        ++updated.window_creates;
        break;
    }
    return updated;
}

std::uint32_t max_duration_ms(std::uint32_t current_max, std::uint32_t candidate_ms) {
    return candidate_ms > current_max ? candidate_ms : current_max;
}

PendingDetailCounters enqueue_pending_detail_counters(
    std::uint32_t count, std::uint32_t dropped, std::uint32_t max_count) {
    PendingDetailCounters counters{};
    if (count >= max_count) {
        counters.count = max_count;
        counters.dropped = dropped + 1;
        return counters;
    }

    counters.count = count + 1;
    counters.dropped = dropped;
    return counters;
}

bool should_log_detail_event(bool log_startup_details, std::uint32_t slow_io_threshold_ms,
                             std::uint32_t duration_ms) {
    return log_startup_details && duration_ms >= slow_io_threshold_ms;
}

FileStackKind classify_file_stack_path(std::string_view path) {
    if (path.empty()) {
        return FileStackKind::None;
    }

    const auto name = filename_only(std::string(path).c_str());
    if (ends_with_ascii_insensitive(name, ".fxo")) {
        return FileStackKind::ShaderBlob;
    }
    if (equals_ascii_insensitive(name, "GAME.GXT")) {
        return FileStackKind::GameText;
    }
    if (equals_ascii_insensitive(name, "gmodelspc.tex")) {
        return FileStackKind::GlobalTexture;
    }

    return FileStackKind::None;
}

extern "C" HANDLE WINAPI hooked_create_file_a(LPCSTR file_name, DWORD desired_access,
                                              DWORD share_mode,
                                              LPSECURITY_ATTRIBUTES security_attributes,
                                              DWORD creation_disposition, DWORD flags_and_attributes,
                                              HANDLE template_file) {
    const auto start = read_counter();
    const auto handle = g_original_create_file_a != nullptr
                            ? g_original_create_file_a(file_name, desired_access, share_mode,
                                                       security_attributes, creation_disposition,
                                                       flags_and_attributes, template_file)
                            : INVALID_HANDLE_VALUE;
    const auto duration_ms = counter_delta_to_ms(start, read_counter());

    g_file_open_count.fetch_add(1);
    update_slowest_detail(
        g_max_file_open_duration_ms, &g_slowest_file_path, file_name != nullptr ? file_name : "",
        duration_ms);
    log_detail_if_needed("file open", file_name != nullptr ? file_name : "", duration_ms);
    maybe_log_file_stack(file_name != nullptr ? file_name : "");
    return handle;
}

extern "C" HANDLE WINAPI hooked_create_file_w(LPCWSTR file_name, DWORD desired_access,
                                              DWORD share_mode,
                                              LPSECURITY_ATTRIBUTES security_attributes,
                                              DWORD creation_disposition, DWORD flags_and_attributes,
                                              HANDLE template_file) {
    const auto start = read_counter();
    const auto handle = g_original_create_file_w != nullptr
                            ? g_original_create_file_w(file_name, desired_access, share_mode,
                                                       security_attributes, creation_disposition,
                                                       flags_and_attributes, template_file)
                            : INVALID_HANDLE_VALUE;
    const auto duration_ms = counter_delta_to_ms(start, read_counter());
    const auto path = narrow(file_name);

    g_file_open_count.fetch_add(1);
    update_slowest_detail(g_max_file_open_duration_ms, &g_slowest_file_path, path, duration_ms);
    log_detail_if_needed("file open", path, duration_ms);
    maybe_log_file_stack(path);
    return handle;
}

extern "C" BOOL WINAPI hooked_read_file(HANDLE file, LPVOID buffer, DWORD bytes_to_read,
                                        LPDWORD bytes_read, LPOVERLAPPED overlapped) {
    const auto ok = g_original_read_file != nullptr
                        ? g_original_read_file(file, buffer, bytes_to_read, bytes_read, overlapped)
                        : FALSE;
    g_read_call_count.fetch_add(1);
    if (ok && bytes_read != nullptr) {
        g_total_read_bytes.fetch_add(*bytes_read);
        log_read_detail_if_needed(*bytes_read);
    }
    return ok;
}

extern "C" HMODULE WINAPI hooked_load_library_a(LPCSTR library_name) {
    const auto start = read_counter();
    const auto module = g_original_load_library_a != nullptr ? g_original_load_library_a(library_name)
                                                             : nullptr;
    const auto duration_ms = counter_delta_to_ms(start, read_counter());
    const auto name = library_name != nullptr ? std::string(library_name) : std::string{};

    g_dll_load_count.fetch_add(1);
    update_slowest_detail(
        g_max_load_library_duration_ms, &g_slowest_dll_name, name, duration_ms);
    log_detail_if_needed("dll load", name, duration_ms);
    if (g_log_startup_details.load() && should_log_dll_stack(name)) {
        queue_or_log_detail("Startup dll load stack for " + name + caller_suffix(2));
    }
    return module;
}

extern "C" HMODULE WINAPI hooked_load_library_w(LPCWSTR library_name) {
    const auto start = read_counter();
    const auto module = g_original_load_library_w != nullptr ? g_original_load_library_w(library_name)
                                                             : nullptr;
    const auto duration_ms = counter_delta_to_ms(start, read_counter());
    const auto name = narrow(library_name);

    g_dll_load_count.fetch_add(1);
    update_slowest_detail(g_max_load_library_duration_ms, &g_slowest_dll_name, name, duration_ms);
    log_detail_if_needed("dll load", name, duration_ms);
    if (g_log_startup_details.load() && should_log_dll_stack(name)) {
        queue_or_log_detail("Startup dll load stack for " + name + caller_suffix(2));
    }
    return module;
}

extern "C" HWND WINAPI hooked_create_window_ex_a(DWORD ex_style, LPCSTR class_name, LPCSTR window_name,
                                                 DWORD style, int x, int y, int width, int height,
                                                 HWND parent, HMENU menu, HINSTANCE instance,
                                                 LPVOID param) {
    const auto window = g_original_create_window_ex_a != nullptr
                            ? g_original_create_window_ex_a(ex_style, class_name, window_name, style,
                                                            x, y, width, height, parent, menu,
                                                            instance, param)
                            : nullptr;
    g_window_create_count.fetch_add(1);
    record_first_window(class_name, window_name);
    return window;
}

extern "C" HWND WINAPI hooked_create_window_ex_w(DWORD ex_style, LPCWSTR class_name, LPCWSTR window_name,
                                                 DWORD style, int x, int y, int width, int height,
                                                 HWND parent, HMENU menu, HINSTANCE instance,
                                                 LPVOID param) {
    const auto window = g_original_create_window_ex_w != nullptr
                            ? g_original_create_window_ex_w(ex_style, class_name, window_name, style,
                                                            x, y, width, height, parent, menu,
                                                            instance, param)
                            : nullptr;
    g_window_create_count.fetch_add(1);
    record_first_window(class_name, window_name);
    return window;
}

void install_early(HMODULE game_module) {
    initialize_clock();
    install_import_hook(
        game_module, kKernel32ModuleName, kCreateFileAProc,
        reinterpret_cast<void*>(&hooked_create_file_a),
        reinterpret_cast<void**>(&g_original_create_file_a), g_hooked_create_file_a);
    install_import_hook(
        game_module, kKernel32ModuleName, kCreateFileWProc,
        reinterpret_cast<void*>(&hooked_create_file_w),
        reinterpret_cast<void**>(&g_original_create_file_w), g_hooked_create_file_w);
    install_import_hook(
        game_module, kKernel32ModuleName, kReadFileProc, reinterpret_cast<void*>(&hooked_read_file),
        reinterpret_cast<void**>(&g_original_read_file), g_hooked_read_file);
    install_import_hook(
        game_module, kKernel32ModuleName, kLoadLibraryAProc,
        reinterpret_cast<void*>(&hooked_load_library_a),
        reinterpret_cast<void**>(&g_original_load_library_a), g_hooked_load_library_a);
    install_import_hook(
        game_module, kKernel32ModuleName, kLoadLibraryWProc,
        reinterpret_cast<void*>(&hooked_load_library_w),
        reinterpret_cast<void**>(&g_original_load_library_w), g_hooked_load_library_w);
    install_import_hook(
        game_module, kUser32ModuleName, kCreateWindowExAProc,
        reinterpret_cast<void*>(&hooked_create_window_ex_a),
        reinterpret_cast<void**>(&g_original_create_window_ex_a), g_hooked_create_window_ex_a);
    install_import_hook(
        game_module, kUser32ModuleName, kCreateWindowExWProc,
        reinterpret_cast<void*>(&hooked_create_window_ex_w),
        reinterpret_cast<void**>(&g_original_create_window_ex_w), g_hooked_create_window_ex_w);
}

bool finalize_install(HMODULE game_module, bool log_startup_details,
                      std::uint32_t slow_io_threshold_ms) {
    g_log_startup_details.store(log_startup_details);
    g_slow_io_threshold_ms.store(slow_io_threshold_ms);
    g_logging_ready.store(true);

    install_early(game_module);

    std::ostringstream oss;
    oss << "Installed startup profiler (detail_logs="
        << (log_startup_details ? "on" : "off")
        << ", slow_io_threshold_ms=" << slow_io_threshold_ms << ')';
    logger::info(oss.str());

    flush_pending_detail_logs();
    maybe_log_startup_summary();
    return true;
}

}  // namespace mh2modern::startup
