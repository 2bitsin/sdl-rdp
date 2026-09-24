#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace SampleGate {
auto BuildRoot()                                                     -> std::filesystem::path;
auto Arguments(std::filesystem::path const& certificates, bool wait) -> std::vector<std::string>;
auto AnnouncedPort(std::string_view line)                            -> unsigned;
}
