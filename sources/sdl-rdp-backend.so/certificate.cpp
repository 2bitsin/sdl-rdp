#include "_detail/state.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <pwd.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Backend {
namespace {
using Key         = std::unique_ptr<EVP_PKEY, Releases<EVP_PKEY_free>>;
using Certificate = std::unique_ptr<X509, Releases<X509_free>>;
using Bio         = std::unique_ptr<BIO, Releases<BIO_free>>;
struct DirectoryLock {
public:
  DirectoryLock(DirectoryLock const&) = delete;
  DirectoryLock(DirectoryLock&&)      = delete;
  explicit DirectoryLock(std::filesystem::path const& directory)
      : fd(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {
    if (fd < 0) throw std::runtime_error("Certificate directory open failed.");
    if (flock(fd, LOCK_EX)) {
      close(fd);
      throw std::runtime_error("Certificate directory lock failed.");
    }
  }
  ~DirectoryLock() { close(fd); }
  DirectoryLock& operator =(DirectoryLock const&) = delete;
  DirectoryLock& operator =(DirectoryLock&&) = delete;

private:
  int fd;
};
std::string Hostname() {
  std::array<char, 256> name{ };
  if (gethostname(name.data(), name.size() - 1)) throw std::runtime_error("Hostname unavailable.");
  return name.data();
}
Certificate SelfSigned(EVP_PKEY* key) {
  Expects(key != nullptr, "RSA key exists");
  Certificate cert(X509_new());
  if (!cert) throw std::runtime_error("Certificate allocation failed.");
  auto* name = X509_get_subject_name(cert.get());
  auto  host = Hostname();
  auto  san  = "DNS:" + host;
  std::unique_ptr<X509_EXTENSION, Releases<X509_EXTENSION_free>> const extension(
      X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, san.c_str()));
  if (!X509_set_version(cert.get(), 2) || !ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) ||
      !X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0) ||
      !X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3650L * 86400) || !X509_set_pubkey(cert.get(), key) ||
      !X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<unsigned char const*>(host.c_str()), -1,
                                  -1, 0) ||
      !X509_set_issuer_name(cert.get(), name) || !extension || !X509_add_ext(cert.get(), extension.get(), -1) ||
      !X509_sign(cert.get(), key, EVP_sha256()))
    throw std::runtime_error("Certificate signing failed.");
  return cert;
}
void Generate(Credentials const& paths) {
  Expects(!paths.key.empty(), "private key path is supplied");
  Expects(!paths.certificate.empty(), "certificate path is supplied");
  Key const key(EVP_RSA_gen(2048));
  if (!key) throw std::runtime_error("RSA key generation failed.");
  auto cert = SelfSigned(key.get());
  auto fd   = open(paths.key.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  Bio const key_file(fd < 0 ? nullptr : BIO_new_fd(fd, BIO_CLOSE));
  if (key_file)
    std::filesystem::permissions(paths.key, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  Bio const cert_file(BIO_new_file(paths.certificate.c_str(), "w"));
  if (!key_file || !cert_file ||
      !PEM_write_bio_PrivateKey(key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) ||
      !PEM_write_bio_X509(cert_file.get(), cert.get()))
    throw std::runtime_error("Credential writing failed.");
}
}
std::filesystem::path DefaultCertificateDirectory() {
  if (auto* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "sdl-rdp";
  if (auto* home = std::getenv("HOME"); home && *home) return std::filesystem::path(home) / ".local/share/sdl-rdp";
  std::array<char, 16384> buffer{ };
  passwd                  entry { };
  passwd*                 found  = nullptr;
  if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &found) || !found)
    throw std::runtime_error("User home directory unavailable.");
  return std::filesystem::path(entry.pw_dir) / ".local/share/sdl-rdp";
}
Credentials EnsureCertificate(std::filesystem::path const& directory) {
  Expects(!directory.empty(), "certificate directory is nonempty");
  static std::mutex generation_guard;
  std::scoped_lock const lock(generation_guard);
  if (!directory.parent_path().empty()) std::filesystem::create_directories(directory.parent_path());
  if (mkdir(directory.c_str(), 0700) && errno != EEXIST)
    throw std::runtime_error("Certificate directory creation failed.");
  DirectoryLock const process_lock(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  Credentials result{ .certificate = directory / "server.crt", .key = directory / "server.key" };
  if (!exists(result.certificate) || !exists(result.key)) Generate(result);
  std::filesystem::permissions(result.key, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  Ensures(exists(result.certificate), "credentials exist");
  Ensures(exists(result.key), "credentials exist");
  return result;
}
}
