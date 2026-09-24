#include "drives.hpp"

#include "check.hpp"

#include <SDL3/SDL.h>
#include <openssl/evp.h>
#include <oxbox/utilities/hex.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace {
using DriveOpen = SDL_IOStream*(SDLCALL*)(char const*, char const*, char const*);
auto SplitDrive(char const* value) -> std::pair<std::string, std::string> {
  std::string const path(value);
  auto              slash = path.find('/');
  return { path.substr(0, slash), slash == std::string::npos ? "" : path.substr(slash + 1) };
}
using IoStream    = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;
using DigestState = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using Storage     = std::unique_ptr<SDL_Storage, decltype(&SDL_CloseStorage)>;
auto DriveResult(bool result, char const* operation) -> bool {
  if (!result) SDL_Log("%s failed: %s", operation, SDL_GetError());
  return result;
}
auto OpenDriveFile(char const* value, char const* mode, char const* operation) -> IoStream {
  auto [drive, path] = SplitDrive(value);
  auto props         = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  // The display property carries the driver's file factory as an untyped pointer.
  auto open          = reinterpret_cast<DriveOpen>(
      SDL_GetPointerProperty(props, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, nullptr));
  Check(open != nullptr);
  IoStream file{ open(drive.c_str(), path.c_str(), mode), SDL_CloseIO };
  DriveResult(file != nullptr, operation);
  return file;
}
auto CloseDriveFile(IoStream file, char const* operation) -> bool {
  return DriveResult(SDL_CloseIO(file.release()), operation);
}
auto ListEntry(void* user, char const* directory, char const* name) -> SDL_EnumerationResult {
  auto*        storage = static_cast<SDL_Storage*>(user);
  SDL_PathInfo info    { };
  if (!SDL_GetStoragePathInfo(storage, (std::string(directory) + name).c_str(), &info)) return SDL_ENUM_FAILURE;
  SDL_Log("entry name=%s size=%" SDL_PRIu64 " dir=%d", name, info.size, info.type == SDL_PATHTYPE_DIRECTORY);
  return SDL_ENUM_CONTINUE;
}
auto ListDrive(char const* value) -> void {
  auto [drive, path] = SplitDrive(value);
  Check(SDL_SetHint(SDL_HINT_STORAGE_TITLE_DRIVER, "rdp"));
  Storage storage(SDL_OpenTitleStorage(drive.c_str(), 0), SDL_CloseStorage);
  if (!DriveResult(storage != nullptr, "ls") || !DriveResult(SDL_StorageReady(storage.get()), "ls")
      || !DriveResult(SDL_EnumerateStorageDirectory(storage.get(), path.c_str(), ListEntry, storage.get()), "ls")
      || !DriveResult(SDL_CloseStorage(storage.release()), "ls"))
    return;
  SDL_Log("ls done");
}
auto ReadDigest(SDL_IOStream& file, EVP_MD_CTX& hash) -> std::optional<std::uint64_t> {
  std::vector<std::byte> buffer(65536);
  std::uint64_t          total  = 0;
  while (auto const count = SDL_ReadIO(&file, buffer.data(), buffer.size())) {
    Check(EVP_DigestUpdate(&hash, buffer.data(), count) == 1);
    total += count;
  }
  if (!DriveResult(SDL_GetIOStatus(&file) == SDL_IO_STATUS_EOF, "cat")) return std::nullopt;
  return total;
}
auto LogDigest(EVP_MD_CTX& hash, std::uint64_t total) -> void {
  std::array<std::uint8_t, EVP_MAX_MD_SIZE> digest { };
  std::uint32_t                             size   = 0;
  Check(EVP_DigestFinal_ex(&hash, digest.data(), &size) == 1);
  auto const hex = oxbox::utilities::ToHex(std::as_bytes(std::span(digest).first(size)));
  SDL_Log("cat bytes=%" SDL_PRIu64 " sha256=%s", total, hex.c_str());
}
auto CatDrive(char const* value) -> void {
  auto file = OpenDriveFile(value, "rb", "cat");
  if (!file) return;
  DigestState const hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  Check(hash && EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) == 1);
  auto const total = ReadDigest(*file, *hash);
  if (!total || !CloseDriveFile(std::move(file), "cat")) return;
  LogDigest(*hash, *total);
}
auto WriteTwice(SDL_IOStream& file, std::span<std::byte const> bytes) -> bool {
  constexpr std::ptrdiff_t Gap = std::ptrdiff_t{ 2 } * 1024 * 1024;
  return SDL_WriteIO(&file, bytes.data(), bytes.size()) == bytes.size()
         && SDL_SeekIO(&file, Gap, SDL_IO_SEEK_SET) == Gap
         && SDL_WriteIO(&file, bytes.data(), bytes.size()) == bytes.size();
}
auto WriteDrive(char const* value) -> void {
  auto file = OpenDriveFile(value, "w+b", "write");
  if (!file) return;
  std::vector<std::byte> bytes(1024uz * 1024);
  std::ranges::transform(std::views::iota(0uz, bytes.size()), bytes.begin(),
                         [](std::size_t i) { return static_cast<std::byte>(i % 251); });
  if (!DriveResult(WriteTwice(*file, bytes), "write") || !CloseDriveFile(std::move(file), "write")) return;
  SDL_Log("write done");
}
}
auto RunDrives(DriveOptions const& options) -> bool {
  if (!options.list && !options.cat && !options.write) return false;
  auto props = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  if (!*SDL_GetStringProperty(props, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, "")) return false;
  if (options.list) ListDrive(options.list);
  if (options.cat) CatDrive(options.cat);
  if (options.write) WriteDrive(options.write);
  return true;
}
