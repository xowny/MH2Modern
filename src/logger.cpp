#include "logger.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace mh2modern::logger {
namespace {

std::mutex g_log_mutex;
std::filesystem::path g_log_path;

std::string timestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << st.wYear << '-' << std::setw(2) << st.wMonth << '-'
        << std::setw(2) << st.wDay << ' ' << std::setw(2) << st.wHour << ':' << std::setw(2)
        << st.wMinute << ':' << std::setw(2) << st.wSecond;
    return oss.str();
}

void write_line(std::string_view level, std::string_view message) {
    std::scoped_lock lock(g_log_mutex);
    if (g_log_path.empty()) {
        return;
    }

    std::ofstream file(g_log_path, std::ios::app);
    if (!file) {
        return;
    }

    file << '[' << timestamp() << "] [" << level << "] " << message << '\n';
}

}  // namespace

void init(HMODULE module) {
    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(module, module_path, MAX_PATH);
    g_log_path = std::filesystem::path(module_path).replace_filename(L"MH2Modern.log");
    write_line("INFO", "Logger initialized");
}

void info(std::string_view message) {
    write_line("INFO", message);
}

void error(std::string_view message) {
    write_line("ERROR", message);
}

}  // namespace mh2modern::logger
