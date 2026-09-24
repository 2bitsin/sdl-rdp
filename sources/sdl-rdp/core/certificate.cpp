#include <sdl-rdp/core/certificate.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/descriptor.hpp>
#include <sdl-rdp/utilities/system-call.hpp>

#include <freerdp/crypto/certificate.h>
#include <freerdp/crypto/privatekey.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <oxbox/utilities/span.hpp>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace Backend {
using utilities::Ensures;
using utilities::Expects;
namespace {
using Key         = std::unique_ptr<EVP_PKEY, Releases<EVP_PKEY_free>>;
using Certificate = std::unique_ptr<X509, Releases<X509_free>>;
using ServerKey   = std::unique_ptr<rdpPrivateKey, Releases<freerdp_key_free>>;
using ServerCert  = std::unique_ptr<rdpCertificate, Releases<freerdp_certificate_free>>;
using Extension   = std::unique_ptr<X509_EXTENSION, Releases<X509_EXTENSION_free>>;
class DirectoryLock {
public:
  explicit DirectoryLock(std::filesystem::path const& directory)
      : descriptor(SystemCall(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC), "certificate directory")) {
    SystemCall(flock(descriptor.Get(), LOCK_EX), "certificate directory lock");
  }

private:
  Descriptor descriptor;
};
template <FreeRDP_Settings_Keys_Pointer KEY, typename VTy, auto RELEASE>
auto Adopt(rdpSettings& settings, std::unique_ptr<VTy, Releases<RELEASE>> owned) -> void {
  Expects(owned != nullptr, "the server credential loaded");
  // These pointer setters transfer ownership despite the generic API's copy documentation.
  if (!freerdp_settings_set_pointer_len(&settings, KEY, owned.get(), 1))
    throw std::runtime_error("FreeRDP refused a server credential.");
  std::ignore = owned.release();
}
auto Hostname() -> std::string {
  std::array<char, 256> name{ };
  if (gethostname(name.data(), name.size() - 1)) throw std::runtime_error("Hostname unavailable.");
  return name.data();
}
auto Stamp(X509& cert) -> bool {
  constexpr long X509Version3 = 2;
  constexpr auto Validity     = std::chrono::seconds(std::chrono::days(3650));
  return X509_set_version(&cert, X509Version3) && ASN1_INTEGER_set(X509_get_serialNumber(&cert), 1)
         && X509_gmtime_adj(X509_getm_notBefore(&cert), 0)
         && X509_gmtime_adj(X509_getm_notAfter(&cert), Validity.count());
}
auto Identify(X509& cert, EVP_PKEY* key, std::string const& host) -> bool {
  auto* const     name      = X509_get_subject_name(&cert);
  auto const      san       = "DNS:" + host;
  Extension const extension(X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, san.c_str()));
  return X509_set_pubkey(&cert, key)
         && X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                       oxbox::utilities::SpanCast<unsigned char const>(std::span(host)).data(), -1, -1,
                                       0)
         && X509_set_issuer_name(&cert, name) && extension && X509_add_ext(&cert, extension.get(), -1);
}
auto SelfSigned(EVP_PKEY* key) -> Certificate {
  Expects(key != nullptr, "RSA key exists");
  Certificate cert(X509_new());
  if (!cert) throw std::runtime_error("Certificate allocation failed.");
  if (!Stamp(*cert) || !Identify(*cert, key, Hostname()) || !X509_sign(cert.get(), key, EVP_sha256()))
    throw std::runtime_error("Certificate signing failed.");
  return cert;
}
auto Generate(Credentials const& paths) -> void {
  Key const key(EVP_RSA_gen(2048));
  if (!key) throw std::runtime_error("RSA key generation failed.");
  auto      cert     = SelfSigned(key.get());
  auto      fd       = open(paths.Key().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  Bio const key_file(fd < 0 ? nullptr : BIO_new_fd(fd, BIO_CLOSE));
  if (key_file)
    std::filesystem::permissions(paths.Key(), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  Bio const cert_file(BIO_new_file(paths.Certificate().c_str(), "w"));
  if (!key_file || !cert_file
      || !PEM_write_bio_PrivateKey(key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr)
      || !PEM_write_bio_X509(cert_file.get(), cert.get()))
    throw std::runtime_error("Credential writing failed.");
}
}
auto DefaultCertificateDirectory() -> std::filesystem::path {
  if (auto* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "sdl-rdp";
  if (auto* home = std::getenv("HOME"); home && *home) return std::filesystem::path(home) / ".local/share/sdl-rdp";
  std::array<char, 16384> buffer { };
  passwd                  entry  { };
  passwd*                 found  = nullptr;
  if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &found) || !found)
    throw std::runtime_error("User home directory unavailable.");
  return std::filesystem::path(entry.pw_dir) / ".local/share/sdl-rdp";
}
auto EnsureCertificate(std::filesystem::path const& directory) -> Credentials {
  Expects(!directory.empty(), "certificate directory is nonempty");
  static std::mutex      generation_guard;
  std::scoped_lock const lock(generation_guard);
  if (!directory.parent_path().empty()) std::filesystem::create_directories(directory.parent_path());
  if (mkdir(directory.c_str(), 0700) && errno != EEXIST)
    throw std::runtime_error("Certificate directory creation failed.");
  DirectoryLock const process_lock(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  Credentials const result{ directory };
  if (!result.Exist()) Generate(result);
  std::filesystem::permissions(result.Key(), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  Ensures(exists(result.Certificate()), "certificate exists");
  Ensures(exists(result.Key()), "private key exists");
  return result;
}
auto InstallServerCredentials(rdpSettings& settings, Credentials const& credentials) -> void {
  ServerKey  key        { freerdp_key_new_from_file(credentials.Key().c_str())                 };
  ServerCert certificate{ freerdp_certificate_new_from_file(credentials.Certificate().c_str()) };
  if (!key) throw std::runtime_error("Server private key failed to load.");
  if (!certificate) throw std::runtime_error("Server certificate failed to load.");
  Adopt<FreeRDP_RdpServerRsaKey>(settings, std::move(key));
  Adopt<FreeRDP_RdpServerCertificate>(settings, std::move(certificate));
}
}
