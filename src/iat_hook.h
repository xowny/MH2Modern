#pragma once

#include <windows.h>

namespace mh2modern::iat_hook {

bool patch_import(HMODULE module, const char* imported_module_name, const char* proc_name,
                  void* replacement, void** original_out);

}  // namespace mh2modern::iat_hook
