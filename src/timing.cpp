#include "timing.h"

#include "logger.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <sstream>

namespace mh2modern::timing {
namespace {

std::atomic<std::uint64_t> g_frequency{};
std::atomic<std::uint64_t> g_start_counter{};
std::atomic<DWORD> g_main_thread_id{};
std::atomic<bool> g_sleep_spin_fix_enabled{};
std::atomic<bool> g_timer_logging_enabled{};
GetTickCountFn g_original_get_tick_count{};
SleepFn g_original_sleep{};

std::uint64_t read_counter() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<std::uint64_t>(value.QuadPart);
}

void busy_wait_until(std::uint64_t target_counter) {
    while (read_counter() < target_counter) {
        YieldProcessor();
    }
}

}  // namespace

std::uint32_t qpc_delta_to_milliseconds(std::uint64_t frequency, std::uint64_t start,
                                        std::uint64_t now) {
    if (frequency == 0 || now < start) {
        return 0;
    }

    const auto delta = now - start;
    const auto milliseconds = (delta * 1000ULL) / frequency;
    return static_cast<std::uint32_t>(milliseconds & 0xFFFFFFFFu);
}

void initialize() {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    g_frequency.store(static_cast<std::uint64_t>(frequency.QuadPart));
    g_start_counter.store(static_cast<std::uint64_t>(counter.QuadPart));
}

void set_original_get_tick_count(GetTickCountFn fn) {
    g_original_get_tick_count = fn;
}

void set_original_sleep(SleepFn fn) {
    g_original_sleep = fn;
}

void set_main_thread_id(DWORD thread_id) {
    g_main_thread_id.store(thread_id);
}

void set_sleep_spin_fix_enabled(bool enabled) {
    g_sleep_spin_fix_enabled.store(enabled);
}

void set_timer_logging_enabled(bool enabled) {
    g_timer_logging_enabled.store(enabled);
}

DWORD WINAPI hooked_get_tick_count() {
    const auto frequency = g_frequency.load();
    const auto start = g_start_counter.load();
    const auto current = read_counter();
    const auto value = qpc_delta_to_milliseconds(frequency, start, current);

    if (g_timer_logging_enabled.load()) {
        std::ostringstream oss;
        oss << "GetTickCount -> " << value;
        logger::info(oss.str());
    }

    return value;
}

VOID WINAPI hooked_sleep(DWORD milliseconds) {
    const auto original_sleep = g_original_sleep;
    if (!g_sleep_spin_fix_enabled.load() || original_sleep == nullptr) {
        if (original_sleep != nullptr) {
            original_sleep(milliseconds);
        }
        return;
    }

    const auto main_thread_id = g_main_thread_id.load();
    const auto current_thread_id = GetCurrentThreadId();
    if (current_thread_id != main_thread_id || milliseconds == 0 || milliseconds > 4) {
        original_sleep(milliseconds);
        return;
    }

    const auto frequency = g_frequency.load();
    const auto target_counter = read_counter() + ((frequency * milliseconds) / 1000ULL);
    busy_wait_until(target_counter);
}

}  // namespace mh2modern::timing
