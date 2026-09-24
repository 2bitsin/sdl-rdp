#pragma once
#include "SDL_rdpfilemode.hpp"
#include "SDL_rdprendezvous.hpp"
#include <optional>
#include <string>
namespace rdp {
template<typename _Byte>
concept IoBuffer = std::same_as<_Byte, void> || std::same_as<_Byte, void const>;
// An absent name selects the first shared drive; SDL's interfaces spell that as a null or empty name.
auto DriveName(char const* name) -> std::optional<std::string>;
auto                  DriveId(Driver const& driver, std::optional<std::string> const& name) -> unsigned;
void                  UpdateDrives(Driver const& driver, SDL_PropertiesID properties);
auto OpenDriveFile(std::shared_ptr<Driver const> driver, unsigned drive, std::string const& path, FileMode mode)
    -> Stream;
// SDL's display property publishes a C-compatible file factory returning SDL-owned streams.
SDL_IOStream* SDLCALL OpenFile(char const* drive, char const* path, char const* mode);
}
