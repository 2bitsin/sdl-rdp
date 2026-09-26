#include <sdl-rdp/auth/certificate.hpp>

#include <sdl-rdp/auth/exceptions.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/posix.hpp>

#include <freerdp/crypto/certificate.h>
#include <freerdp/crypto/privatekey.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <oxbox/utilities/span.hpp>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace sdl_rdp::auth::detail::certificate {
using sdl_rdp::freerdp_facade::Bio;
using sdl_rdp::freerdp_facade::Certificate;
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Releases;
using sdl_rdp::utilities::SystemCall;
namespace {
using Key        = std::unique_ptr<EVP_PKEY, Releases<EVP_PKEY_free>>;
using ServerKey  = std::unique_ptr<rdpPrivateKey, Releases<freerdp_key_free>>;
using ServerCert = std::unique_ptr<rdpCertificate, Releases<freerdp_certificate_free>>;
using Extension  = std::unique_ptr<X509_EXTENSION, Releases<X509_EXTENSION_free>>;
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
  if (!freerdp_settings_set_pointer_len(&settings, KEY, owned.get(), 1)) throw CredentialFailed{ "installation" };
  std::ignore = owned.release();
}
auto Hostname() -> std::string {
  std::array<char, 256> name{ };
  SystemCall(gethostname(name.data(), name.size() - 1), "Hostname");
  return name.data();
}
auto Stamp(X509& cert) -> bool {
  constexpr int  X509Version3 = 2;
  constexpr auto Validity     = std::chrono::seconds(std::chrono::days(3650));
  return X509_set_version(&cert, X509Version3) && ASN1_INTEGER_set(X509_get_serialNumber(&cert), 1)
         && X509_gmtime_adj(X509_getm_notBefore(&cert), 0)
         && X509_gmtime_adj(X509_getm_notAfter(&cert), Validity.count());
}
auto Identify(X509& cert, EVP_PKEY& key, std::string const& host) -> bool {
  auto* const     name      = X509_get_subject_name(&cert);
  auto const      san       = "DNS:" + host;
  Extension const extension(X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, san.c_str()));
  return X509_set_pubkey(&cert, &key)
         && X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                       oxbox::utilities::SpanCast<std::uint8_t const>(std::span(host)).data(), -1, -1,
                                       0)
         && X509_set_issuer_name(&cert, name) && extension && X509_add_ext(&cert, extension.get(), -1);
}
auto SelfSigned(EVP_PKEY& key) -> Certificate {
  Certificate cert(X509_new());
  if (!cert) throw AllocationFailed{ "Certificate" };
  if (!Stamp(*cert) || !Identify(*cert, key, Hostname()) || !X509_sign(cert.get(), &key, EVP_sha256()))
    throw CredentialFailed{ "certificate signing" };
  return cert;
}
auto Generate(Credentials const& paths) -> void {
  Key const key(EVP_RSA_gen(2048));
  if (!key) throw CredentialFailed{ "RSA key generation" };
  auto      cert     = SelfSigned(*key);
  auto      fd       = open(paths.Key().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  Bio const key_file(fd < 0 ? nullptr : BIO_new_fd(fd, BIO_CLOSE));
  if (key_file)
    std::filesystem::permissions(paths.Key(), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  Bio const cert_file(BIO_new_file(paths.Certificate().c_str(), "w"));
  if (!key_file || !cert_file
      || !PEM_write_bio_PrivateKey(key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr)
      || !PEM_write_bio_X509(cert_file.get(), cert.get()))
    throw CredentialFailed{ "writing" };
}
}
auto EnsureCertificate(Credentials const& credentials) -> void {
  auto const&            directory        = credentials.Directory();
  static std::mutex      generation_guard;
  std::scoped_lock const lock(generation_guard);
  if (!directory.parent_path().empty()) std::filesystem::create_directories(directory.parent_path());
  if (mkdir(directory.c_str(), 0700) && errno != EEXIST) throw CertificateDirectoryFailed{ directory.native() };
  DirectoryLock const process_lock(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  if (!credentials.Exist()) Generate(credentials);
  std::filesystem::permissions(credentials.Key(),
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  Ensures(std::filesystem::exists(credentials.Certificate()), "certificate exists");
  Ensures(std::filesystem::exists(credentials.Key()), "private key exists");
}
auto InstallServerCredentials(rdpSettings& settings, Credentials const& credentials) -> void {
  ServerKey  key        { freerdp_key_new_from_file(credentials.Key().c_str())                 };
  ServerCert certificate{ freerdp_certificate_new_from_file(credentials.Certificate().c_str()) };
  if (!key) throw CredentialFailed{ "private key loading" };
  if (!certificate) throw CredentialFailed{ "certificate loading" };
  Adopt<FreeRDP_RdpServerRsaKey>(settings, std::move(key));
  Adopt<FreeRDP_RdpServerCertificate>(settings, std::move(certificate));
}
}
