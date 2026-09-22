#include "_detail/state.hpp"
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdexcept>

namespace Backend {
namespace {
using Key = std::unique_ptr<EVP_PKEY, Releases<EVP_PKEY_free>>;
using Certificate = std::unique_ptr<X509, Releases<X509_free>>;
using Bio = std::unique_ptr<BIO, Releases<BIO_free>>;
Certificate SelfSigned(EVP_PKEY* key)
{
  Expects(key != nullptr, "RSA key exists");
  Certificate cert(X509_new());
  if (!cert) throw std::runtime_error("Certificate allocation failed.");
  auto name = X509_get_subject_name(cert.get());
  if (!X509_set_version(cert.get(), 2)
      || !ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1)
      || !X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0)
      || !X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3650L * 86400)
      || !X509_set_pubkey(cert.get(), key)
      || !X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
          reinterpret_cast<unsigned char const*>("sdl-rdp"), -1, -1, 0)
      || !X509_set_issuer_name(cert.get(), name)
      || !X509_sign(cert.get(), key, EVP_sha256()))
    throw std::runtime_error("Certificate signing failed.");
  return cert;
}
void Generate(Credentials const& paths)
{
  Expects(!paths.key.empty() && !paths.certificate.empty(), "credential paths exist");
  Key key(EVP_RSA_gen(2048));
  if (!key) throw std::runtime_error("RSA key generation failed.");
  auto cert = SelfSigned(key.get());
  Bio key_file(BIO_new_file(paths.key.c_str(), "w"));
  if (key_file) std::filesystem::permissions(paths.key,
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  Bio cert_file(BIO_new_file(paths.certificate.c_str(), "w"));
  if (!key_file || !cert_file
      || !PEM_write_bio_PrivateKey(key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr)
      || !PEM_write_bio_X509(cert_file.get(), cert.get()))
    throw std::runtime_error("Credential writing failed.");
}
}
Credentials EnsureCertificate(std::filesystem::path const& directory)
{
  Expects(!directory.empty(), "certificate directory is nonempty");
  static std::mutex generation_guard;
  std::scoped_lock lock(generation_guard);
  std::filesystem::create_directories(directory);
  Credentials result{directory / "server.crt", directory / "server.key"};
  if (!exists(result.certificate) || !exists(result.key)) Generate(result);
  Ensures(exists(result.certificate) && exists(result.key), "credentials exist");
  return result;
}
}
