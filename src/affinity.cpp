#include "affinity.h"

#include "logger.h"

#include <sstream>

namespace mh2modern::affinity {
namespace {

constexpr std::uintptr_t kSetProcessAffinityCallRva = 0x2154F9;
constexpr std::uint8_t kExpectedCallBytes[6] = {0xFF, 0x15, 0xB4, 0x81, 0x63, 0x00};

bool g_log_affinity_patch = true;

bool is_single_bit_mask(std::uintptr_t mask) {
    return mask != 0 && (mask & (mask - 1)) == 0;
}

bool patch_callsite(std::uintptr_t address, void* replacement) {
    const auto replacement_rel =
        reinterpret_cast<std::intptr_t>(replacement) - static_cast<std::intptr_t>(address + 5);
    if (replacement_rel < INT32_MIN || replacement_rel > INT32_MAX) {
        return false;
    }

    auto* code = reinterpret_cast<std::uint8_t*>(address);
    DWORD old_protect{};
    if (!VirtualProtect(code, 6, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    code[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(code + 1) = static_cast<std::int32_t>(replacement_rel);
    code[5] = 0x90;

    DWORD restored_protect{};
    VirtualProtect(code, 6, old_protect, &restored_protect);
    FlushInstructionCache(GetCurrentProcess(), code, 6);
    return true;
}

}  // namespace

std::uintptr_t choose_affinity_mask(std::uintptr_t requested_mask, std::uintptr_t process_mask) {
    if (requested_mask == 1 && is_single_bit_mask(requested_mask) &&
        process_mask != 0 && !is_single_bit_mask(process_mask)) {
        return process_mask;
    }

    return requested_mask;
}

extern "C" BOOL WINAPI hooked_set_process_affinity_mask(HANDLE process,
                                                        DWORD_PTR requested_mask) {
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (!GetProcessAffinityMask(process, &process_mask, &system_mask)) {
        if (g_log_affinity_patch) {
            logger::error("GetProcessAffinityMask failed in affinity modernization hook");
        }
        return TRUE;
    }

    const auto effective_mask =
        static_cast<DWORD_PTR>(choose_affinity_mask(requested_mask, process_mask));
    if (g_log_affinity_patch) {
        std::ostringstream oss;
        oss << "Affinity patch requested mask=0x" << std::hex
            << static_cast<std::uintptr_t>(requested_mask)
            << ", process_mask=0x" << static_cast<std::uintptr_t>(process_mask)
            << ", system_mask=0x" << static_cast<std::uintptr_t>(system_mask)
            << ", effective_mask=0x" << static_cast<std::uintptr_t>(effective_mask)
            << std::dec;
        logger::info(oss.str());
    }

    if (effective_mask == requested_mask) {
        return ::SetProcessAffinityMask(process, requested_mask);
    }

    return ::SetProcessAffinityMask(process, effective_mask);
}

bool install_affinity_patch(HMODULE game_module, bool log_affinity_patch) {
    if (game_module == nullptr) {
        return false;
    }

    g_log_affinity_patch = log_affinity_patch;
    auto* code = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(game_module) + kSetProcessAffinityCallRva);
    for (std::size_t i = 0; i < std::size(kExpectedCallBytes); ++i) {
        if (code[i] != kExpectedCallBytes[i]) {
            std::ostringstream oss;
            oss << "Affinity patch preflight failed at Manhunt2.exe+0x" << std::hex
                << kSetProcessAffinityCallRva << std::dec;
            logger::error(oss.str());
            return false;
        }
    }

    if (!patch_callsite(reinterpret_cast<std::uintptr_t>(game_module) + kSetProcessAffinityCallRva,
                        reinterpret_cast<void*>(&hooked_set_process_affinity_mask))) {
        logger::error("Failed to patch startup SetProcessAffinityMask call");
        return false;
    }

    logger::info("Patched startup single-core affinity call in Manhunt2.exe");
    return true;
}

}  // namespace mh2modern::affinity
