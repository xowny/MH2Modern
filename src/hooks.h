#pragma once

#include <windows.h>

namespace mh2modern::hooks {

void install_all(HMODULE plugin_module, DWORD main_thread_id);

}  // namespace mh2modern::hooks
