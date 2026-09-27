#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::auth::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::RuntimeFailure;
using std::string_view;

using TlsAcceptRefused     = RuntimeFailure<"TlsAcceptRefused"_hash, "TLS rehearsal accept failed.">;
using TlsHandshakeFailed   = RuntimeFailure<"TlsHandshakeFailed"_hash, "TLS rehearsal client handshake failed.">;
using BioMethodSetupFailed = RuntimeFailure<"BioMethodSetupFailed"_hash, "Socket BIO method setup failed.">;

using CertificateDirectoryFailed = RuntimeFailure<"CertificateDirectoryFailed"_hash,
                                                  "Certificate directory {} creation failed.", string_view>;
}

namespace sdl_rdp::auth {
using detail::exceptions::BioMethodSetupFailed;
using detail::exceptions::CertificateDirectoryFailed;
using detail::exceptions::TlsAcceptRefused;
using detail::exceptions::TlsHandshakeFailed;
}
