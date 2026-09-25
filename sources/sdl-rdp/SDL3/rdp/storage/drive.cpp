#include "drive.hpp"
#include <oxbox/utilities/text.hpp>
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <sdl-rdp/SDL3/rdp/owneddriver.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/drive/drive.hpp>
#include <sdl-rdp/drive/file.hpp>
#include <sdl-rdp/drive/files.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/void-buffer.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>
namespace sdl3::rdp::storage::detail::drive {
using sdl3::rdp::sdl::Boundary;
using sdl3::rdp::sdl::Stream;
using sdl3::rdp::settings::Text;
using sdl_rdp::drive::Drive;
using sdl_rdp::drive::FileKind;
using sdl_rdp::utilities::BytesOf;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::VoidBuffer;
namespace {
auto FirstDrive(std::span<Drive const> drives) -> std::uint32_t {
  if (drives.empty()) throw DriveUnavailable{ "none is shared" };
  return drives.front().id;
}
auto NamedDrive(std::span<Drive const> drives, std::string const& name) -> std::uint32_t {
  auto const found = std::ranges::find(drives, name, &Drive::name);
  if (found == drives.end()) throw DriveUnavailable{ name };
  return found->id;
}
// One open drive file behind an SDL stream; the stream position is SDL's, the file's offset is per transfer.
class StreamFile : private OwnedDriver {
public:
  using OwnedDriver::Driver;
  StreamFile(std::shared_ptr<sdl3::rdp::Driver> driver, std::uint32_t drive, std::string const& path, FileMode mode)
      : OwnedDriver{ std::move(driver) },
        _file{ Driver().Backend().Drive().Open(drive, path, mode.Access(), FileKind::File) }, _mode{ mode } { }
  auto Current() -> sdl_rdp::drive::File& {
    return Driver().Backend().Drive().Attached(*_file);
  }
  auto Mode() const -> FileMode {
    return _mode;
  }
  auto Position() const -> std::int64_t {
    return _position;
  }
  auto Seek(std::int64_t position) -> void {
    _position = position;
  }
  auto Close() -> bool {
    return Boundary([&] {
      Current().Close();
      return true;
    });
  }
private:
  std::unique_ptr<sdl_rdp::drive::File> _file;
  FileMode                              _mode;
  std::int64_t                          _position{ };
};
auto Size(StreamFile& file) -> std::int64_t {
  return Boundary(
      [&] -> std::int64_t {
        auto const info = file.Current().Stat();
        if (std::in_range<std::int64_t>(info.size)) return static_cast<std::int64_t>(info.size);
        SDL_SetError("RDP file exceeds signed stream size");
        return -1;
      },
      std::int64_t{ -1 });
}
auto SeekBase(StreamFile& file, SDL_IOWhence origin) -> std::int64_t {
  switch (origin) {
  case SDL_IO_SEEK_SET: return 0;
  case SDL_IO_SEEK_CUR: return file.Position();
  case SDL_IO_SEEK_END: return Size(file);
  default:              return -1;
  }
}
// SDL stream callbacks carry their owned StreamFile through an opaque context pointer.
auto SDLCALL FileSize(void* context) -> std::int64_t {
  Expects(context != nullptr, "stream size has state");
  return Size(*static_cast<StreamFile*>(context));
}
// SDL stream seek borrows its opaque state and supplies a signed offset and origin.
auto SDLCALL FileSeek(void* context, std::int64_t offset, SDL_IOWhence origin) -> std::int64_t {
  Expects(context != nullptr, "stream seek has state");
  auto&      file = *static_cast<StreamFile*>(context);
  auto const base = SeekBase(file, origin);
  if (base < 0 || offset < -base || offset > SDL_MAX_SINT64 - base) {
    SDL_SetError("Invalid RDP file seek");
    return -1;
  }
  file.Seek(base + offset);
  return file.Position();
}
auto Advance(StreamFile& file, std::size_t count, std::size_t size, SDL_IOStatus short_status)
    -> std::pair<std::size_t, SDL_IOStatus> {
  if (std::cmp_greater(count, SDL_MAX_SINT64 - file.Position())) {
    SDL_SetError("RDP file exceeds signed stream position");
    return { 0, SDL_IO_STATUS_ERROR };
  }
  file.Seek(file.Position() + static_cast<std::int64_t>(count));
  return { count, count < size ? short_status : SDL_IO_STATUS_READY };
}
template <VoidBuffer VoidTy>
auto TransferBytes(StreamFile& file, std::span<BytesOf<VoidTy>> bytes) -> std::size_t {
  return file.Current().Transfer(static_cast<std::uint64_t>(file.Position()), bytes);
}
// SDL's stream transfer callbacks require raw counted buffers and a status output.
template <VoidBuffer VoidTy>
auto SDLCALL Transfer(void* context, VoidTy* buffer, std::size_t size, SDL_IOStatus* status) -> std::size_t {
  Expects(context != nullptr, "stream transfer has state");
  Expects(buffer != nullptr, "stream transfer has a buffer");
  Expects(status != nullptr, "stream transfer has a status output");
  constexpr auto writing      = std::is_const_v<VoidTy>;
  constexpr auto short_status = writing ? SDL_IO_STATUS_ERROR : SDL_IO_STATUS_EOF;
  auto&          file         = *static_cast<StreamFile*>(context);
  if (writing && file.Mode().Appends() && FileSeek(context, 0, SDL_IO_SEEK_END) < 0) {
    *status = SDL_IO_STATUS_ERROR;
    return 0;
  }
  auto const [bytes, outcome] = Boundary(
      [&] {
        auto const limit = std::min(size, static_cast<std::size_t>(SDL_MAX_SINT32));
        auto const count = TransferBytes<VoidTy>(file, { static_cast<BytesOf<VoidTy>*>(buffer), limit });
        return Advance(file, count, size, short_status);
      },
      std::pair{ 0uz, SDL_IO_STATUS_ERROR });
  if (outcome != SDL_IO_STATUS_READY) *status = outcome;
  return bytes;
}
// SDL returns ownership of stream state to its close callback.
auto SDLCALL FileClose(void* context) -> bool {
  Expects(context != nullptr, "stream close owns state");
  return std::unique_ptr<StreamFile>{ static_cast<StreamFile*>(context) }->Close();
}
auto FileInterface(FileMode mode) -> SDL_IOStreamInterface {
  SDL_IOStreamInterface interface{ };
  // No flush slot: FreeRDP 3.32 drive_main.c:754 has no FLUSH_BUFFERS case; writes are synchronous.
  interface.version = sizeof(interface);
  interface.size    = FileSize;
  interface.seek    = FileSeek;
  interface.read    = mode.Reads() ? Transfer<void> : nullptr;
  interface.write   = mode.Writes() ? Transfer<void const> : nullptr;
  interface.close   = FileClose;
  return interface;
}
}
auto DriveName(char const* name) -> std::optional<std::string> {
  auto text = Text(name);
  if (text && text->empty()) return std::nullopt;
  return text;
}
auto DriveId(Driver& driver, std::optional<std::string> const& name) -> std::uint32_t {
  auto const drives = driver.Backend().Drive().List();
  return name ? NamedDrive(drives, *name) : FirstDrive(drives);
}
auto OpenDriveFile(std::shared_ptr<Driver> driver, std::uint32_t drive, std::string const& path, FileMode mode)
    -> Stream {
  auto       file      = std::make_unique<StreamFile>(std::move(driver), drive, path, mode);
  auto const interface = FileInterface(mode);
  Stream     stream    { &interface, file.get() };
  // The stream owns the StreamFile from here on; FileClose adopts it.
  std::ignore = file.release();
  return stream;
}
auto SDLCALL OpenFile(char const* drive, char const* path, char const* mode) -> SDL_IOStream* {
  auto stream = Boundary([&] -> std::optional<Stream> {
    auto const file_path = Text(path);
    if (!file_path) throw InvalidFilePath{ };
    FileMode const file_mode { Text(mode).value_or("") };
    auto           driver    = Rendezvous::Acquire();
    auto const     id        = DriveId(*driver, DriveName(drive));
    return OpenDriveFile(std::move(driver), id, *file_path, file_mode);
  });
  return stream ? stream->Release() : nullptr;
}
auto UpdateDrives(Driver& driver, SDL_PropertiesID properties) -> void {
  Expects(properties != 0, "drive publication has display properties");
  auto const names = oxbox::utilities::Joined(driver.Backend().Drive().List(), "\n", &Drive::name);
  SDL_SetStringProperty(properties, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, names.c_str());
  SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, reinterpret_cast<void*>(OpenFile));
}
}
