#include "drive.hpp"
#include <sdl-rdp/SDL3/rdp/backend/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/owneddriver.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/void-buffer.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
namespace sdl3::rdp::storage::detail::bootstrap {
using sdl3::rdp::backend::Boundary;
using sdl3::rdp::backend::Operation;
using sdl3::rdp::backend::StorageHandle;
using sdl_rdp::utilities::ByteBuffer;
using sdl_rdp::utilities::BytesOf;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::NotImplemented;
using sdl_rdp::utilities::VoidBuffer;
namespace {
constexpr std::size_t DirectoryBatch = 32;
constexpr std::size_t CopyChunkBytes = 65536;
class Storage : private OwnedDriver<Driver const> {
public:
  using OwnedDriver<Driver const>::Backend;
  using OwnedDriver<Driver const>::Owner;
       Storage(std::shared_ptr<Driver const> driver, std::optional<std::string> name)
      : OwnedDriver<Driver const>{ std::move(driver) }, _name{ std::move(name) } { }
  auto Drive() const -> std::uint32_t {
    return _drive;
  }
  auto Resolve() -> void {
    _drive = DriveId(Backend(), _name);
  }
private:
  std::optional<std::string> _name;
  std::uint32_t              _drive{ };
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
// SDL path queries supply a borrowed path and an ABI output record.
auto StorageInfo(void* context, char const* path, SDL_PathInfo* info) -> bool {
  Expects(path != nullptr, "path query has a path");
  Expects(info != nullptr, "path query has an output");
  auto const& data  = Opened(context);
  sdlrdp_stat value { };
  if (data.Backend().Call<Operation::DRIVE_STAT>(data.Drive(), path, &value) < 0) return data.Backend().Fail();
  *info             = { };
  info->type        = value.directory ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
  info->size        = value.size;
  info->modify_time = static_cast<SDL_Time>(SDL_SECONDS_TO_NS(value.modified));
  return true;
}
// Drive paths are client paths in UTF-8 with '/' separators, not host filesystem paths.
auto DirectoryPrefix(std::string_view path) -> std::string {
  std::string prefix{ path };
  if (!prefix.empty() && !prefix.ends_with('/')) prefix += '/';
  return prefix;
}
auto ReadDirectory(Storage const& data, std::string const& path, std::uint32_t offset, std::span<sdlrdp_dirent> entries)
    -> std::span<sdlrdp_dirent const> {
  auto const count = data.Backend().Call<Operation::DRIVE_ENUMERATE>(data.Drive(), path.c_str(), offset, entries.data(),
                                                                     Narrowed<std::uint32_t>(entries.size()));
  if (count < 0) data.Backend().Throw();
  Ensures(std::cmp_less_equal(count, entries.size()), "backend fills at most the directory buffer");
  return entries.first(static_cast<std::size_t>(count));
}
template <typename ConsumerTy>
concept EntryConsumer = std::is_invocable_r_v<SDL_EnumerationResult, ConsumerTy const&, sdlrdp_dirent const&>;
template <EntryConsumer ConsumerTy>
auto Deliver(std::span<sdlrdp_dirent const> entries, ConsumerTy const& consume) -> SDL_EnumerationResult {
  for (auto const& entry : entries)
    if (auto const result = consume(entry); result != SDL_ENUM_CONTINUE) return result;
  return SDL_ENUM_CONTINUE;
}
template <EntryConsumer ConsumerTy>
auto Enumerate(Storage const& data, std::string const& path, ConsumerTy const& consume) -> bool {
  std::array<sdlrdp_dirent, DirectoryBatch> entries{ };
  for (std::uint32_t offset{ };; offset += Narrowed<std::uint32_t>(entries.size())) {
    auto const batch  = ReadDirectory(data, path, offset, entries);
    auto const result = Deliver(batch, consume);
    if (result != SDL_ENUM_CONTINUE) return result == SDL_ENUM_SUCCESS;
    if (batch.size() < entries.size()) return true;
  }
}
// SDL enumeration passes a borrowed path and the application's callback with its opaque context.
auto StorageEnumerate(void* context, char const* path, SDL_EnumerateDirectoryCallback callback, void* user) -> bool {
  Expects(path != nullptr, "enumeration has a path");
  Expects(callback != nullptr, "enumeration has a consumer");
  auto const& data = Opened(context);
  return Boundary([&] {
    auto const directory = DirectoryPrefix(path);
    return Enumerate(data, path,
                     [&](sdlrdp_dirent const& entry) { return callback(user, directory.c_str(), entry.name); });
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
template <Operation OPERATION, typename... PathTy>
  requires(std::same_as<PathTy, char const*> && ...)
auto StorageMutate(void* context, PathTy... path) -> bool {
  (Expects(path != nullptr, "storage mutation has its paths"), ...);
  auto const& data = Opened(context);
  return data.Backend().Call<OPERATION>(data.Drive(), path...) >= 0 || data.Backend().Fail();
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
  auto const& data = Opened(context);
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
  NotImplemented("RDP backend ABI has no free-space query");
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
                                           StorageMutate<Operation::DRIVE_MKDIR, char const*>,
                                           StorageMutate<Operation::DRIVE_REMOVE, char const*>,
                                           StorageMutate<Operation::DRIVE_RENAME, char const*, char const*>,
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
