#pragma once

#include <string_view>
#include <windows.h>

namespace mh2modern::logger {

void init(HMODULE module);
void info(std::string_view message);
void error(std::string_view message);

}  // namespace mh2modern::logger
