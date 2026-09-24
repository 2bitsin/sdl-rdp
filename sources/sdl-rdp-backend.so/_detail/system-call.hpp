#pragma once
#include <string_view>

namespace Backend {
auto SystemCall(int result, std::string_view operation) -> int;
}
