#include "drives.hpp"

#include "check.hpp"

#include <SDL3/SDL.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <memory>
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
auto OpenDriveFile(char const* value, char const* mode) -> SDL_IOStream* {
  auto [drive, path] = SplitDrive(value);
  auto props         = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  auto open          = reinterpret_cast<DriveOpen>(
      SDL_GetPointerProperty(props, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, nullptr));
  Check(open != nullptr);
  return open(drive.c_str(), path.c_str(), mode);
}
auto ListEntry(void* user, char const* directory, char const* name) -> SDL_EnumerationResult {
  auto*        storage = static_cast<SDL_Storage*>(user);
  SDL_PathInfo info    { };
  if (!SDL_GetStoragePathInfo(storage, (std::string(directory) + name).c_str(), &info)) return SDL_ENUM_FAILURE;
  SDL_Log("entry name=%s size=%llu dir=%d", name, (unsigned long long)info.size, info.type == SDL_PATHTYPE_DIRECTORY);
  return SDL_ENUM_CONTINUE;
}
auto DriveResult(bool result, char const* operation) -> bool {
  if (!result) SDL_Log("%s failed: %s", operation, SDL_GetError());
  return result;
}
auto ListDrive(char const* value) -> void {
  auto [drive, path] = SplitDrive(value);
  Check(SDL_SetHint(SDL_HINT_STORAGE_TITLE_DRIVER, "rdp"));
  auto* storage = SDL_OpenTitleStorage(drive.c_str(), 0);
  if (!storage) {
    SDL_Log("ls failed: %s", SDL_GetError());
    return;
  }
  std::unique_ptr<SDL_Storage, decltype(&SDL_CloseStorage)> owned(storage, SDL_CloseStorage);
  if (!DriveResult(SDL_StorageReady(storage), "ls")) return;
  if (!SDL_EnumerateStorageDirectory(storage, path.c_str(), ListEntry, storage)) {
    SDL_Log("ls failed: %s", SDL_GetError());
    return;
  }
  if (!SDL_CloseStorage(owned.release())) {
    SDL_Log("ls failed: %s", SDL_GetError());
    return;
  }
  SDL_Log("ls done");
}
auto ReadDigest(SDL_IOStream* file, EVP_MD_CTX* hash, uint64_t& total) -> bool {
  std::vector<unsigned char> buffer(65536);
  for (;;) {
    auto count = SDL_ReadIO(file, buffer.data(), buffer.size());
    Check(EVP_DigestUpdate(hash, buffer.data(), count) == 1);
    total += count;
    if (!count) {
      if (SDL_GetIOStatus(file) != SDL_IO_STATUS_EOF) {
        SDL_Log("cat failed: %s", SDL_GetError());
        return false;
      }
      break;
    }
  }
  return true;
}
auto LogDigest(EVP_MD_CTX* hash, uint64_t total) -> void {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest { };
  unsigned                                   size   = 0;
  Check(EVP_DigestFinal_ex(hash, digest.data(), &size) == 1);
  auto hex = std::ranges::fold_left(
      std::span(digest).first(size), std::string{ },
      [](std::string const& text, unsigned char byte) { return text + std::format("{:02x}", byte); });
  SDL_Log("cat bytes=%llu sha256=%s", (unsigned long long)total, hex.c_str());
}
auto CatDrive(char const* value) -> void {
  auto* file = OpenDriveFile(value, "rb");
  if (!file) {
    SDL_Log("cat failed: %s", SDL_GetError());
    return;
  }
  std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> owned(file, SDL_CloseIO);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> const hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  Check(hash && EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) == 1);
  uint64_t total = 0;
  if (!ReadDigest(file, hash.get(), total)) return;
  if (!SDL_CloseIO(owned.release())) {
    SDL_Log("cat failed: %s", SDL_GetError());
    return;
  }
  LogDigest(hash.get(), total);
}
auto WriteDrive(char const* value) -> void {
  auto* file = OpenDriveFile(value, "w+b");
  if (!file) {
    SDL_Log("write failed: %s", SDL_GetError());
    return;
  }
  std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> owned(file, SDL_CloseIO);
  std::vector<unsigned char> bytes(1024uz * 1024);
  std::ranges::transform(std::views::iota(0uz, bytes.size()), bytes.begin(),
                         [](size_t i) { return static_cast<unsigned char>(i % 251); });
  if (SDL_WriteIO(file, bytes.data(), bytes.size()) != bytes.size()
      || SDL_SeekIO(file, static_cast<std::ptrdiff_t>(2 * 1024) * 1024, SDL_IO_SEEK_SET)
             != static_cast<std::ptrdiff_t>(2 * 1024) * 1024
      || SDL_WriteIO(file, bytes.data(), bytes.size()) != bytes.size()) {
    SDL_Log("write failed: %s", SDL_GetError());
    return;
  }
  if (!DriveResult(SDL_CloseIO(owned.release()), "write")) return;
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
