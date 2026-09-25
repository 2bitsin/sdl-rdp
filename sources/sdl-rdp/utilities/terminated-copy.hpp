#pragma once
#include <span>
#include <string_view>

namespace sdl_rdp::utilities::detail::terminated_copy {
auto CopyTerminated(std::span<char> field, std::string_view text) -> void;
}

namespace sdl_rdp::utilities {
using detail::terminated_copy::CopyTerminated;
}
