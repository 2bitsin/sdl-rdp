#include "_detail/state.hpp"
#include <spawn.h>
#include <sys/wait.h>
#include <array>
#include <stdexcept>
#include <cstring>
extern char** environ;

namespace Backend {
Credentials EnsureCertificate(std::filesystem::path const& directory)
{
  Expects(!directory.empty(), "certificate directory is nonempty");
  static std::mutex generation_guard;
  std::scoped_lock lock(generation_guard);
  std::filesystem::create_directories(directory);
  Credentials result{directory / "server.crt", directory / "server.key"};
  if (exists(result.certificate) && exists(result.key)) return result;
  auto cert = result.certificate.string(), key = result.key.string();
  std::array<char const*, 15> args{"openssl", "req", "-x509", "-newkey", "rsa:2048",
    "-nodes", "-days", "3650", "-subj", "/CN=sdl-rdp", "-keyout", key.c_str(),
    "-out", cert.c_str(), nullptr};
  pid_t child = 0;
  auto error = posix_spawnp(&child, "openssl", nullptr, nullptr,
                            const_cast<char**>(args.data()), environ);
  if (error) throw std::runtime_error(std::format("OpenSSL spawn failed: {}.", std::strerror(error)));
  int status = 0;
  pid_t waited;
  do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited < 0) throw std::runtime_error(std::format("OpenSSL wait failed: {}.", std::strerror(errno)));
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    throw std::runtime_error(std::format("OpenSSL certificate generation failed with process status {}.", status));
  Ensures(exists(result.certificate) && exists(result.key), "credentials exist");
  return result;
}
}
