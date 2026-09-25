#pragma once
#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/peer.h>
#include <openssl/bio.h>
#include <winpr/handle.h>
#include <winpr/wtsapi.h>
#include <memory>

namespace sdl_rdp::freerdp_facade::detail::rdp_handles {
using sdl_rdp::utilities::Releases;

// A waitable the peer loop hands to WaitForMultipleObjects; its owner closes it.
using WaitHandle     = HANDLE;
using EventHandle    = std::unique_ptr<void, Releases<CloseHandle>>;
using Bio            = std::unique_ptr<BIO, Releases<BIO_free>>;
using VirtualChannel = std::unique_ptr<void, Releases<WTSVirtualChannelClose>>;
using PeerHandle     = std::unique_ptr<freerdp_peer, Releases<freerdp_peer_context_free, freerdp_peer_free>>;
}

namespace sdl_rdp::freerdp_facade {
using detail::rdp_handles::Bio;
using detail::rdp_handles::EventHandle;
using detail::rdp_handles::PeerHandle;
using detail::rdp_handles::VirtualChannel;
using detail::rdp_handles::WaitHandle;
}
