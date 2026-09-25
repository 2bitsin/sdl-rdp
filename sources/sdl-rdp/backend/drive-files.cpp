#include <sdl-rdp/backend/exceptions.hpp>
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/drive/exceptions.hpp>
#include <sdl-rdp/drive/file-request.hpp>
#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/transfer.hpp>
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/terminated-copy.hpp>

#include <freerdp/channels/rdpdr.h>
#include <winpr/nt.h>
#include <array>
#include <chrono>
#include <climits>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

using sdl_rdp::backend::EntryNameTooLong;
using sdl_rdp::backend::InvalidArguments;
using sdl_rdp::backend::NoDriveChannel;
using sdl_rdp::drive::DriveChannel;
using sdl_rdp::drive::DrivePacket;
using sdl_rdp::drive::DrivePath;
using sdl_rdp::drive::Exchange;
using sdl_rdp::drive::FileKind;
using sdl_rdp::drive::FileRequest;
using sdl_rdp::drive::PeerDisconnected;
using sdl_rdp::drive::Transfer;

namespace {
using namespace Backend;
auto CurrentDrive(sdlrdp_handle& handle) -> std::shared_ptr<DriveChannel> {
  return OnCurrent(handle.Session(), [](Peer& peer) { return peer.Redirected().Drive(); });
}
auto Channel(sdlrdp_handle* handle) -> std::shared_ptr<DriveChannel> {
  if (!handle) throw NullArgument{ "Drive handle" };
  auto drive = CurrentDrive(*handle);
  if (!drive) throw NoDriveChannel{ };
  return drive;
}
auto Attached(sdlrdp_handle* handle, sdlrdp_file* file) -> sdlrdp_file& {
  if (!file) throw NullArgument{ "Drive file" };
  if (Channel(handle) != file->Channel()) throw PeerDisconnected{ file->Path() };
  return *file;
}
template <std::invocable OperationTy> auto Call(sdlrdp_handle* handle, OperationTy operation) noexcept -> int {
  return Contained(-1, operation, [handle](std::string_view text) { SetError(handle, std::string{ text }); });
}
auto Open(sdlrdp_handle* handle, std::uint32_t drive, char const* path, FileRequest const& request)
    -> std::unique_ptr<sdlrdp_file> {
  auto channel  = Channel(handle);
  auto packet   = request.Create(DrivePath(path));
  auto response = channel->Wait(channel->Send(drive, 0, IRP_MJ_CREATE, packet), path);
  auto file     = std::make_unique<sdlrdp_file>(channel, drive, response.Read<std::uint32_t>(), path);
  Ensures(file->Channel() != nullptr, "open file retains channel");
  return file;
}
auto InformationRequest(std::uint32_t type, DrivePacket const& body) -> DrivePacket {
  constexpr std::size_t padding_after_length = 24;
  DrivePacket           packet;
  packet.Write(type);
  packet.Write(Narrowed<std::uint32_t>(body.Bytes().size()));
  packet.Zero(padding_after_length);
  packet.Append(body.Bytes());
  return packet;
}
auto Information(sdlrdp_file& file, std::uint32_t type) -> DrivePacket {
  auto result = Exchange(file, IRP_MJ_QUERY_INFORMATION, InformationRequest(type, { }));
  auto length = result.Read<std::uint32_t>();
  if (length > result.Bytes().size() - result.Position()) result.Invalid("truncated information");
  return result;
}
auto UnixSeconds(std::uint64_t value) -> std::int64_t {
  // WinPR 3.32 timezone.c:689 FileTimeToSystemTime is a stub on Linux.
  constexpr std::uint64_t filetime_ticks_per_second = 10'000'000;
  using namespace std::chrono;
  constexpr auto epoch = duration_cast<seconds>(sys_days{ 1970y / January / 1 } - sys_days{ 1601y / January / 1 })
                             .count();
  return Narrowed<std::int64_t>(value / filetime_ticks_per_second) - epoch;
}
auto Stat(sdlrdp_file& file) -> sdlrdp_stat {
  auto                  basic                  = Information(file, FileBasicInformation);
  constexpr std::size_t last_write_time_offset = 16;
  constexpr std::size_t change_time_size       = 8;
  basic.Skip(last_write_time_offset);
  auto modified = basic.Read<std::uint64_t>();
  basic.Skip(change_time_size);
  auto                  attributes         = basic.Read<std::uint32_t>();
  auto                  standard           = Information(file, FileStandardInformation);
  constexpr std::size_t end_of_file_offset = 8;
  standard.Skip(end_of_file_offset);
  return { standard.Read<std::uint64_t>(), bool(attributes & FILE_ATTRIBUTE_DIRECTORY), UnixSeconds(modified) };
}
auto ValidateTransfer(sdlrdp_handle* handle, sdlrdp_file* file, std::uint64_t offset, void const* buffer,
                      std::size_t size) -> void {
  if (!handle || !file || (!buffer && size) || size > INT_MAX || offset > UINT64_MAX - size)
    throw InvalidArguments{ "drive transfer", "handle, file, buffer, size or offset" };
  Attached(handle, file);
}

auto SetInformation(sdlrdp_file& file, std::uint32_t type, DrivePacket const& body) -> void {
  Exchange(file, IRP_MJ_SET_INFORMATION, InformationRequest(type, body));
}
auto SetPathInformation(sdlrdp_handle* handle, std::uint32_t drive, char const* path, std::uint32_t type,
                        DrivePacket const& body) -> void {
  auto file = Open(handle, drive, path, { 0, FileKind::Any, DELETE });
  SetInformation(*file, type, body);
  file->Close();
}
template <class Byte>
  requires std::same_as<std::remove_const_t<Byte>, std::byte>
auto CheckedTransfer(sdlrdp_handle* handle, sdlrdp_file* file, std::uint64_t offset, Byte* data, std::size_t size)
    -> int {
  return Call(handle, [&] {
    ValidateTransfer(handle, file, offset, data, size);
    return Transfer(file, offset, data, size);
  });
}
auto Entry(DrivePacket& packet) -> sdlrdp_dirent {
  constexpr std::size_t end_of_file_offset         = 40;
  constexpr std::size_t allocation_size_field_size = 8;
  packet.Skip(end_of_file_offset);
  sdlrdp_dirent entry{ };
  entry.size = packet.Read<std::uint64_t>();
  packet.Skip(allocation_size_field_size);
  entry.directory = bool(packet.Read<std::uint32_t>() & FILE_ATTRIBUTE_DIRECTORY);
  auto length = packet.Read<std::uint32_t>();
  auto name   = packet.Text(length);
  if (name.size() >= sizeof(entry.name)) throw EntryNameTooLong{ name.size(), sizeof(entry.name) - 1 };
  CopyTerminated(entry.name, name);
  return entry;
}
auto Listed(sdlrdp_dirent const& entry) -> bool {
  std::string_view const name = entry.name;
  return name != "." && name != "..";
}
auto CollectEntries(DrivePacket& response, std::size_t& skipped, std::uint32_t offset, std::span<sdlrdp_dirent> out,
                    std::size_t& count) -> void {
  Expects(count <= out.size(), "directory output cursor is bounded");
  auto length = response.Read<std::uint32_t>();
  if (length > response.Bytes().size() - response.Position()) response.Invalid("truncated directory listing");
  auto limit = response.Position() + length;
  while (response.Position() < limit && count < out.size()) {
    auto start = response.Position();
    auto next  = response.Read<std::uint32_t>();
    response.Seek(start);
    auto entry = Entry(response);
    if (Listed(entry) && skipped++ >= offset) out[count++] = entry;
    if (!next) break;
    if (next < response.Position() - start || next > limit - start) response.Invalid("invalid directory entry offset");
    response.Seek(start + next);
  }
}
auto DirectoryQuery(bool first, std::span<std::byte const> pattern) -> DrivePacket {
  constexpr std::size_t padding_after_path_length = 23;
  DrivePacket           packet;
  packet.Write(std::uint32_t{ FileDirectoryInformation });
  packet.Write(std::uint8_t{ first });
  packet.Write(Narrowed<std::uint32_t>(first ? pattern.size() : 0));
  packet.Zero(padding_after_path_length);
  if (first) packet.Append(pattern);
  return packet;
}
auto Enumerate(sdlrdp_handle* handle, std::uint32_t drive, char const* path, std::uint32_t offset,
               std::span<sdlrdp_dirent> out) -> int {
  auto        file    = Open(handle, drive, path, { SDLRDP_FILE_READ, FileKind::Directory });
  auto        pattern = DrivePath((std::string(path) + "/*").c_str());
  std::size_t count   = 0;
  std::size_t skipped = 0;
  bool        first   = true;
  while (count < out.size()) {
    auto packet = DirectoryQuery(first, pattern);
    first = false;
    auto response = Exchange(*file, IRP_MJ_DIRECTORY_CONTROL, packet, IRP_MN_QUERY_DIRECTORY, true);
    auto length   = response.Read<std::uint32_t>();
    if (!length) break;
    response.Seek(0);
    CollectEntries(response, skipped, offset, out, count);
  }
  file->Close();
  return int(count);
}
}
auto sdlrdp_drive_list(sdlrdp_handle* handle, sdlrdp_drive* out, std::uint32_t max) -> int {
  return Call(handle, [&] {
    if (!handle || (!out && max) || max > INT_MAX) throw InvalidArguments{ "drive list", "handle, output or count" };
    auto const drive = CurrentDrive(*handle);
    return drive ? drive->List(out, max) : 0;
  });
}
auto sdlrdp_drive_open(sdlrdp_handle* handle, std::uint32_t drive, char const* path, std::uint32_t flags,
                       sdlrdp_file** out) -> int {
  return Call(handle, [&] {
    if (!out) throw NullArgument{ "Drive open output" };
    *out = nullptr;
    auto kind = flags & SDLRDP_FILE_DIRECTORY ? FileKind::Directory : FileKind::File;
    *out = Open(handle, drive, path, { flags, kind }).release();
    return 0;
  });
}
auto sdlrdp_drive_close(sdlrdp_handle* handle, sdlrdp_file* file) -> int {
  return Call(handle, [&] {
    std::unique_ptr<sdlrdp_file> const owned(file);
    Attached(handle, file).Close();
    return 0;
  });
}
auto sdlrdp_drive_read(sdlrdp_handle* h, sdlrdp_file* f, std::uint64_t offset, void* data, std::size_t size) -> int {
  return CheckedTransfer(h, f, offset, static_cast<std::byte*>(data), size);
}
auto sdlrdp_drive_write(sdlrdp_handle* h, sdlrdp_file* f, std::uint64_t offset, void const* data, std::size_t size)
    -> int {
  return CheckedTransfer(h, f, offset, static_cast<std::byte const*>(data), size);
}
auto sdlrdp_drive_stat(sdlrdp_handle* h, std::uint32_t drive, char const* path, sdlrdp_stat* out) -> int {
  return Call(h, [&] {
    if (!out) throw NullArgument{ "Drive stat output" };
    auto file = Open(h, drive, path, { 0, FileKind::Any });
    *out = Stat(*file);
    file->Close();
    return 0;
  });
}
auto sdlrdp_drive_enumerate(sdlrdp_handle* h, std::uint32_t drive, char const* path, std::uint32_t offset,
                            sdlrdp_dirent* out, std::uint32_t max) -> int {
  return Call(h, [&] {
    if (!out && max) throw NullArgument{ "Drive directory output" };
    return Enumerate(h, drive, path, offset, { out, max });
  });
}
auto sdlrdp_drive_mkdir(sdlrdp_handle* h, std::uint32_t drive, char const* path) -> int {
  return Call(h, [&] {
    auto file = Open(h, drive, path, { SDLRDP_FILE_CREATE, FileKind::Directory });
    file->Close();
    return 0;
  });
}
auto sdlrdp_drive_remove(sdlrdp_handle* h, std::uint32_t drive, char const* path) -> int {
  return Call(h, [&] {
    DrivePacket body;
    body.Write(std::uint8_t{ 1 });
    SetPathInformation(h, drive, path, FileDispositionInformation, body);
    return 0;
  });
}
auto sdlrdp_drive_rename(sdlrdp_handle* h, std::uint32_t drive, char const* path, char const* destination) -> int {
  return Call(h, [&] {
    auto name = DrivePath(destination);
    name.resize(name.size() - 2);
    DrivePacket body;
    body.Write(std::uint8_t{ 0 });
    body.Write(std::uint8_t{ 0 });
    body.Write(Narrowed<std::uint32_t>(name.size()));
    body.Append(name);
    SetPathInformation(h, drive, path, FileRenameInformation, body);
    return 0;
  });
}
auto sdlrdp_drive_fstat(sdlrdp_handle* h, sdlrdp_file* file, sdlrdp_stat* out) -> int {
  return Call(h, [&] {
    if (!out) throw NullArgument{ "Drive fstat output" };
    *out = Stat(Attached(h, file));
    return 0;
  });
}
auto sdlrdp_drive_flush(sdlrdp_handle* h, sdlrdp_file* file) -> int {
  return Call(h, [&] {
    Attached(h, file);
    // FreeRDP 3.32 drive_main.c:754 has no FLUSH_BUFFERS case; synchronous writes are already acknowledged.
    return 0;
  });
}
