#include <sdl-rdp/drive/files.hpp>

#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/drive/information.hpp>
#include <sdl-rdp/drive/listing.hpp>
#include <sdl-rdp/freerdp-facade/rdpdr.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace sdl_rdp::drive::detail::files {
using sdl_rdp::freerdp_facade::AccessMask;
using sdl_rdp::freerdp_facade::IrpMajor;
using sdl_rdp::freerdp_facade::IrpMinor;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

namespace {
auto Query(sdlrdp_file& directory, bool first, std::span<std::byte const> pattern) -> DrivePacket {
  return Exchange(directory, IrpMajor::DirectoryControl, DirectoryQuery(first, pattern), IrpMinor::QueryDirectory,
                  true);
}
auto RenameBody(std::string_view destination) -> DrivePacket {
  auto target = DrivePath(destination);
  Expects(target.size() >= sizeof(char16_t), "a drive path ends in its terminator");
  target.resize(target.size() - sizeof(char16_t));
  DrivePacket body;
  body.Write(std::uint8_t{ 0 });
  body.Write(std::uint8_t{ 0 });
  body.Write(Narrowed<std::uint32_t>(target.size()));
  body.Append(target);
  return body;
}
}
DriveFiles::DriveFiles(std::shared_ptr<DriveChannel> channel) noexcept : _channel{ std::move(channel) } { }
auto DriveFiles::List(std::span<sdlrdp_drive> out) const -> int {
  return _channel ? _channel->List(out) : 0;
}
auto DriveFiles::Open(std::uint32_t drive, std::string_view path, std::uint32_t flags) const
    -> std::unique_ptr<sdlrdp_file> {
  auto const kind = flags & SDLRDP_FILE_DIRECTORY ? FileKind::Directory : FileKind::File;
  return Opened(drive, path, { flags, kind });
}
auto DriveFiles::Attached(sdlrdp_file& file) const -> sdlrdp_file& {
  if (Channel() != file.Channel()) throw PeerDisconnected{ file.Path() };
  return file;
}
auto DriveFiles::Stat(std::uint32_t drive, std::string_view path) const -> sdlrdp_stat {
  auto const file = Opened(drive, path, { 0, FileKind::Any });
  auto const stat = file->Stat();
  file->Close();
  return stat;
}
auto DriveFiles::Enumerate(std::uint32_t drive, std::string_view path, std::uint32_t offset,
                           std::span<sdlrdp_dirent> out) const -> int {
  auto const directory = Opened(drive, path, { SDLRDP_FILE_READ, FileKind::Directory });
  auto const pattern   = DrivePath(std::string(path) + "/*");
  Listing    listing   { offset, out };
  auto       first     = true;
  for (auto more = true; more && !listing.Full(); first = false)
    more = listing.Collect(Query(*directory, first, pattern));
  directory->Close();
  return Narrowed<int>(listing.Count());
}
auto DriveFiles::MakeDirectory(std::uint32_t drive, std::string_view path) const -> void {
  Opened(drive, path, { SDLRDP_FILE_CREATE, FileKind::Directory })->Close();
}
auto DriveFiles::Remove(std::uint32_t drive, std::string_view path) const -> void {
  DrivePacket body;
  body.Write(std::uint8_t{ 1 });
  SetInformation(drive, path, InformationClass::Disposition, body);
}
auto DriveFiles::Rename(std::uint32_t drive, std::string_view path, std::string_view destination) const -> void {
  SetInformation(drive, path, InformationClass::Rename, RenameBody(destination));
}
auto DriveFiles::Channel() const -> std::shared_ptr<DriveChannel> const& {
  if (!_channel) throw NoDriveChannel{ };
  return _channel;
}
auto DriveFiles::Opened(std::uint32_t drive, std::string_view path, FileRequest const& request) const
    -> std::unique_ptr<sdlrdp_file> {
  auto const& channel  = Channel();
  auto        response = channel->Wait(channel->Send(drive, 0, IrpMajor::Create, request.Create(DrivePath(path))),
                                       std::string{ path });
  auto file = std::make_unique<sdlrdp_file>(channel, drive, response.Read<std::uint32_t>(), std::string{ path });
  Ensures(file->Channel() != nullptr, "open file retains channel");
  return file;
}
auto DriveFiles::SetInformation(std::uint32_t drive, std::string_view path, InformationClass type,
                                DrivePacket const& body) const -> void {
  auto const file = Opened(drive, path, { 0, FileKind::Any, AccessMask::Delete });
  Exchange(*file, IrpMajor::SetInformation, InformationRequest(type, body));
  file->Close();
}
}
