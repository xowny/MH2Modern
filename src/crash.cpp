#include "crash.h"

#include "logger.h"

#include <DbgHelp.h>

#include <filesystem>
#include <sstream>

#pragma comment(lib, "Dbghelp.lib")

namespace mh2modern::crash {
namespace {

std::filesystem::path g_dump_path;
SetUnhandledExceptionFilterFn g_original_set_unhandled_exception_filter = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_installed_filter = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_chained_filter = nullptr;
InvalidParameterHandler g_chained_invalid_parameter_handler = nullptr;
RuntimeCrashHandler g_chained_purecall_handler = nullptr;

using SetInvalidParameterHandlerFn = InvalidParameterHandler(__cdecl*)(InvalidParameterHandler);
using SetPurecallHandlerFn = RuntimeCrashHandler(__cdecl*)(RuntimeCrashHandler);

bool write_dump(EXCEPTION_POINTERS* exception_pointers, const char* context) {
    const auto file = CreateFileW(
        g_dump_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        logger::error("Failed to create crash dump file");
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exception_info{};
    exception_info.ThreadId = GetCurrentThreadId();
    exception_info.ExceptionPointers = exception_pointers;
    exception_info.ClientPointers = FALSE;

    const auto ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal,
        exception_pointers != nullptr ? &exception_info : nullptr, nullptr, nullptr);
    CloseHandle(file);

    std::ostringstream oss;
    if (ok) {
        oss << context << ": crash dump written to " << g_dump_path.string();
        logger::error(oss.str());
        return true;
    }

    oss << context << ": MiniDumpWriteDump failed";
    logger::error(oss.str());
    return false;
}

HMODULE resolve_crt_runtime_module() {
    constexpr const wchar_t* candidates[] = {
        L"ucrtbase.dll",
        L"api-ms-win-crt-runtime-l1-1-0.dll",
        L"msvcrt.dll",
    };

    for (const auto* candidate : candidates) {
        if (auto* module = GetModuleHandleW(candidate); module != nullptr) {
            return module;
        }
    }

    for (const auto* candidate : candidates) {
        if (auto* module = LoadLibraryW(candidate); module != nullptr) {
            return module;
        }
    }

    return nullptr;
}

void __cdecl invalid_parameter_handler(const wchar_t*, const wchar_t*, const wchar_t*,
                                       unsigned int, std::uintptr_t) {
    write_dump(nullptr, "CRT invalid parameter");
    if (should_chain_runtime_handler(
            reinterpret_cast<void*>(&invalid_parameter_handler),
            reinterpret_cast<void*>(g_chained_invalid_parameter_handler))) {
        logger::info("Forwarding invalid parameter to chained CRT handler");
        g_chained_invalid_parameter_handler(nullptr, nullptr, nullptr, 0U, 0U);
    }
}

void __cdecl purecall_handler() {
    write_dump(nullptr, "CRT purecall");
    if (should_chain_runtime_handler(
            reinterpret_cast<void*>(&purecall_handler),
            reinterpret_cast<void*>(g_chained_purecall_handler))) {
        logger::info("Forwarding purecall to chained CRT handler");
        g_chained_purecall_handler();
    }
}

void install_runtime_crash_handlers() {
    const auto runtime = resolve_crt_runtime_module();
    if (runtime == nullptr) {
        logger::info("CRT crash handler installation skipped: runtime module unavailable");
        return;
    }

    const auto set_invalid_parameter_handler =
        reinterpret_cast<SetInvalidParameterHandlerFn>(
            GetProcAddress(runtime, "_set_invalid_parameter_handler"));
    if (set_invalid_parameter_handler != nullptr) {
        g_chained_invalid_parameter_handler =
            set_invalid_parameter_handler(&invalid_parameter_handler);
        logger::info("Installed CRT invalid-parameter crash handler");
    } else {
        logger::info("CRT invalid-parameter handler API unavailable");
    }

    const auto set_purecall_handler =
        reinterpret_cast<SetPurecallHandlerFn>(GetProcAddress(runtime, "_set_purecall_handler"));
    if (set_purecall_handler != nullptr) {
        g_chained_purecall_handler = set_purecall_handler(&purecall_handler);
        logger::info("Installed CRT purecall crash handler");
    } else {
        logger::info("CRT purecall handler API unavailable");
    }
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exception_pointers) {
    write_dump(exception_pointers, "Unhandled exception");

    if (should_chain_exception_filter(g_installed_filter, g_chained_filter)) {
        logger::info("Forwarding exception to chained game crash filter");
        return g_chained_filter(exception_pointers);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

bool should_chain_exception_filter(LPTOP_LEVEL_EXCEPTION_FILTER current_handler,
                                   LPTOP_LEVEL_EXCEPTION_FILTER candidate) {
    return candidate != nullptr && candidate != current_handler;
}

bool should_chain_runtime_handler(void* current_handler, void* candidate) {
    return candidate != nullptr && candidate != current_handler;
}

void set_original_set_unhandled_exception_filter(SetUnhandledExceptionFilterFn fn) {
    g_original_set_unhandled_exception_filter = fn;
}

void install(HMODULE module) {
    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(module, module_path, MAX_PATH);
    g_dump_path = std::filesystem::path(module_path).replace_filename(L"MH2Modern.dmp");
    g_installed_filter = unhandled_exception_filter;
    g_chained_filter = SetUnhandledExceptionFilter(unhandled_exception_filter);
    logger::info("Crash dump handler installed");
    install_runtime_crash_handlers();
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI hooked_set_unhandled_exception_filter(
    LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    const auto current = g_installed_filter != nullptr ? g_installed_filter : unhandled_exception_filter;

    if (should_chain_exception_filter(current, filter)) {
        g_chained_filter = filter;
        logger::info("Preserved MH2Modern crash handler and chained game exception filter");
    } else if (filter == nullptr) {
        g_chained_filter = nullptr;
        logger::info("Blocked attempt to clear MH2Modern crash handler");
    } else {
        g_chained_filter = filter;
    }

    return current;
}

}  // namespace mh2modern::crash
