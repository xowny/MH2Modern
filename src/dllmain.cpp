#include "hooks.h"
#include "startup.h"
#include "version.h"

#include <exception>
#include <windows.h>

namespace {

DWORD g_main_thread_id = 0;

DWORD WINAPI bootstrap_thread(void* module_ptr) {
    const auto module = static_cast<HMODULE>(module_ptr);

    try {
        mh2modern::hooks::install_all(module, g_main_thread_id);
    } catch (const std::exception&) {
        return 1;
    } catch (...) {
        return 1;
    }

    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_main_thread_id = GetCurrentThreadId();
        DisableThreadLibraryCalls(module);
        mh2modern::startup::install_early(GetModuleHandleW(nullptr));
        mh2modern::version::install_early(GetModuleHandleW(nullptr));
        const auto thread = CreateThread(nullptr, 0, bootstrap_thread, module, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }

    return TRUE;
}
