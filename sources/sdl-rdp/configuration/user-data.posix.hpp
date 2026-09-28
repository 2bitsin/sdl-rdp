#pragma once
#include <filesystem>

namespace sdl_rdp::configuration::detail::user_data {
auto Home() -> std::filesystem::path;
}
