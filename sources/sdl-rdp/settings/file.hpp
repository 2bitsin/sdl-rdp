#pragma once
#include <sdl-rdp/settings/settings.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::settings::detail::file {
auto Extensions()                                                           -> std::vector<std::string_view>;
auto SettingsName(std::filesystem::path const& library)                     -> std::string;
auto Located(std::filesystem::path const& directory, std::string_view name) -> std::optional<std::filesystem::path>;
auto Load(std::filesystem::path const& path)                                -> Settings;
}

namespace sdl_rdp::settings {
using detail::file::Extensions;
using detail::file::Load;
using detail::file::Located;
using detail::file::SettingsName;
}
