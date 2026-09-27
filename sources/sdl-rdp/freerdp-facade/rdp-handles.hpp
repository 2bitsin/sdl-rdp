#pragma once
#include <sdl-rdp/utilities/releases.hpp>

#include <openssl/bio.h>
#include <openssl/x509.h>
#include <winpr/handle.h>
#include <winpr/wtsapi.h>
#include <memory>
#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::rdp_handles {
using sdl_rdp::utilities::Releases;

using EventHandle   = std::unique_ptr<void, Releases<CloseHandle>>;
using ServerHandle  = std::unique_ptr<void, Releases<WTSCloseServer>>;
using Bio           = std::unique_ptr<BIO, Releases<BIO_free>>;
using Certificate   = std::unique_ptr<X509, Releases<X509_free>>;
using ChannelHandle = std::unique_ptr<void, Releases<WTSVirtualChannelClose>>;
auto ManualResetEvent(std::string_view subject) -> EventHandle;
}

namespace sdl_rdp::freerdp_facade {
using detail::rdp_handles::Bio;
using detail::rdp_handles::Certificate;
using detail::rdp_handles::EventHandle;
using detail::rdp_handles::ManualResetEvent;
}
