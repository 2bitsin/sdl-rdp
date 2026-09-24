#include "_detail/drive-channel.hpp"
#include "_detail/drive-transfer.hpp"
#include "_detail/file-request.hpp"
#include "_detail/handle.hpp"
#include "_detail/malformed-response.hpp"
#include "_detail/peer.hpp"
#include "_detail/sdlrdp-file.hpp"

#include <array>
#include <chrono>
#include <climits>
#include <concepts>
#include <cstring>
#include <freerdp/channels/rdpdr.h>
#include <memory>
#include <winpr/nt.h>

namespace {
using namespace Backend;
auto CurrentDrive(sdlrdp_handle& handle) -> std::shared_ptr<DriveChannel> {
  return OnCurrent(handle.Session(), [](Peer& peer) { return peer.Drive(); });
}
auto Channel(sdlrdp_handle* handle) -> std::shared_ptr<DriveChannel> {
  if (!handle) throw std::runtime_error("Invalid drive handle.");
  auto drive = CurrentDrive(*handle);
  if (!drive) throw std::runtime_error("Drive peer disconnected or no drives shared.");
  return drive;
}
template <std::invocable Operation> auto Call(sdlrdp_handle* handle, Operation operation) -> int {
  try {
    return operation();
  } catch (MalformedResponse const& error) {
    if (auto origin = error.origin.lock()) origin->Abort(error.what());
    SetError(handle, error.what());
    return -1;
  } catch (std::exception const& error) {
    SetError(handle, error.what());
    return -1;
  }
}
auto Open(sdlrdp_handle* handle, unsigned drive, char const* path,
          FileRequest const& request) -> std::unique_ptr<sdlrdp_file> {
  auto channel  = Channel(handle);
  auto packet   = request.Create(DrivePath(path));
  auto response = channel->Wait(channel->Send(drive, 0, IRP_MJ_CREATE, packet), path);
  auto file     = std::make_unique<sdlrdp_file>(channel, drive, unsigned(response.Get(4)), path);
  Ensures(file->Channel() != nullptr, "open file retains channel");
  return file;
}
auto Information(sdlrdp_file& file, unsigned type) -> DrivePacket {
  DrivePacket        packet;
  constexpr unsigned padding_after_length = 24;
  packet.Put(type);
  packet.Put(0);
  packet.Zero(padding_after_length);
  auto result = Exchange(file, IRP_MJ_QUERY_INFORMATION, packet);
  auto length = result.Get(4);
  if (length > result.Bytes().size() - result.Position()) result.Invalid("Truncated drive information.");
  return result;
}
auto UnixSeconds(uint64_t value) -> int64_t {
  // WinPR 3.15 FileTimeToSystemTime is a stub on Linux.
  constexpr uint64_t filetime_ticks_per_second = 10'000'000;
  using namespace std::chrono;
  constexpr auto epoch =
      duration_cast<seconds>(sys_days{ 1970y / January / 1 } - sys_days{ 1601y / January / 1 }).count();
  return int64_t(value / filetime_ticks_per_second) - epoch;
}
auto Stat(sdlrdp_file& file) -> sdlrdp_stat {
  auto               basic                  = Information(file, FileBasicInformation);
  constexpr unsigned last_write_time_offset = 16;
  constexpr unsigned change_time_size       = 8;
  basic.Skip(last_write_time_offset);
  auto modified = basic.Get(8);
  basic.Skip(change_time_size);
  auto               attributes         = basic.Get(4);
  auto               standard           = Information(file, FileStandardInformation);
  constexpr unsigned end_of_file_offset = 8;
  standard.Skip(end_of_file_offset);
  return { standard.Get(8), bool(attributes & FILE_ATTRIBUTE_DIRECTORY), UnixSeconds(modified) };
}
auto ValidateTransfer(sdlrdp_handle* handle, sdlrdp_file* file, uint64_t offset, void const* buffer,
                      std::size_t size) -> void {
  if (!handle || !file || (!buffer && size) || size > INT_MAX || offset > UINT64_MAX - size)
    throw std::runtime_error("Invalid drive transfer arguments.");
  if (Channel(handle) != file->Channel()) throw std::runtime_error("File belongs to a disconnected peer.");
}

auto SetInformation(sdlrdp_file& file, unsigned type, DrivePacket body) -> void {
  DrivePacket        packet;
  constexpr unsigned padding_after_length = 24;
  packet.Put(type);
  packet.Put(body.Bytes().size());
  packet.Zero(padding_after_length);
  packet.Append(body.Bytes());
  Exchange(file, IRP_MJ_SET_INFORMATION, packet);
}
auto SetPathInformation(sdlrdp_handle* handle, unsigned drive, char const* path, unsigned type,
                        DrivePacket body) -> void {
  auto file = Open(handle, drive, path, { 0, FileKind::Any, DELETE });
  SetInformation(*file, type, std::move(body));
  file->Close();
}
template <class Byte>
  requires std::same_as<std::remove_const_t<Byte>, uint8_t>
auto CheckedTransfer(sdlrdp_handle* handle, sdlrdp_file* file, uint64_t offset, Byte* data, std::size_t size) -> int {
  return Call(handle, [&] {
    ValidateTransfer(handle, file, offset, data, size);
    return Transfer(file, offset, data, size);
  });
}
auto Entry(DrivePacket& packet) -> sdlrdp_dirent {
  constexpr unsigned end_of_file_offset         = 40;
  constexpr unsigned allocation_size_field_size = 8;
  packet.Skip(end_of_file_offset);
  sdlrdp_dirent entry{ };
  entry.size = packet.Get(8);
  packet.Skip(allocation_size_field_size);
  entry.directory = bool(packet.Get(4) & FILE_ATTRIBUTE_DIRECTORY);
  auto length = packet.Get(4);
  auto name   = packet.Text(length);
  if (name.size() >= sizeof(entry.name)) throw std::runtime_error("Drive entry name exceeds ABI capacity.");
  std::memcpy(entry.name, name.c_str(), name.size() + 1);
  return entry;
}
auto Listed(sdlrdp_dirent const& entry) -> bool {
  std::string_view const name = entry.name;
  return name != "." && name != "..";
}
auto CollectEntries(DrivePacket& response, unsigned& skipped, unsigned offset, std::span<sdlrdp_dirent> out,
                    unsigned& count) -> void {
  Expects(count <= out.size(), "directory output cursor is bounded");
  auto length = response.Get(4);
  if (length > response.Bytes().size() - response.Position()) response.Invalid("Truncated directory response.");
  auto limit = response.Position() + length;
  while (response.Position() < limit && count < out.size()) {
    auto start = response.Position();
    auto next  = response.Get(4);
    response.Seek(start);
    auto entry = Entry(response);
    if (Listed(entry) && skipped++ >= offset) out[count++] = entry;
    if (!next) break;
    if (next < response.Position() - start || next > limit - start) response.Invalid("Invalid directory entry offset.");
    response.Seek(start + next);
  }
}
auto DirectoryQuery(bool first, std::span<uint8_t const> pattern) -> DrivePacket {
  constexpr unsigned padding_after_path_length = 23;
  DrivePacket        packet;
  packet.Put(FileDirectoryInformation);
  packet.Put(first, 1);
  packet.Put(first ? pattern.size() : 0);
  packet.Zero(padding_after_path_length);
  if (first) packet.Append(pattern);
  return packet;
}
auto Enumerate(sdlrdp_handle* handle, unsigned drive, char const* path, unsigned offset,
               std::span<sdlrdp_dirent> out) -> int {
  auto     file    = Open(handle, drive, path, { SDLRDP_FILE_READ, FileKind::Directory });
  auto     pattern = DrivePath((std::string(path) + "/*").c_str());
  unsigned count   = 0;
  unsigned skipped = 0;
  bool     first   = true;
  while (count < out.size()) {
    auto packet = DirectoryQuery(first, pattern);
    first = false;
    auto response = Exchange(*file, IRP_MJ_DIRECTORY_CONTROL, packet, IRP_MN_QUERY_DIRECTORY, true);
    auto length   = response.Get(4);
    if (!length) break;
    response.Seek(0);
    CollectEntries(response, skipped, offset, out, count);
  }
  file->Close();
  return int(count);
}
}
auto sdlrdp_drive_list(sdlrdp_handle* handle, sdlrdp_drive* out, unsigned max) -> int {
  return Call(handle, [&] {
    if (!handle || (!out && max) || max > INT_MAX) throw std::runtime_error("Invalid drive list arguments.");
    auto const drive = CurrentDrive(*handle);
    return drive ? drive->List(out, max) : 0;
  });
}
auto sdlrdp_drive_open(sdlrdp_handle* handle, unsigned drive, char const* path, unsigned flags,
                       sdlrdp_file** out) -> int {
  return Call(handle, [&] {
    if (!out) throw std::runtime_error("File output is null.");
    *out = nullptr;
    auto kind = flags & SDLRDP_FILE_DIRECTORY ? FileKind::Directory : FileKind::File;
    *out = Open(handle, drive, path, { flags, kind }).release();
    return 0;
  });
}
auto sdlrdp_drive_close(sdlrdp_handle* handle, sdlrdp_file* file) -> int {
  return Call(handle, [&] {
    std::unique_ptr<sdlrdp_file> const owned(file);
    if (!handle || !file) throw std::runtime_error("Invalid file handle.");
    if (Channel(handle) != file->Channel()) throw std::runtime_error("File belongs to a disconnected peer.");
    file->Close();
    return 0;
  });
}
auto sdlrdp_drive_read(sdlrdp_handle* h, sdlrdp_file* f, uint64_t offset, void* data, size_t size) -> int {
  return CheckedTransfer(h, f, offset, static_cast<uint8_t*>(data), size);
}
auto sdlrdp_drive_write(sdlrdp_handle* h, sdlrdp_file* f, uint64_t offset, void const* data, size_t size) -> int {
  return CheckedTransfer(h, f, offset, static_cast<uint8_t const*>(data), size);
}
auto sdlrdp_drive_stat(sdlrdp_handle* h, unsigned drive, char const* path, sdlrdp_stat* out) -> int {
  return Call(h, [&] {
    if (!out) throw std::runtime_error("Stat output is null.");
    auto file = Open(h, drive, path, { 0, FileKind::Any });
    *out = Stat(*file);
    file->Close();
    return 0;
  });
}
auto sdlrdp_drive_enumerate(sdlrdp_handle* h, unsigned drive, char const* path, unsigned offset, sdlrdp_dirent* out,
                            unsigned max) -> int {
  return Call(h, [&] {
    if (!out && max) throw std::runtime_error("Directory output is null.");
    return Enumerate(h, drive, path, offset, { out, max });
  });
}
auto sdlrdp_drive_mkdir(sdlrdp_handle* h, unsigned drive, char const* path) -> int {
  return Call(h, [&] {
    auto file = Open(h, drive, path, { SDLRDP_FILE_CREATE, FileKind::Directory });
    file->Close();
    return 0;
  });
}
auto sdlrdp_drive_remove(sdlrdp_handle* h, unsigned drive, char const* path) -> int {
  return Call(h, [&] {
    DrivePacket body;
    body.Put(1, 1);
    SetPathInformation(h, drive, path, FileDispositionInformation, std::move(body));
    return 0;
  });
}
auto sdlrdp_drive_rename(sdlrdp_handle* h, unsigned drive, char const* path, char const* destination) -> int {
  return Call(h, [&] {
    auto name = DrivePath(destination);
    name.resize(name.size() - 2);
    DrivePacket body;
    body.Put(0, 1);
    body.Put(0, 1);
    body.Put(name.size());
    body.Append(name);
    SetPathInformation(h, drive, path, FileRenameInformation, std::move(body));
    return 0;
  });
}
auto sdlrdp_drive_fstat(sdlrdp_handle* h, sdlrdp_file* file, sdlrdp_stat* out) -> int {
  return Call(h, [&] {
    if (!file || !out || Channel(h) != file->Channel()) throw std::runtime_error("Invalid or disconnected file.");
    *out = Stat(*file);
    return 0;
  });
}
auto sdlrdp_drive_flush(sdlrdp_handle* h, sdlrdp_file* file) -> int {
  return Call(h, [&] {
    if (!file || Channel(h) != file->Channel()) throw std::runtime_error("Invalid or disconnected file.");
    // FreeRDP 3.15 does not handle FLUSH_BUFFERS; synchronous writes are already acknowledged.
    return 0;
  });
}
