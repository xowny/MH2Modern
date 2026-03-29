#include "iat_hook.h"

#include <cstdint>
#include <cstring>

namespace mh2modern::iat_hook {

bool patch_import(HMODULE module, const char* imported_module_name, const char* proc_name,
                  void* replacement, void** original_out) {
    if (module == nullptr || imported_module_name == nullptr || proc_name == nullptr || replacement == nullptr) {
        return false;
    }

    auto* base = reinterpret_cast<std::uint8_t*>(module);
    const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    const auto* nt_headers =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const auto& import_directory =
        nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (import_directory.VirtualAddress == 0) {
        return false;
    }

    auto* import_descriptor =
        reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + import_directory.VirtualAddress);
    for (; import_descriptor->Name != 0; ++import_descriptor) {
        const auto* module_name = reinterpret_cast<const char*>(base + import_descriptor->Name);
        if (_stricmp(module_name, imported_module_name) != 0) {
            continue;
        }

        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import_descriptor->FirstThunk);
        auto* original_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (import_descriptor->OriginalFirstThunk != 0 ? import_descriptor->OriginalFirstThunk
                                                               : import_descriptor->FirstThunk));

        for (; original_thunk->u1.AddressOfData != 0; ++original_thunk, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(original_thunk->u1.Ordinal)) {
                continue;
            }

            const auto* import_by_name =
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + original_thunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import_by_name->Name), proc_name) != 0) {
                continue;
            }

            DWORD old_protect{};
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old_protect)) {
                return false;
            }

            if (original_out != nullptr) {
                *original_out = reinterpret_cast<void*>(thunk->u1.Function);
            }
            thunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);

            DWORD restored_protect{};
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old_protect, &restored_protect);
            FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
            return true;
        }
    }

    return false;
}

}  // namespace mh2modern::iat_hook
