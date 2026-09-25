#pragma once
#include <string_view>
#include <vector>

namespace sdl_rdp::headless_client_test::utilities::detail::split {
auto Split(std::string_view text, char separator) -> std::vector<std::string_view>;
}

namespace sdl_rdp::headless_client_test::utilities {
using detail::split::Split;
}
