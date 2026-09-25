#include "drive.hpp"
#include <oxbox/utilities/text.hpp>
#include <sdl-rdp/SDL3/rdp/backend/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/owneddriver.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace sdl3::rdp::storage::detail::drive {
using backend::Boundary;
using backend::Operation;
using backend::PointerState;
using backend::Stream;
using settings::Text;
namespace {
constexpr std::size_t InitialDriveCapacity = 16;
template <typename ElementTy, typename FillTy>
  requires std::invocable<FillTy const&, std::span<ElementTy>>
auto GrowUntilFits(std::size_t capacity, FillTy const& fill) -> std::vector<ElementTy> {
  std::vector<ElementTy> buffer(capacity);
  auto                   count  = std::invoke(fill, std::span(buffer));
  while (count == buffer.size()) {
    buffer.resize(buffer.size() * 2);
    count = std::invoke(fill, std::span(buffer));
  }
  buffer.resize(count);
  return buffer;
}
auto ListDrives(Driver const& driver, std::span<sdlrdp_drive> drives) -> std::size_t {
  if (!std::in_range<std::uint32_t>(drives.size())) throw std::length_error("Too many RDP drives");
  auto const count = driver.Call<Operation::DRIVE_LIST>(drives.data(),
                                                        ::Backend::Narrowed<std::uint32_t>(drives.size()));
  if (count < 0) driver.Throw();
  return std::min(static_cast<std::size_t>(count), drives.size());
}
auto Drives(Driver const& driver) -> std::vector<sdlrdp_drive> {
  return GrowUntilFits<sdlrdp_drive>(InitialDriveCapacity,
                                     [&](std::span<sdlrdp_drive> drives) { return ListDrives(driver, drives); });
}
auto FirstDrive(std::span<sdlrdp_drive const> drives) -> std::uint32_t {
  if (drives.empty()) throw std::runtime_error("No RDP drive is available");
  return drives.front().id;
}
auto NamedDrive(std::span<sdlrdp_drive const> drives, std::string const& name) -> std::uint32_t {
  auto const found = std::ranges::find_if(drives, [&](sdlrdp_drive const& drive) { return name == drive.name; });
  if (found == drives.end()) throw std::runtime_error("RDP drive unavailable: " + name);
  return found->id;
}
auto OpenHandle(Driver const& driver, std::uint32_t drive, std::string const& path, std::uint32_t flags)
    -> std::pair<std::reference_wrapper<Driver const>, sdlrdp_file*> {
  sdlrdp_file* opened{ };
  if (driver.Call<Operation::DRIVE_OPEN>(drive, path.c_str(), flags, &opened) < 0) driver.Throw();
  return { driver, opened };
}
auto CloseHandle(std::pair<std::reference_wrapper<Driver const>, sdlrdp_file*> const& handle) noexcept -> bool {
  auto const& driver = handle.first.get();
  return driver.Call<Operation::DRIVE_CLOSE>(handle.second) >= 0 || driver.Fail();
}
using DriveFileState = PointerState<std::pair<std::reference_wrapper<Driver const>, sdlrdp_file*>,
                                    &std::pair<std::reference_wrapper<Driver const>, sdlrdp_file*>::second>;
using DriveFile = utilities::RAIIWrap<std::pair<std::reference_wrapper<Driver const>, sdlrdp_file*>, OpenHandle,
                                      CloseHandle, DriveFileState::IsNull, DriveFileState::MakeNull>;
class File : private OwnedDriver<Driver const> {
public:
  using OwnedDriver<Driver const>::Backend;
       File(std::shared_ptr<Driver const> driver, std::uint32_t drive, std::string const& path, FileMode mode)
      : OwnedDriver<Driver const>{ std::move(driver) }, _file{ Backend(), drive, path, mode.Flags() }, _mode{ mode } { }
  auto Handle() const -> sdlrdp_file* {
    return _file.Get().second;
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
    return _file.Close();
  }
private:
  DriveFile    _file;
  FileMode     _mode;
  std::int64_t _position{ };
};
auto Size(File const& file) -> std::int64_t {
  sdlrdp_stat info{ };
  if (file.Backend().Call<Operation::DRIVE_FSTAT>(file.Handle(), &info) < 0)
    return file.Backend().Fail<std::int64_t>(-1);
  if (std::in_range<std::int64_t>(info.size)) return static_cast<std::int64_t>(info.size);
  SDL_SetError("RDP file exceeds signed stream size");
  return -1;
}
auto SeekBase(File const& file, SDL_IOWhence origin) -> std::int64_t {
  switch (origin) {
  case SDL_IO_SEEK_SET: return 0;
  case SDL_IO_SEEK_CUR: return file.Position();
  case SDL_IO_SEEK_END: return Size(file);
  default:              return -1;
  }
}
// SDL stream callbacks carry their owned File through an opaque context pointer.
auto SDLCALL FileSize(void* context) -> std::int64_t {
  utilities::Expects(context != nullptr, "stream size has state");
  return Size(*static_cast<File*>(context));
}
// SDL stream seek borrows its opaque state and supplies a signed offset and origin.
auto SDLCALL FileSeek(void* context, std::int64_t offset, SDL_IOWhence origin) -> std::int64_t {
  utilities::Expects(context != nullptr, "stream seek has state");
  auto&      file = *static_cast<File*>(context);
  auto const base = SeekBase(file, origin);
  if (base < 0 || offset < -base || offset > SDL_MAX_SINT64 - base) {
    SDL_SetError("Invalid RDP file seek");
    return -1;
  }
  file.Seek(base + offset);
  return file.Position();
}
auto Advance(File& file, int count, std::size_t size, SDL_IOStatus short_status)
    -> std::pair<std::size_t, SDL_IOStatus> {
  if (count < 0) return file.Backend().Fail(std::pair{ 0uz, SDL_IO_STATUS_ERROR });
  if (count > SDL_MAX_SINT64 - file.Position()) {
    SDL_SetError("RDP file exceeds signed stream position");
    return { 0, SDL_IO_STATUS_ERROR };
  }
  file.Seek(file.Position() + count);
  return { static_cast<std::size_t>(count), std::cmp_less(count, size) ? short_status : SDL_IO_STATUS_READY };
}
// SDL's stream transfer callbacks require raw counted buffers and a status output.
template <Operation OPERATION, typename ByteTy>
  requires IoBuffer<ByteTy>
auto SDLCALL Transfer(void* context, ByteTy* buffer, std::size_t size, SDL_IOStatus* status) -> std::size_t {
  utilities::Expects(context != nullptr, "stream transfer has state");
  utilities::Expects(status != nullptr, "stream transfer has a status output");
  auto&          file         = *static_cast<File*>(context);
  constexpr auto short_status = OPERATION == Operation::DRIVE_READ ? SDL_IO_STATUS_EOF : SDL_IO_STATUS_ERROR;
  if (OPERATION == Operation::DRIVE_WRITE && file.Mode().Appends() && FileSeek(context, 0, SDL_IO_SEEK_END) < 0) {
    *status = SDL_IO_STATUS_ERROR;
    return 0;
  }
  auto const count = file.Backend().Call<OPERATION>(file.Handle(), static_cast<std::uint64_t>(file.Position()), buffer,
                                                    std::min(size, static_cast<std::size_t>(SDL_MAX_SINT32)));
  auto const [bytes, outcome] = Advance(file, count, size, short_status);
  if (outcome != SDL_IO_STATUS_READY) *status = outcome;
  return bytes;
}
// SDL stream flush borrows its opaque state and provides a status output.
auto SDLCALL FileFlush(void* context, SDL_IOStatus* status) -> bool {
  utilities::Expects(context != nullptr, "stream flush has state");
  utilities::Expects(status != nullptr, "stream flush has a status output");
  auto const& file = *static_cast<File*>(context);
  if (file.Backend().Call<Operation::DRIVE_FLUSH>(file.Handle()) >= 0) return true;
  *status = SDL_IO_STATUS_ERROR;
  return file.Backend().Fail();
}
// SDL returns ownership of stream state to its close callback.
auto SDLCALL FileClose(void* context) -> bool {
  utilities::Expects(context != nullptr, "stream close owns state");
  return std::unique_ptr<File>{ static_cast<File*>(context) }->Close();
}
auto FileInterface(FileMode mode) -> SDL_IOStreamInterface {
  SDL_IOStreamInterface interface{ };
  interface.version = sizeof(interface);
  interface.size    = FileSize;
  interface.seek    = FileSeek;
  interface.read    = mode.Reads() ? Transfer<Operation::DRIVE_READ, void> : nullptr;
  interface.write   = mode.Writes() ? Transfer<Operation::DRIVE_WRITE, void const> : nullptr;
  interface.flush   = FileFlush;
  interface.close   = FileClose;
  return interface;
}
}
auto DriveName(char const* name) -> std::optional<std::string> {
  auto text = Text(name);
  if (text && text->empty()) return std::nullopt;
  return text;
}
auto DriveId(Driver const& driver, std::optional<std::string> const& name) -> std::uint32_t {
  auto const drives = Drives(driver);
  return name ? NamedDrive(drives, *name) : FirstDrive(drives);
}
auto OpenDriveFile(std::shared_ptr<Driver const> driver, std::uint32_t drive, std::string const& path, FileMode mode)
    -> Stream {
  auto       file      = std::make_unique<File>(std::move(driver), drive, path, mode);
  auto const interface = FileInterface(mode);
  Stream     stream    { &interface, file.get() };
  // The stream owns the File from here on; FileClose adopts it.
  std::ignore = file.release();
  return stream;
}
auto SDLCALL OpenFile(char const* drive, char const* path, char const* mode) -> SDL_IOStream* {
  return Boundary([&] {
    auto const file_path = Text(path);
    if (!file_path) throw std::invalid_argument("Invalid RDP file path");
    FileMode const file_mode { Text(mode).value_or("") };
    auto           driver    = Rendezvous::Acquire();
    auto const     id        = DriveId(*driver, DriveName(drive));
    return OpenDriveFile(std::move(driver), id, *file_path, file_mode).Release();
  });
}
auto UpdateDrives(Driver const& driver, SDL_PropertiesID properties) -> void {
  utilities::Expects(properties != 0, "drive publication has display properties");
  auto const names = oxbox::utilities::Joined(Drives(driver), "\n", &sdlrdp_drive::name);
  SDL_SetStringProperty(properties, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, names.c_str());
  SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, reinterpret_cast<void*>(OpenFile));
}
}
