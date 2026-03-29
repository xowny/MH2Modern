#pragma once

#include <cstdint>
#include <windows.h>

namespace mh2modern::affinity {

std::uintptr_t choose_affinity_mask(std::uintptr_t requested_mask, std::uintptr_t process_mask);
bool install_affinity_patch(HMODULE game_module, bool log_affinity_patch);

}  // namespace mh2modern::affinity
