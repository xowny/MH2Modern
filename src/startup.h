#pragma once

#include <cstdint>
#include <string_view>
#include <windows.h>

namespace mh2modern::startup {

enum class EventKind {
    FileOpen,
    Read,
    DllLoad,
    WindowCreate,
};

struct EventCounters {
    std::uint32_t file_opens{0};
    std::uint32_t read_calls{0};
    std::uint64_t read_bytes{0};
    std::uint32_t dll_loads{0};
    std::uint32_t window_creates{0};
};

struct PendingDetailCounters {
    std::uint32_t count{0};
    std::uint32_t dropped{0};
};

enum class FileStackKind {
    None,
    ShaderBlob,
    GameText,
    GlobalTexture,
};

EventCounters observe_event(const EventCounters& current, EventKind kind, std::uint32_t amount = 0);
std::uint32_t max_duration_ms(std::uint32_t current_max, std::uint32_t candidate_ms);
PendingDetailCounters enqueue_pending_detail_counters(
    std::uint32_t count, std::uint32_t dropped, std::uint32_t max_count);
bool should_log_detail_event(bool log_startup_details, std::uint32_t slow_io_threshold_ms,
                             std::uint32_t duration_ms);
FileStackKind classify_file_stack_path(std::string_view path);

void install_early(HMODULE game_module);
bool finalize_install(HMODULE game_module, bool log_startup_details,
                      std::uint32_t slow_io_threshold_ms);

}  // namespace mh2modern::startup
