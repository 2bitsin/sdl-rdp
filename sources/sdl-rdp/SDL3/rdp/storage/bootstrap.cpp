#include "drive.hpp"
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/resources.hpp>
#include <sdl-rdp/drive/directory-entry.hpp>
#include <sdl-rdp/drive/file-status.hpp>
#include <sdl-rdp/drive/files.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
namespace sdl3::rdp::storage::detail::bootstrap {
using sdl3::rdp::sdl::Boundary;
using sdl3::rdp::sdl::StorageHandle;
using sdl_rdp::drive::DirectoryEntry;
using sdl_rdp::drive::DriveFiles;
using sdl_rdp::drive::FileStatus;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::NotImplemented;
namespace {
constexpr std::size_t DirectoryBatch = 32;
constexpr std::size_t CopyChunkBytes = 65536;
class Storage {
public:
       Storage(std::shared_ptr<sdl3::rdp::Driver> driver, std::optional<std::string> name)
      : _driver{ std::move(driver) }, _name{ std::move(name) } { }
  auto Owner() const noexcept -> std::shared_ptr<sdl3::rdp::Driver> const& {
    return _driver;
  }
  auto Drive() const -> std::uint32_t {
    return _drive;
  }
  auto Files() -> DriveFiles {
    return _driver->Backend().Drive();
  }
  auto Resolve() -> void {
    _drive = DriveId(*_driver, _name);
  }
private:
  std::shared_ptr<sdl3::rdp::Driver> _driver;
  std::optional<std::string>         _name;
  std::uint32_t                      _drive { };
};
// SDL storage callbacks carry the Storage through an opaque context pointer.
auto Opened(void* context) -> Storage& {
  Expects(context != nullptr, "storage callback has its state");
  return *static_cast<Storage*>(context);
}
// SDL returns ownership of opaque storage state to its close callback.
auto StorageClose(void* context) -> bool {
  std::unique_ptr<Storage> const owner{ &Opened(context) };
  return true;
}
auto StorageReady(void* context) -> bool {
  auto& data = Opened(context);
  return Boundary([&] {
    data.Resolve();
    return true;
  });
}
auto PathInfo(FileStatus const& value) -> SDL_PathInfo {
  SDL_PathInfo info{ };
  info.type        = value.directory ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
  info.size        = value.size;
  info.modify_time = static_cast<SDL_Time>(SDL_SECONDS_TO_NS(value.modified));
  return info;
}
// SDL path queries supply a borrowed path and an output record.
auto StorageInfo(void* context, char const* path, SDL_PathInfo* info) -> bool {
  Expects(path != nullptr, "path query has a path");
  Expects(info != nullptr, "path query has an output");
  auto& data = Opened(context);
  return Boundary([&] {
    *info = PathInfo(data.Files().Stat(data.Drive(), path));
    return true;
  });
}
// Drive paths are client paths in UTF-8 with '/' separators, not host filesystem paths.
auto DirectoryPrefix(std::string_view path) -> std::string {
  std::string prefix{ path };
  if (!prefix.empty() && !prefix.ends_with('/')) prefix += '/';
  return prefix;
}
template <typename ConsumerTy>
concept EntryConsumer = std::is_invocable_r_v<SDL_EnumerationResult, ConsumerTy const&, DirectoryEntry const&>;
template <EntryConsumer ConsumerTy>
auto Deliver(std::span<DirectoryEntry const> entries, ConsumerTy const& consume) -> SDL_EnumerationResult {
  for (auto const& entry : entries)
    if (auto const result = consume(entry); result != SDL_ENUM_CONTINUE) return result;
  return SDL_ENUM_CONTINUE;
}
template <EntryConsumer ConsumerTy>
auto Enumerate(Storage& data, std::string_view path, ConsumerTy const& consume) -> bool {
  for (std::size_t offset{ };; offset += DirectoryBatch) {
    auto const batch  = data.Files().Enumerate(data.Drive(), path, offset, DirectoryBatch);
    auto const result = Deliver(batch, consume);
    if (result != SDL_ENUM_CONTINUE) return result == SDL_ENUM_SUCCESS;
    if (batch.size() < DirectoryBatch) return true;
  }
}
// SDL enumeration passes a borrowed path and the application's callback with its opaque context.
auto StorageEnumerate(void* context, char const* path, SDL_EnumerateDirectoryCallback callback, void* user) -> bool {
  Expects(path != nullptr, "enumeration has a path");
  Expects(callback != nullptr, "enumeration has a consumer");
  auto& data = Opened(context);
  return Boundary([&] {
    auto const directory = DirectoryPrefix(path);
    return Enumerate(
        data, path, [&](DirectoryEntry const& entry) { return callback(user, directory.c_str(), entry.name.c_str()); });
  });
}
template <ByteBuffer ByteTy> auto TransferAll(SDL_IOStream& stream, std::span<ByteTy> bytes) -> std::size_t {
  if constexpr (std::is_const_v<ByteTy>)
    return SDL_WriteIO(&stream, bytes.data(), bytes.size());
  else
    return SDL_ReadIO(&stream, bytes.data(), bytes.size());
}
// abi: SDL passes a null buffer with a zero length (SDL_storage.c), an empty transfer.
template <VoidBuffer VoidTy> auto StorageBytes(VoidTy* buffer, std::size_t length) -> std::span<BytesOf<VoidTy>> {
  if (length == 0) return { };
  Expects(buffer != nullptr, "a non-empty storage transfer has a buffer");
  return { static_cast<BytesOf<VoidTy>*>(buffer), length };
}
// SDL storage transfer callbacks provide counted raw buffers and borrowed paths.
template <VoidBuffer VoidTy>
auto StorageTransfer(void* context, char const* path, VoidTy* buffer, std::uint64_t length) -> bool {
  Expects(path != nullptr, "storage transfer has a path");
  if (!std::in_range<std::size_t>(length)) return SDL_SetError("RDP storage transfer too large");
  auto const& data  = Opened(context);
  auto const  bytes = StorageBytes(buffer, static_cast<std::size_t>(length));
  return Boundary([&] {
    FileMode const mode   { std::is_const_v<VoidTy> ? "wb" : "rb" };
    auto           file   = OpenDriveFile(data.Owner(), data.Drive(), path, mode);
    auto const     count  = TransferAll(*file.Get(), bytes);
    auto const     closed = file.Close();
    return count == length && closed;
  });
}
// SDL storage mutation callbacks supply an opaque context and one or more borrowed paths.
template <auto OPERATION, typename... PathTy>
  requires(std::same_as<PathTy, char const*> && ...)
auto StorageMutate(void* context, PathTy... path) -> bool {
  (Expects(path != nullptr, "storage mutation has its paths"), ...);
  auto& data = Opened(context);
  return Boundary([&] {
    std::invoke(OPERATION, data.Files(), data.Drive(), std::string_view{ path }...);
    return true;
  });
}
auto CopyStream(SDL_IOStream& source, SDL_IOStream& target) -> bool {
  std::array<std::uint8_t, CopyChunkBytes> buffer{ };
  for (auto count = SDL_ReadIO(&source, buffer.data(), buffer.size()); count;
       count = SDL_ReadIO(&source, buffer.data(), buffer.size()))
    if (SDL_WriteIO(&target, buffer.data(), count) != count) return false;
  return SDL_GetIOStatus(&source) == SDL_IO_STATUS_EOF;
}
// SDL copy callbacks supply an opaque context and borrowed source and target paths.
auto StorageCopy(void* context, char const* from, char const* to) -> bool {
  Expects(from != nullptr, "storage copy has a source path");
  Expects(to != nullptr, "storage copy has a target path");
  auto& data = Opened(context);
  return Boundary([&] {
    if (std::string_view{ from } == to) return SDL_SetError("RDP copy source equals destination");
    auto       source        = OpenDriveFile(data.Owner(), data.Drive(), from, FileMode{ "rb" });
    auto       target        = OpenDriveFile(data.Owner(), data.Drive(), to, FileMode{ "wb" });
    auto const copied        = CopyStream(*source.Get(), *target.Get());
    auto const target_closed = target.Close();
    auto const source_closed = source.Close();
    return copied && target_closed && source_closed;
  });
}
// SDL storage space callbacks supply their opaque state.
auto StorageSpace([[maybe_unused]] void* unused_context) -> std::uint64_t {
  NotImplemented("RDP drive redirection has no free-space query");
  constexpr std::uint64_t unknown_space = 0;
  return unknown_space;
}
// SDL's storage bootstrap lends the name and takes ownership of the returned storage.
auto StorageOpen(char const* name, [[maybe_unused]] SDL_PropertiesID unused_properties) -> SDL_Storage* {
  auto storage = Boundary([&] -> std::optional<StorageHandle> {
    auto                       data      = std::make_unique<Storage>(Rendezvous::Acquire(), DriveName(name));
    SDL_StorageInterface const interface { sizeof(SDL_StorageInterface),
                                           StorageClose,
                                           StorageReady,
                                           StorageEnumerate,
                                           StorageInfo,
                                           StorageTransfer<void>,
                                           StorageTransfer<void const>,
                                           StorageMutate<&DriveFiles::MakeDirectory, char const*>,
                                           StorageMutate<&DriveFiles::Remove, char const*>,
                                           StorageMutate<&DriveFiles::Rename, char const*, char const*>,
                                           StorageCopy,
                                           StorageSpace };
    StorageHandle              storage   { &interface, data.get() };
    // The storage owns its state from here on; StorageClose adopts it.
    std::ignore = data.release();
    return storage;
  });
  return storage ? storage->Release() : nullptr;
}
// SDL's user storage bootstrap lends organization and application names.
auto UserStorageOpen([[maybe_unused]] char const* organization, char const* app, SDL_PropertiesID properties)
    -> SDL_Storage* {
  return StorageOpen(app, properties);
}
}
// SDL's C storage tables require these named objects with static storage; C linkage names the global symbols.
extern "C" TitleStorageBootStrap const RDP_titlebootstrap = { "rdp", "RDP client drive storage", StorageOpen };
extern "C" UserStorageBootStrap const RDP_userbootstrap   = { "rdp", "RDP client drive storage", UserStorageOpen };
}
