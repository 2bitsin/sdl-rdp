#pragma once
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SampleGate {
using Words = std::vector<std::string>;
using Hint  = std::pair<char const*, char const*>;

auto BuildRoot()                                                -> std::filesystem::path;
auto BackendLibrary()                                           -> std::filesystem::path;
auto SetBackendHints(std::filesystem::path const& certificates) -> bool;
auto SetLoopbackHints(std::filesystem::path const& certificates, std::initializer_list<Hint> hints) -> bool;
auto Arguments(std::filesystem::path const& certificates, Words const& environment = { }, Words const& options = { })
    -> Words;
auto AnnouncedPort(std::string_view line)                       -> std::uint32_t;
auto PrimaryDisplayPort()                                       -> std::uint32_t;
auto AspectOptions()                                            -> Words;
}
