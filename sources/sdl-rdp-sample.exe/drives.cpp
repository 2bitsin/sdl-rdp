#include <cstddef>
#include "drives.hpp"
#include "_detail/check.hpp"
#include <SDL3/SDL.h>
#include <openssl/evp.h>
#include <string>
#include <vector>
#include <memory>
#include <format>

namespace {
using DriveOpen = SDL_IOStream*(SDLCALL*)(const char*, const char*, const char*);
std::pair<std::string, std::string> SplitDrive(char const* value)
{
  std::string const path(value);
  auto slash = path.find('/');
  return { path.substr(0, slash), slash == std::string::npos ? "" : path.substr(slash + 1) };
}
SDL_IOStream* OpenDriveFile(char const* value, char const* mode)
{
  auto [drive, path] = SplitDrive(value);
  auto props = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  auto open  = reinterpret_cast<DriveOpen>(SDL_GetPointerProperty(props, SDL_PROP_DISPLAY_RDP_OPEN_FILE_POINTER, nullptr));
  Check(open != nullptr);
  return open(drive.c_str(), path.c_str(), mode);
}
void ListDrive(char const* value)
{
  auto [drive, path] = SplitDrive(value);
  Check(SDL_SetHint(SDL_HINT_STORAGE_TITLE_DRIVER, "rdp"));
  auto* storage = SDL_OpenTitleStorage(drive.c_str(), 0);
  if (!storage) {
    SDL_Log("ls failed: %s", SDL_GetError());
    return;
  }
  std::unique_ptr<SDL_Storage, decltype(&SDL_CloseStorage)> owned(storage, SDL_CloseStorage);
  if (!SDL_StorageReady(storage)) {
    SDL_Log("ls failed: %s", SDL_GetError());
    return;
  }
  if (!SDL_EnumerateStorageDirectory(storage, path.c_str(), [](void* user, char const* directory, char const* name) -> SDL_EnumerationResult {
        auto *storage = static_cast<SDL_Storage*>(user);
        SDL_PathInfo info{ };
        if (!SDL_GetStoragePathInfo(storage, (std::string(directory) + name).c_str(), &info)) return SDL_ENUM_FAILURE;
        SDL_Log("entry name=%s size=%llu dir=%d", name, (unsigned long long)info.size, info.type == SDL_PATHTYPE_DIRECTORY);
        return SDL_ENUM_CONTINUE; }, storage)) {
    SDL_Log("ls failed: %s", SDL_GetError());
    return;
  }
  if (!SDL_CloseStorage(owned.release())) {
    SDL_Log("ls failed: %s", SDL_GetError());
    return;
  }
  SDL_Log("ls done");
}
void CatDrive(char const* value)
{
  auto* file = OpenDriveFile(value, "rb");
  if (!file) {
    SDL_Log("cat failed: %s", SDL_GetError());
    return;
  }
  std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>         owned(file, SDL_CloseIO);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> const hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  Check(hash && EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) == 1);
  std::vector<unsigned char> buffer(65536);
  uint64_t total = 0;
  for (;;) {
    auto count = SDL_ReadIO(file, buffer.data(), buffer.size());
    Check(EVP_DigestUpdate(hash.get(), buffer.data(), count) == 1);
    total += count;
    if (!count) {
      if (SDL_GetIOStatus(file) != SDL_IO_STATUS_EOF) {
        SDL_Log("cat failed: %s", SDL_GetError());
        return;
      }
      break;
    }
  }
  if (!SDL_CloseIO(owned.release())) {
    SDL_Log("cat failed: %s", SDL_GetError());
    return;
  }
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned size = 0;
  Check(EVP_DigestFinal_ex(hash.get(), digest, &size) == 1);
  std::string hex;
  for (unsigned i = 0; i < size; ++i) hex += std::format("{:02x}", digest[i]);
  SDL_Log("cat bytes=%llu sha256=%s", (unsigned long long)total, hex.c_str());
}
void WriteDrive(char const* value)
{
  auto* file = OpenDriveFile(value, "w+b");
  if (!file) {
    SDL_Log("write failed: %s", SDL_GetError());
    return;
  }
  std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> owned(file, SDL_CloseIO);
  std::vector<unsigned char>                            bytes(1024uz * 1024);
  for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i % 251);
  if (SDL_WriteIO(file, bytes.data(), bytes.size()) != bytes.size() ||
      SDL_SeekIO(file, static_cast<std::ptrdiff_t>(2 * 1024) * 1024, SDL_IO_SEEK_SET) != static_cast<std::ptrdiff_t>(2 * 1024) * 1024 ||
      SDL_WriteIO(file, bytes.data(), bytes.size()) != bytes.size()) {
    SDL_Log("write failed: %s", SDL_GetError());
    return;
  }
  if (!SDL_CloseIO(owned.release())) {
    SDL_Log("write failed: %s", SDL_GetError());
    return;
  }
  SDL_Log("write done");
}
}
bool RunDrives(DriveOptions const& options)
{
  if (!options.list && !options.cat && !options.write) return false;
  auto props = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  if (!*SDL_GetStringProperty(props, SDL_PROP_DISPLAY_RDP_DRIVES_STRING, "")) return false;
  if (options.list) ListDrive(options.list);
  if (options.cat) CatDrive(options.cat);
  if (options.write) WriteDrive(options.write);
  return true;
}
