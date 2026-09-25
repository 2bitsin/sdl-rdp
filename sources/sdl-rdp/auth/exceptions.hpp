#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace Backend::detail::exceptions {
using std::string_view;

using TlsAcceptRefused     = RuntimeFailure<"TlsAcceptRefused"_hash, "TLS rehearsal accept failed.">;
using TlsHandshakeFailed   = RuntimeFailure<"TlsHandshakeFailed"_hash, "TLS rehearsal client handshake failed.">;
using BioMethodSetupFailed = RuntimeFailure<"BioMethodSetupFailed"_hash, "Socket BIO method setup failed.">;
using CredentialFailed     = RuntimeFailure<"CredentialFailed"_hash, "Server credential {} failed.", string_view>;

using CertificateDirectoryFailed = RuntimeFailure<"CertificateDirectoryFailed"_hash,
                                                  "Certificate directory {} creation failed.", string_view>;
}
namespace Backend {
using detail::exceptions::BioMethodSetupFailed;
using detail::exceptions::CertificateDirectoryFailed;
using detail::exceptions::CredentialFailed;
using detail::exceptions::TlsAcceptRefused;
using detail::exceptions::TlsHandshakeFailed;
}
