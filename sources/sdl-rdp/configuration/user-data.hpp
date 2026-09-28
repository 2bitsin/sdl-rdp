#pragma once
#include <filesystem>

namespace sdl_rdp::configuration::detail::user_data {
// Where sdl-rdp keeps the current user's files, by the platform's convention; nothing is created.
auto UserDataDirectory() -> std::filesystem::path;
}

namespace sdl_rdp::configuration {
using detail::user_data::UserDataDirectory;
}
