#pragma once
#include "filemode.hpp"
#include <sdl-rdp/SDL3/rdp/rendezvous.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/resources.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
namespace sdl3::rdp::storage::detail::drive {
using sdl3::rdp::sdl::Stream;

// An absent name selects the first shared drive; SDL's interfaces spell that as a null or empty name.
auto DriveName(char const* name)                                     -> std::optional<std::string>;
auto DriveId(Driver& driver, std::optional<std::string> const& name) -> std::uint32_t;
auto UpdateDrives(Driver& driver, SDL_PropertiesID properties)       -> void;
auto OpenDriveFile(std::shared_ptr<Driver> driver, std::uint32_t drive, std::string const& path, FileMode mode)
    -> Stream;
// SDL's display property publishes a C-compatible file factory returning SDL-owned streams.
auto SDLCALL OpenFile(char const* drive, char const* path, char const* mode) -> SDL_IOStream*;
}

namespace sdl3::rdp::storage {
using detail::drive::DriveId;
using detail::drive::DriveName;
using detail::drive::OpenDriveFile;
using detail::drive::OpenFile;
using detail::drive::UpdateDrives;
}
