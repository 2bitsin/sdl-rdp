#pragma once
#include "filemode.hpp"
#include <sdl-rdp/SDL3/rdp/rendezvous.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/resources.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
namespace sdl3::rdp::storage::detail::drive {
using sdl3::rdp::sdl::Stream;

template <class VoidTy>
concept VoidBuffer = std::same_as<VoidTy, void> || std::same_as<VoidTy, void const>;
// The bytes a C transfer buffer holds: written from when const, read into otherwise.
template <VoidBuffer VoidTy> using BytesOf = std::conditional_t<std::is_const_v<VoidTy>, std::byte const, std::byte>;
template <class ByteTy>
concept ByteBuffer = std::same_as<std::remove_const_t<ByteTy>, std::byte>;
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
using detail::drive::ByteBuffer;
using detail::drive::BytesOf;
using detail::drive::DriveId;
using detail::drive::DriveName;
using detail::drive::OpenDriveFile;
using detail::drive::OpenFile;
using detail::drive::UpdateDrives;
using detail::drive::VoidBuffer;
}
