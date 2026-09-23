#include "_detail/drive-transfer.hpp"
#include "_detail/state.hpp"

#include <array>
#include <chrono>
#include <climits>
#include <cstring>
#include <freerdp/channels/rdpdr.h>
#include <memory>
#include <winpr/nt.h>

namespace {
using namespace Backend;
std::shared_ptr<DriveChannel> Channel(sdlrdp_handle* handle) {
  if (!handle) throw std::runtime_error("Invalid drive handle.");
  std::scoped_lock const lock(handle->state->session_guard);
  auto* peer = handle->state->current;
  if (!peer || !peer->drive) throw std::runtime_error("Drive peer disconnected or no drives shared.");
  return peer->drive;
}
template <class Operation> int Call(sdlrdp_handle* handle, Operation operation) {
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
enum class FileKind { File, Directory, Any };
DrivePacket Exchange(sdlrdp_file& file, unsigned major, DrivePacket const& packet, unsigned minor = 0,
                     bool end = false) {
  Expects(file.Channel() != nullptr, "file retains its channel");
  auto request = file.Channel()->Send(file.Drive(), file.Id(), major, packet, minor);
  return file.Channel()->Wait(request, file.Path(), end);
}
DrivePacket CreatePacket(unsigned access, unsigned disposition, FileKind kind, std::span<uint8_t const> name) {
  DrivePacket packet;
  packet.Put(access);
  packet.Put(0, 8);
  packet.Put(0);
  packet.Put(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  packet.Put(disposition);
  packet.Put(kind == FileKind::Directory ? FILE_DIRECTORY_FILE
                                         : (kind == FileKind::File ? FILE_NON_DIRECTORY_FILE : 0));
  packet.Put(name.size());
  packet.Append(name);
  return packet;
}
std::unique_ptr<sdlrdp_file> Open(sdlrdp_handle* handle, unsigned drive, char const* path, unsigned flags,
                                  FileKind kind, unsigned access = 0) {
  auto channel = Channel(handle);
  auto name    = DrivePath(path);
  if ((flags & SDLRDP_FILE_TRUNCATE) && !(flags & SDLRDP_FILE_WRITE))
    throw std::runtime_error("Truncate requires write access.");
  constexpr unsigned allowed =
      SDLRDP_FILE_READ | SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE | SDLRDP_FILE_TRUNCATE | SDLRDP_FILE_DIRECTORY;
  if (flags & ~allowed) throw std::runtime_error("Invalid drive open flags.");
  access |= FILE_READ_ATTRIBUTES | SYNCHRONIZE;
  if (flags & SDLRDP_FILE_READ) access |= FILE_READ_DATA;
  if (flags & SDLRDP_FILE_WRITE) access |= FILE_WRITE_DATA;
  unsigned disposition = flags & SDLRDP_FILE_CREATE ? FILE_OPEN_IF : FILE_OPEN;
  if (flags & SDLRDP_FILE_TRUNCATE) disposition = flags & SDLRDP_FILE_CREATE ? FILE_OVERWRITE_IF : FILE_OVERWRITE;
  auto packet   = CreatePacket(access, disposition, kind, name);
  auto request  = channel->Send(drive, 0, IRP_MJ_CREATE, packet);
  auto response = channel->Wait(request, path);
  auto file     = std::make_unique<sdlrdp_file>(channel, drive, unsigned(response.Get(4)), path);
  Ensures(file->Channel() != nullptr, "open file retains channel");
  return file;
}
DrivePacket Information(sdlrdp_file& file, unsigned type) {
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
int64_t UnixSeconds(uint64_t value) {
  // WinPR 3.15 FileTimeToSystemTime is a stub on Linux.
  constexpr uint64_t filetime_ticks_per_second = 10'000'000;
  using namespace std::chrono;
  constexpr auto epoch =
      duration_cast<seconds>(sys_days{ 1970y / January / 1 } - sys_days{ 1601y / January / 1 }).count();
  return int64_t(value / filetime_ticks_per_second) - epoch;
}
sdlrdp_stat Stat(sdlrdp_file& file) {
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
void ValidateTransfer(sdlrdp_handle* handle, sdlrdp_file* file, uint64_t offset, void const* buffer, std::size_t size) {
  if (!handle || !file || (!buffer && size) || size > INT_MAX || offset > UINT64_MAX - size)
    throw std::runtime_error("Invalid drive transfer arguments.");
  if (Channel(handle) != file->Channel()) throw std::runtime_error("File belongs to a disconnected peer.");
}

void SetInformation(sdlrdp_file& file, unsigned type, DrivePacket body) {
  DrivePacket        packet;
  constexpr unsigned padding_after_length = 24;
  packet.Put(type);
  packet.Put(body.Bytes().size());
  packet.Zero(padding_after_length);
  packet.Append(body.Bytes());
  Exchange(file, IRP_MJ_SET_INFORMATION, packet);
}
sdlrdp_dirent Entry(DrivePacket& packet) {
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
void CollectEntries(DrivePacket& response, unsigned& skipped, unsigned offset, std::span<sdlrdp_dirent> out,
                    unsigned& count) {
  Expects(count <= out.size(), "directory output cursor is bounded");
  auto length = response.Get(4);
  if (length > response.Bytes().size() - response.Position()) response.Invalid("Truncated directory response.");
  auto limit = response.Position() + length;
  while (response.Position() < limit && count < out.size()) {
    auto start = response.Position();
    auto next  = response.Get(4);
    response.Seek(start);
    auto entry = Entry(response);
    if (std::string_view(entry.name) != "." && std::string_view(entry.name) != "..") {
      if (skipped++ >= offset) out[count++] = entry;
    }
    if (!next) break;
    if (next < response.Position() - start || next > limit - start) response.Invalid("Invalid directory entry offset.");
    response.Seek(start + next);
  }
}
DrivePacket DirectoryQuery(bool first, std::span<uint8_t const> pattern) {
  constexpr unsigned padding_after_path_length = 23;
  DrivePacket        packet;
  packet.Put(FileDirectoryInformation);
  packet.Put(first, 1);
  packet.Put(first ? pattern.size() : 0);
  packet.Zero(padding_after_path_length);
  if (first) packet.Append(pattern);
  return packet;
}
int Enumerate(sdlrdp_handle* handle, unsigned drive, char const* path, unsigned offset, sdlrdp_dirent* out,
              unsigned max) {
  if (!out && max) throw std::runtime_error("Directory output is null.");
  auto     file    = Open(handle, drive, path, SDLRDP_FILE_READ, FileKind::Directory);
  auto     pattern = DrivePath((std::string(path) + "/*").c_str());
  unsigned count   = 0;
  unsigned skipped = 0;
  bool     first   = true;
  while (count < max) {
    auto packet = DirectoryQuery(first, pattern);
    first = false;
    auto response = Exchange(*file, IRP_MJ_DIRECTORY_CONTROL, packet, IRP_MN_QUERY_DIRECTORY, true);
    auto length   = response.Get(4);
    if (!length) break;
    response.Seek(0);
    CollectEntries(response, skipped, offset, { out, max }, count);
  }
  file->Close();
  return int(count);
}
}
void sdlrdp_file::Close() {
  if (std::exchange(closed, true)) return;
  DrivePacket        packet;
  constexpr unsigned padding_after_request_header = 32;
  packet.Zero(padding_after_request_header);
  Exchange(*this, IRP_MJ_CLOSE, packet);
}
sdlrdp_file::~sdlrdp_file() {
  try {
    Close();
  } catch (std::exception const& error) {
    channel->Warn(std::format("Drive close '{}': {}", path, error.what()));
  }
}
int sdlrdp_drive_list(sdlrdp_handle* handle, sdlrdp_drive* out, unsigned max) {
  return Call(handle, [&] {
    if (!handle || (!out && max) || max > INT_MAX) throw std::runtime_error("Invalid drive list arguments.");
    std::scoped_lock const lock(handle->state->session_guard);
    auto* peer = handle->state->current;
    return peer && peer->drive ? peer->drive->List(out, max) : 0;
  });
}
int sdlrdp_drive_open(sdlrdp_handle* handle, unsigned drive, char const* path, unsigned flags, sdlrdp_file** out) {
  return Call(handle, [&] {
    if (!out) throw std::runtime_error("File output is null.");
    *out = nullptr;
    auto kind = flags & SDLRDP_FILE_DIRECTORY ? FileKind::Directory : FileKind::File;
    *out = Open(handle, drive, path, flags, kind).release();
    return 0;
  });
}
int sdlrdp_drive_close(sdlrdp_handle* handle, sdlrdp_file* file) {
  return Call(handle, [&] {
    std::unique_ptr<sdlrdp_file> const owned(file);
    if (!handle || !file) throw std::runtime_error("Invalid file handle.");
    if (Channel(handle) != file->Channel()) throw std::runtime_error("File belongs to a disconnected peer.");
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_read(sdlrdp_handle* h, sdlrdp_file* f, uint64_t offset, void* data, size_t size) {
  return Call(h, [&] {
    ValidateTransfer(h, f, offset, data, size);
    return Transfer(f, offset, static_cast<uint8_t*>(data), size);
  });
}
int sdlrdp_drive_write(sdlrdp_handle* h, sdlrdp_file* f, uint64_t offset, void const* data, size_t size) {
  return Call(h, [&] {
    ValidateTransfer(h, f, offset, data, size);
    return Transfer(f, offset, static_cast<uint8_t const*>(data), size);
  });
}
int sdlrdp_drive_stat(sdlrdp_handle* h, unsigned drive, char const* path, sdlrdp_stat* out) {
  return Call(h, [&] {
    if (!out) throw std::runtime_error("Stat output is null.");
    auto file = Open(h, drive, path, 0, FileKind::Any);
    *out = Stat(*file);
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_enumerate(sdlrdp_handle* h, unsigned drive, char const* path, unsigned offset, sdlrdp_dirent* out,
                           unsigned max) {
  return Call(h, [&] { return Enumerate(h, drive, path, offset, out, max); });
}
int sdlrdp_drive_mkdir(sdlrdp_handle* h, unsigned drive, char const* path) {
  return Call(h, [&] {
    auto file = Open(h, drive, path, SDLRDP_FILE_CREATE, FileKind::Directory);
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_remove(sdlrdp_handle* h, unsigned drive, char const* path) {
  return Call(h, [&] {
    auto        file = Open(h, drive, path, 0, FileKind::Any, DELETE);
    DrivePacket body;
    body.Put(1, 1);
    SetInformation(*file, FileDispositionInformation, std::move(body));
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_rename(sdlrdp_handle* h, unsigned drive, char const* path, char const* destination) {
  return Call(h, [&] {
    auto name = DrivePath(destination);
    name.resize(name.size() - 2);
    auto        file = Open(h, drive, path, 0, FileKind::Any, DELETE);
    DrivePacket body;
    body.Put(0, 1);
    body.Put(0, 1);
    body.Put(name.size());
    body.Append(name);
    SetInformation(*file, FileRenameInformation, std::move(body));
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_fstat(sdlrdp_handle* h, sdlrdp_file* file, sdlrdp_stat* out) {
  return Call(h, [&] {
    if (!file || !out || Channel(h) != file->Channel()) throw std::runtime_error("Invalid or disconnected file.");
    *out = Stat(*file);
    return 0;
  });
}
int sdlrdp_drive_flush(sdlrdp_handle* h, sdlrdp_file* file) {
  return Call(h, [&] {
    if (!file || Channel(h) != file->Channel()) throw std::runtime_error("Invalid or disconnected file.");
    // FreeRDP 3.15 does not handle FLUSH_BUFFERS; synchronous writes are already acknowledged.
    return 0;
  });
}
