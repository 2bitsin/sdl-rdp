#include "_detail/state.hpp"
#include <winpr/nt.h>
#include <freerdp/channels/rdpdr.h>
#include <cstring>
#include <climits>
#include <chrono>
#include <array>

namespace Backend { void DriveError(std::string); }
namespace {
using namespace Backend;
std::shared_ptr<DriveChannel> Channel(sdlrdp_handle* handle) {
  if (!handle) throw std::runtime_error("Invalid drive handle.");
  std::scoped_lock lock(handle->state->session_guard);
  auto peer = handle->state->current;
  if (!peer || !peer->drive) throw std::runtime_error("Drive peer disconnected or no drives shared.");
  return peer->drive;
}
template<class Operation> int Call(Operation operation) {
  try { return operation(); }
  catch (MalformedResponse const& error) {
    if (auto origin = error.origin.lock()) origin->Abort(error.what());
    DriveError(error.what());
    return -1;
  }
  catch (std::exception const& error) { DriveError(error.what()); return -1; }
}
enum class FileKind { File, Directory, Any };
DrivePacket Exchange(sdlrdp_file& file, unsigned major, DrivePacket packet, unsigned minor = 0, bool end = false) {
  Expects(file.channel != nullptr, "file retains its channel");
  auto request = file.channel->Send(file.drive, file.wire, major, std::move(packet), minor);
  return file.channel->Wait(request, file.path, end);
}
std::unique_ptr<sdlrdp_file> Open(sdlrdp_handle* handle, unsigned drive, char const* path,
                                unsigned flags, FileKind kind, unsigned access = 0) {
  auto channel = Channel(handle);
  auto name = DrivePath(path);
  if ((flags & SDLRDP_FILE_TRUNCATE) && !(flags & SDLRDP_FILE_WRITE))
    throw std::runtime_error("Truncate requires write access.");
  constexpr unsigned allowed = SDLRDP_FILE_READ | SDLRDP_FILE_WRITE | SDLRDP_FILE_CREATE
    | SDLRDP_FILE_TRUNCATE | SDLRDP_FILE_DIRECTORY;
  if (flags & ~allowed) throw std::runtime_error("Invalid drive open flags.");
  access |= FILE_READ_ATTRIBUTES | SYNCHRONIZE;
  if (flags & SDLRDP_FILE_READ) access |= FILE_READ_DATA;
  if (flags & SDLRDP_FILE_WRITE) access |= FILE_WRITE_DATA;
  unsigned disposition = flags & SDLRDP_FILE_CREATE ? FILE_OPEN_IF : FILE_OPEN;
  if (flags & SDLRDP_FILE_TRUNCATE) disposition = flags & SDLRDP_FILE_CREATE ? FILE_OVERWRITE_IF : FILE_OVERWRITE;
  DrivePacket packet;
  packet.Put(access); packet.Put(0, 8); packet.Put(0); packet.Put(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  packet.Put(disposition);
  packet.Put(kind == FileKind::Directory ? FILE_DIRECTORY_FILE : (kind == FileKind::File ? FILE_NON_DIRECTORY_FILE : 0));
  packet.Put(name.size()); packet.Append(name);
  auto request = channel->Send(drive, 0, IRP_MJ_CREATE, std::move(packet));
  auto response = channel->Wait(request, path);
  auto file = std::unique_ptr<sdlrdp_file>(new sdlrdp_file{channel, drive, unsigned(response.Get(4)), path});
  Ensures(file->channel != nullptr, "open file retains channel");
  return file;
}
DrivePacket Information(sdlrdp_file& file, unsigned type) {
  DrivePacket packet;
  constexpr unsigned padding_after_length = 24;
  packet.Put(type); packet.Put(0); packet.Zero(padding_after_length);
  auto result = Exchange(file, IRP_MJ_QUERY_INFORMATION, std::move(packet));
  auto length = result.Get(4);
  if (length > result.bytes.size() - result.position) result.Invalid("Truncated drive information.");
  return result;
}
int64_t UnixSeconds(uint64_t value) {
  // WinPR 3.15 FileTimeToSystemTime is a stub on Linux.
  constexpr uint64_t filetime_ticks_per_second = 10'000'000;
  using namespace std::chrono;
  constexpr auto epoch = duration_cast<seconds>(sys_days{1970y / January / 1}
    - sys_days{1601y / January / 1}).count();
  return int64_t(value / filetime_ticks_per_second) - epoch;
}
sdlrdp_stat Stat(sdlrdp_file& file) {
  auto basic = Information(file, FileBasicInformation);
  constexpr unsigned last_write_time_offset = 16, change_time_size = 8;
  basic.Skip(last_write_time_offset);
  auto modified = basic.Get(8);
  basic.Skip(change_time_size);
  auto attributes = basic.Get(4);
  auto standard = Information(file, FileStandardInformation);
  constexpr unsigned end_of_file_offset = 8;
  standard.Skip(end_of_file_offset);
  return {standard.Get(8), bool(attributes & FILE_ATTRIBUTE_DIRECTORY),
    UnixSeconds(modified)};
}
template<class Byte>
std::shared_ptr<DriveRequest> Submit(sdlrdp_file& file, uint64_t offset, std::span<Byte> bytes) {
  Expects(!bytes.empty() && bytes.size() <= UINT32_MAX, "transfer chunk fits the wire length");
  constexpr bool write = std::is_const_v<Byte>;
  DrivePacket packet;
  constexpr unsigned padding_after_offset = 20;
  packet.Put(bytes.size()); packet.Put(offset, 8); packet.Zero(padding_after_offset);
  if constexpr (write) packet.Append(bytes);
  return file.channel->Send(file.drive, file.wire, write ? IRP_MJ_WRITE : IRP_MJ_READ, std::move(packet));
}
template<class Byte>
size_t Finish(sdlrdp_file& file, std::shared_ptr<DriveRequest> const& request, std::span<Byte> bytes) {
  constexpr bool write = std::is_const_v<Byte>;
  auto response = file.channel->Wait(request, file.path, !write);
  auto received = response.Get(4);
  if (received > bytes.size()) response.Invalid("Drive returned oversized transfer.");
  if constexpr (!write) {
    if (received > response.bytes.size() - response.position) response.Invalid("Truncated drive read.");
    std::memcpy(bytes.data(), response.bytes.data() + response.position, received);
  }
  return received;
}
template<class Byte>
int Transfer(sdlrdp_handle* handle, sdlrdp_file* file, uint64_t offset, Byte* buffer, size_t size) {
  if (!handle || !file || (!buffer && size) || size > INT_MAX || offset > UINT64_MAX - size)
    throw std::runtime_error("Invalid drive transfer arguments.");
  if (Channel(handle) != file->channel) throw std::runtime_error("File belongs to a disconnected peer.");
  constexpr size_t chunk = 65536, depth = 8;
  std::array<Slot, depth> slots{};
  size_t submitted = 0, active = 0, limit = size;
  std::exception_ptr failure;
  auto submit = [&](Slot& slot) {
    if (failure || submitted >= limit) return;
    Expects(!slot.request, "submission slot is empty");
    slot.offset = submitted;
    slot.count = std::min(chunk, size - submitted);
    slot.request = Submit(*file, offset + submitted, std::span(buffer + submitted, slot.count));
    submitted += slot.count;
    ++active;
  };
  for (auto& slot : slots) submit(slot);
  while (active) {
    auto& slot = slots[file->channel->WaitAny(slots)];
    try {
      auto received = Finish(*file, slot.request, std::span(buffer + slot.offset, slot.count));
      if (received != slot.count) limit = std::min(limit, slot.offset + received);
    } catch (MalformedResponse const&) { throw; }
    catch (...) { if (!failure) failure = std::current_exception(); }
    slot.request.reset();
    --active;
    submit(slot);
  }
  if (failure) std::rethrow_exception(failure);
  return int(limit);
}
void SetInformation(sdlrdp_file& file, unsigned type, DrivePacket body) {
  DrivePacket packet;
  constexpr unsigned padding_after_length = 24;
  packet.Put(type); packet.Put(body.bytes.size()); packet.Zero(padding_after_length); packet.Append(body.bytes);
  Exchange(file, IRP_MJ_SET_INFORMATION, std::move(packet));
}
sdlrdp_dirent Entry(DrivePacket& packet) {
  constexpr unsigned end_of_file_offset = 40, allocation_size_field_size = 8;
  packet.Skip(end_of_file_offset);
  sdlrdp_dirent entry{};
  entry.size = packet.Get(8);
  packet.Skip(allocation_size_field_size);
  entry.directory = bool(packet.Get(4) & FILE_ATTRIBUTE_DIRECTORY);
  auto length = packet.Get(4);
  auto name = packet.Text(length);
  if (name.size() >= sizeof(entry.name)) throw std::runtime_error("Drive entry name exceeds ABI capacity.");
  std::memcpy(entry.name, name.c_str(), name.size() + 1);
  return entry;
}
void CollectEntries(DrivePacket& response, unsigned& skipped, unsigned offset,
                    std::span<sdlrdp_dirent> out, unsigned& count) {
  Expects(count <= out.size(), "directory output cursor is bounded");
  auto length = response.Get(4);
  if (length > response.bytes.size() - response.position) response.Invalid("Truncated directory response.");
  auto limit = response.position + length;
  while (response.position < limit && count < out.size()) {
    auto start = response.position;
    auto next = response.Get(4);
    response.position = start;
    auto entry = Entry(response);
    if (std::string_view(entry.name) != "." && std::string_view(entry.name) != "..") {
      if (skipped++ >= offset) out[count++] = entry;
    }
    if (!next) break;
    if (next < response.position - start || next > limit - start)
      response.Invalid("Invalid directory entry offset.");
    response.position = start + next;
  }
}
int Enumerate(sdlrdp_handle* handle, unsigned drive, char const* path, unsigned offset,
              sdlrdp_dirent* out, unsigned max) {
  if (!out && max) throw std::runtime_error("Directory output is null.");
  auto file = Open(handle, drive, path, SDLRDP_FILE_READ, FileKind::Directory);
  auto pattern = DrivePath((std::string(path) + "/*").c_str());
  unsigned count = 0, skipped = 0;
  bool first = true;
  constexpr unsigned padding_after_path_length = 23;
  while (count < max) {
    DrivePacket packet;
    packet.Put(FileDirectoryInformation); packet.Put(first, 1);
    packet.Put(first ? pattern.size() : 0); packet.Zero(padding_after_path_length);
    if (first) packet.Append(pattern);
    first = false;
    auto response = Exchange(*file, IRP_MJ_DIRECTORY_CONTROL, std::move(packet), IRP_MN_QUERY_DIRECTORY, true);
    auto length = response.Get(4);
    if (!length) break;
    response.position = 0;
    CollectEntries(response, skipped, offset, {out, max}, count);
  }
  file->Close();
  return int(count);
}
}
void sdlrdp_file::Close() {
  if (std::exchange(closed, true)) return;
  DrivePacket packet;
  constexpr unsigned padding_after_request_header = 32;
  packet.Zero(padding_after_request_header);
  Exchange(*this, IRP_MJ_CLOSE, std::move(packet));
}
sdlrdp_file::~sdlrdp_file() {
  try { Close(); }
  catch (std::exception const& error) { channel->Warn(std::format("Drive close '{}': {}", path, error.what())); }
}
int sdlrdp_drive_list(sdlrdp_handle* handle, sdlrdp_drive* out, unsigned max) {
  return Call([&] {
    if (!handle || (!out && max) || max > INT_MAX) throw std::runtime_error("Invalid drive list arguments.");
    std::scoped_lock lock(handle->state->session_guard);
    auto peer = handle->state->current;
    return peer && peer->drive ? peer->drive->List(out, max) : 0;
  });
}
int sdlrdp_drive_open(sdlrdp_handle* handle, unsigned drive, char const* path, unsigned flags, sdlrdp_file** out) {
  return Call([&] {
    if (!out) throw std::runtime_error("File output is null.");
    *out = nullptr;
    auto kind = flags & SDLRDP_FILE_DIRECTORY ? FileKind::Directory : FileKind::File;
    *out = Open(handle, drive, path, flags, kind).release();
    return 0;
  });
}
int sdlrdp_drive_close(sdlrdp_handle* handle, sdlrdp_file* file) {
  return Call([&] {
    std::unique_ptr<sdlrdp_file> owned(file);
    if (!handle || !file) throw std::runtime_error("Invalid file handle.");
    if (Channel(handle) != file->channel) throw std::runtime_error("File belongs to a disconnected peer.");
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_read(sdlrdp_handle* h, sdlrdp_file* f, uint64_t offset, void* data, size_t size) {
  return Call([&] { return Transfer(h, f, offset, static_cast<uint8_t*>(data), size); });
}
int sdlrdp_drive_write(sdlrdp_handle* h, sdlrdp_file* f, uint64_t offset, void const* data, size_t size) {
  return Call([&] { return Transfer(h, f, offset, static_cast<uint8_t const*>(data), size); });
}
int sdlrdp_drive_stat(sdlrdp_handle* h, unsigned drive, char const* path, sdlrdp_stat* out) {
  return Call([&] {
    if (!out) throw std::runtime_error("Stat output is null.");
    auto file = Open(h, drive, path, 0, FileKind::Any);
    *out = Stat(*file);
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_enumerate(sdlrdp_handle* h, unsigned drive, char const* path, unsigned offset,
                          sdlrdp_dirent* out, unsigned max) {
  return Call([&] { return Enumerate(h, drive, path, offset, out, max); });
}
int sdlrdp_drive_mkdir(sdlrdp_handle* h, unsigned drive, char const* path) {
  return Call([&] {
    auto file = Open(h, drive, path, SDLRDP_FILE_CREATE, FileKind::Directory);
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_remove(sdlrdp_handle* h, unsigned drive, char const* path) {
  return Call([&] {
    auto file = Open(h, drive, path, 0, FileKind::Any, DELETE);
    DrivePacket body;
    body.Put(1, 1);
    SetInformation(*file, FileDispositionInformation, std::move(body));
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_rename(sdlrdp_handle* h, unsigned drive, char const* path, char const* destination) {
  return Call([&] {
    auto name = DrivePath(destination);
    name.resize(name.size() - 2);
    auto file = Open(h, drive, path, 0, FileKind::Any, DELETE);
    DrivePacket body;
    body.Put(0, 1); body.Put(0, 1); body.Put(name.size()); body.Append(name);
    SetInformation(*file, FileRenameInformation, std::move(body));
    file->Close();
    return 0;
  });
}
int sdlrdp_drive_fstat(sdlrdp_handle* h, sdlrdp_file* file, sdlrdp_stat* out) {
  return Call([&] {
    if (!file || !out || Channel(h) != file->channel) throw std::runtime_error("Invalid or disconnected file.");
    *out = Stat(*file);
    return 0;
  });
}
int sdlrdp_drive_flush(sdlrdp_handle* h, sdlrdp_file* file) {
  return Call([&] {
    if (!file || Channel(h) != file->channel) throw std::runtime_error("Invalid or disconnected file.");
    // FreeRDP 3.15 does not handle FLUSH_BUFFERS; synchronous writes are already acknowledged.
    return 0;
  });
}
