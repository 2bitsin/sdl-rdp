#include "SDL_rdpdrive.hpp"
#include "boundary.hpp"
#include <span>
namespace rdp {
namespace {
constexpr std::size_t DirectoryBatch = 32;
constexpr std::size_t CopyChunkBytes = 65536;
class Storage {
public:
       Storage(std::shared_ptr<Driver const> driver, std::optional<std::string> name)
      : _driver{std::move(driver)}, _name{std::move(name)} { }
  auto Backend() const -> Driver const& { return *_driver; }
  auto Owner() const   -> std::shared_ptr<Driver const> const& { return _driver; }
  auto Drive() const   -> unsigned { return _drive; }
  void Resolve() { _drive = DriveId(*_driver, _name); }
private:
  std::shared_ptr<Driver const> _driver;
  std::optional<std::string>    _name;
  unsigned                      _drive { };
};
// SDL storage callbacks carry the Storage through an opaque context pointer.
auto Opened(void* context) -> Storage& {
  utilities::Expects(context != nullptr, "storage callback has its state");
  return *static_cast<Storage*>(context);
}
// SDL returns ownership of opaque storage state to its close callback.
bool StorageClose(void* context) {
  std::unique_ptr<Storage> const owner{ &Opened(context) };
  return true;
}
bool StorageReady(void* context) {
  return Boundary([&] {
    Opened(context).Resolve();
    return true;
  });
}
// SDL path queries supply a borrowed path and an ABI output record.
bool StorageInfo(void* context, char const* path, SDL_PathInfo* info) {
  utilities::Expects(path != nullptr, "path query has a path");
  utilities::Expects(info != nullptr, "path query has an output");
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
auto ReadDirectory(Storage const& data, std::string const& path, unsigned offset, std::span<sdlrdp_dirent> entries)
    -> std::span<sdlrdp_dirent const> {
  auto const count = data.Backend().Call<Operation::DRIVE_ENUMERATE>(data.Drive(), path.c_str(), offset, entries.data(),
                                                                      static_cast<unsigned>(entries.size()));
  if (count < 0) data.Backend().Throw();
  utilities::Ensures(std::cmp_less_equal(count, entries.size()), "backend fills at most the directory buffer");
  return entries.first(static_cast<std::size_t>(count));
}
// SDL enumeration passes a borrowed path and callback with an opaque application context.
auto Deliver(std::span<sdlrdp_dirent const> entries, std::string const& directory,
             SDL_EnumerateDirectoryCallback callback, void* user) -> SDL_EnumerationResult {
  for (auto const& entry : entries)
    if (auto const result = callback(user, directory.c_str(), entry.name); result != SDL_ENUM_CONTINUE) return result;
  return SDL_ENUM_CONTINUE;
}
auto Enumerate(Storage const& data, std::string const& path, SDL_EnumerateDirectoryCallback callback, void* user)
    -> bool {
  auto const                                directory = DirectoryPrefix(path);
  std::array<sdlrdp_dirent, DirectoryBatch> entries   { };
  for (unsigned offset{ };; offset += static_cast<unsigned>(entries.size())) {
    auto const batch  = ReadDirectory(data, path, offset, entries);
    auto const result = Deliver(batch, directory, callback, user);
    if (result != SDL_ENUM_CONTINUE) return result == SDL_ENUM_SUCCESS;
    if (batch.size() < entries.size()) return true;
  }
}
bool StorageEnumerate(void* context, char const* path, SDL_EnumerateDirectoryCallback callback, void* user) {
  utilities::Expects(path != nullptr, "enumeration has a path");
  utilities::Expects(callback != nullptr, "enumeration has a consumer");
  auto const& data = Opened(context);
  return Boundary([&] { return Enumerate(data, std::string{path}, callback, user); });
}
template<typename _Byte>
  requires IoBuffer<_Byte>
auto TransferAll(SDL_IOStream& stream, _Byte* buffer, std::size_t length) -> std::size_t {
  if constexpr (std::is_const_v<_Byte>) return SDL_WriteIO(&stream, buffer, length);
  else return SDL_ReadIO(&stream, buffer, length);
}
// SDL storage transfer callbacks provide counted raw buffers and borrowed paths.
template<typename _Byte>
  requires IoBuffer<_Byte>
bool StorageTransfer(void* context, char const* path, _Byte* buffer, Uint64 length) {
  utilities::Expects(path != nullptr, "storage transfer has a path");
  auto const& data = Opened(context);
  return Boundary([&] {
    if (!std::in_range<std::size_t>(length)) return SDL_SetError("RDP storage transfer too large");
    FileMode const mode   { std::is_const_v<_Byte> ? "wb" : "rb" };
    auto           file   = OpenDriveFile(data.Owner(), data.Drive(), path, mode);
    auto const     count  = TransferAll(*file.Get(), buffer, static_cast<std::size_t>(length));
    auto const     closed = file.Close();
    return count == length && closed;
  });
}
// SDL storage mutation callbacks supply an opaque context and one or more borrowed paths.
template<Operation _Operation, typename... _Path>
  requires (std::same_as<_Path, char const*> && ...)
bool StorageMutate(void* context, _Path... path) {
  (utilities::Expects(path != nullptr, "storage mutation has its paths"), ...);
  auto const& data = Opened(context);
  return data.Backend().Call<_Operation>(data.Drive(), path...) >= 0 || data.Backend().Fail();
}
auto CopyStream(SDL_IOStream& source, SDL_IOStream& target) -> bool {
  std::array<Uint8, CopyChunkBytes> buffer{ };
  for (auto count = SDL_ReadIO(&source, buffer.data(), buffer.size()); count;
       count = SDL_ReadIO(&source, buffer.data(), buffer.size()))
    if (SDL_WriteIO(&target, buffer.data(), count) != count) return false;
  return SDL_GetIOStatus(&source) == SDL_IO_STATUS_EOF;
}
// SDL copy callbacks supply an opaque context and borrowed source and target paths.
bool StorageCopy(void* context, char const* from, char const* to) {
  utilities::Expects(from != nullptr, "storage copy has a source path");
  utilities::Expects(to != nullptr, "storage copy has a target path");
  auto const& data = Opened(context);
  return Boundary([&] {
    if (std::string_view{from} == to) return SDL_SetError("RDP copy source equals destination");
    auto       source        = OpenDriveFile(data.Owner(), data.Drive(), from, FileMode{"rb"});
    auto       target        = OpenDriveFile(data.Owner(), data.Drive(), to, FileMode{"wb"});
    auto const copied        = CopyStream(*source.Get(), *target.Get());
    auto const target_closed = target.Close();
    auto const source_closed = source.Close();
    return copied && target_closed && source_closed;
  });
}
// SDL storage space callbacks supply their opaque state.
Uint64 StorageSpace([[maybe_unused]] void* unused_context) {
  utilities::NotImplemented("RDP backend ABI has no free-space query");
  constexpr Uint64 unknown_space = 0;
  return unknown_space;
}
// SDL's storage bootstrap lends the name and takes ownership of the returned storage.
SDL_Storage* StorageOpen(char const* name, [[maybe_unused]] SDL_PropertiesID unused_properties) {
  return Boundary([&] {
    auto                       data      = std::make_unique<Storage>(Rendezvous::Acquire(), DriveName(name));
    SDL_StorageInterface const interface { sizeof(SDL_StorageInterface), StorageClose, StorageReady, StorageEnumerate,
        StorageInfo, StorageTransfer<void>, StorageTransfer<void const>,
        StorageMutate<Operation::DRIVE_MKDIR, char const*>, StorageMutate<Operation::DRIVE_REMOVE, char const*>,
        StorageMutate<Operation::DRIVE_RENAME, char const*, char const*>, StorageCopy, StorageSpace};
    StorageHandle              storage   { &interface, data.get() };
    // The storage owns its state from here on; StorageClose adopts it.
    std::ignore = data.release();
    return storage.Release();
  });
}
// SDL's user storage bootstrap lends organization and application names.
SDL_Storage* UserStorageOpen([[maybe_unused]] char const* organization, char const* app, SDL_PropertiesID properties) {
  return StorageOpen(app, properties);
}
}
}
// SDL's C title storage table requires this named object with static storage.
extern "C" TitleStorageBootStrap const RDP_titlebootstrap = {"rdp", "RDP client drive storage", rdp::StorageOpen};
// SDL's C user storage table requires this named object with static storage.
extern "C" UserStorageBootStrap const RDP_userbootstrap   = {"rdp", "RDP client drive storage", rdp::UserStorageOpen};
