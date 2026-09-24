#pragma once
#include "SDL_rdpfilemode.hpp"
#include "SDL_rdprendezvous.hpp"
#include <cstdint>
#include <optional>
#include <string>
namespace rdp {
template <typename ByteTy>
concept IoBuffer = std::same_as<ByteTy, void> || std::same_as<ByteTy, void const>;
// An absent name selects the first shared drive; SDL's interfaces spell that as a null or empty name.
auto DriveName(char const* name)                                           -> std::optional<std::string>;
auto DriveId(Driver const& driver, std::optional<std::string> const& name) -> std::uint32_t;
auto UpdateDrives(Driver const& driver, SDL_PropertiesID properties)       -> void;
auto OpenDriveFile(std::shared_ptr<Driver const> driver, std::uint32_t drive, std::string const& path, FileMode mode)
    -> Stream;
// SDL's display property publishes a C-compatible file factory returning SDL-owned streams.
auto SDLCALL OpenFile(char const* drive, char const* path, char const* mode) -> SDL_IOStream*;
}
