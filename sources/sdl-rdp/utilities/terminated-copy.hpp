#pragma once
#include <span>
#include <string_view>

namespace Backend {
auto CopyTerminated(std::span<char> field, std::string_view text) -> void;
}
