#pragma once
#include <sdl-rdp/freerdp-facade/forward.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <openssl/bio.h>
#include <openssl/x509.h>
#include <memory>
#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::rdp_handles {
using sdl_rdp::utilities::Releases;

// abi: WinPR's HANDLE is void*; each release makes its one WinPR call in rdp-handles.cpp.
auto CloseEvent(void* event) noexcept     -> void;
auto CloseServer(void* server) noexcept   -> void;
auto CloseChannel(void* channel) noexcept -> void;

using OwnedEvent    = std::unique_ptr<void, Releases<CloseEvent>>;
using ServerHandle  = std::unique_ptr<void, Releases<CloseServer>>;
using Bio           = std::unique_ptr<BIO, Releases<BIO_free>>;
using Certificate   = std::unique_ptr<X509, Releases<X509_free>>;
using ChannelHandle = std::unique_ptr<void, Releases<CloseChannel>>;
// A manual-reset WinPR event, created unsignalled; a WaitHandle lends it to a wait.
class EventHandle {
public:
  explicit EventHandle(OwnedEvent event) noexcept;
  auto     Set() const   -> void;
  auto     Reset() const -> void;

private:
  friend class wait_handle::WaitHandle;
  OwnedEvent _event;
};
auto ManualResetEvent(std::string_view subject) -> EventHandle;
}

namespace sdl_rdp::freerdp_facade {
using detail::rdp_handles::Bio;
using detail::rdp_handles::Certificate;
using detail::rdp_handles::EventHandle;
using detail::rdp_handles::ManualResetEvent;
}
