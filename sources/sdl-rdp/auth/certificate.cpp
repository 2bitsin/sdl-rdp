#include <sdl-rdp/auth/certificate.hpp>

#include <sdl-rdp/auth/private-directory.hpp>
#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/socket.hpp>

#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <oxbox/utilities/path.hpp>
#include <oxbox/utilities/span.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace sdl_rdp::auth::detail::certificate {
using oxbox::utilities::PathToString;
using sdl_rdp::freerdp_facade::Bio;
using sdl_rdp::freerdp_facade::Certificate;
using sdl_rdp::freerdp_facade::CredentialFailed;
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::HostName;
using sdl_rdp::utilities::Releases;
namespace {
using Key       = std::unique_ptr<EVP_PKEY, Releases<EVP_PKEY_free>>;
using Extension = std::unique_ptr<X509_EXTENSION, Releases<X509_EXTENSION_free>>;
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
  if (!Stamp(*cert) || !Identify(*cert, key, HostName()) || !X509_sign(cert.get(), &key, EVP_sha256()))
    throw CredentialFailed{ "certificate signing" };
  return cert;
}
auto Generate(Credentials const& paths, PrivateDirectory const& directory) -> void {
  Key const key(EVP_RSA_gen(2048));
  if (!key) throw CredentialFailed{ "RSA key generation" };
  auto      cert      = SelfSigned(*key);
  Bio const key_file  = directory.NewFile(paths.Key().filename());
  Bio const cert_file(BIO_new_file(PathToString(paths.Certificate()).c_str(), "wb"));
  if (!key_file || !cert_file
      || !PEM_write_bio_PrivateKey(key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr)
      || !PEM_write_bio_X509(cert_file.get(), cert.get()))
    throw CredentialFailed{ "writing" };
}
}
auto EnsureCertificate(Credentials const& credentials) -> void {
  static std::mutex      generation_guard;
  std::scoped_lock const lock(generation_guard);
  PrivateDirectory const directory(credentials.Directory());
  if (!credentials.Exist()) Generate(credentials, directory);
  directory.Secure(credentials.Key().filename());
  Ensures(std::filesystem::exists(credentials.Certificate()), "certificate exists");
  Ensures(std::filesystem::exists(credentials.Key()), "private key exists");
}
}
