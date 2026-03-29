#pragma once

#include <cstdint>
#include <windows.h>

namespace mh2modern::version {

struct VersionDescriptor {
    std::uint32_t major{0};
    std::uint32_t minor{0};
    std::uint32_t build{0};
    std::uint32_t platform_id{0};
    std::uint16_t service_pack_major{0};
    std::uint16_t service_pack_minor{0};
    std::uint16_t suite_mask{0};
    std::uint8_t product_type{0};
};

struct PendingLogCounters {
    std::uint32_t count{0};
    std::uint32_t dropped{0};
};

bool is_supported_version_info_size(std::uint32_t size, bool wide);
PendingLogCounters enqueue_pending_log_counters(std::uint32_t current_count,
                                                std::uint32_t current_dropped,
                                                std::uint32_t capacity);
VersionDescriptor choose_version_descriptor(const VersionDescriptor& reported,
                                           const VersionDescriptor& actual);

void install_early(HMODULE game_module);
bool finalize_install(HMODULE game_module, bool log_version_calls);

}  // namespace mh2modern::version
