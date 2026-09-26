#pragma once
#include <sdl-rdp/clipboard/forward.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/server/cliprdr.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdl_rdp::clipboard::detail::channel {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::Activation;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Releases;

class ClipboardChannel : private Pinned {
public:
       ClipboardChannel(PeerLink& link, Activation const& activation, ClipboardStore& store, EventQueue& events,
                        Diagnostics const& diagnostics) noexcept;
       ~ClipboardChannel();
  auto Open()                          -> bool;
  auto Pump(Signalled const& signaled) -> bool;
  auto Event() const                   -> std::optional<WaitHandle>;

private:
  class Callbacks;
  auto Formats(CLIPRDR_FORMAT_LIST const& list)                   -> std::uint32_t;
  auto FailureSource() const noexcept                             -> Diagnostics const&;
  auto DataRequest(CLIPRDR_FORMAT_DATA_REQUEST const& request)    -> std::uint32_t;
  auto DataResponse(CLIPRDR_FORMAT_DATA_RESPONSE const& response) -> std::uint32_t;
  auto Announce()                                                 -> std::uint32_t;
  auto Request()                                                  -> std::uint32_t;
  auto RespondToList()                                            -> std::uint32_t;
  auto FirstOfferWhileAppHoldsText() const                        -> bool;
  auto RequestOfferedText(CLIPRDR_FORMAT_LIST const& /*list*/)    -> std::uint32_t;
  auto Changed(std::string text)                                  -> void;
  using ClipboardContext = std::unique_ptr<CliprdrServerContext, Releases<cliprdr_server_context_free>>;
  PeerLink&          _link;
  Activation const&  _activation;
  ClipboardStore&    _store;
  EventQueue&        _events;
  Diagnostics const& _diagnostics;
  ClipboardContext   _context;
  std::uint64_t      _announced           { };
  std::uint64_t      _requested           { };
  std::uint64_t      _offered             { };
  std::uint64_t      _requested_generation{ };
  std::uint64_t      _offered_generation  { };
  bool               _ready               { };
  bool               _opened              { };
  bool               _pending             { };
  bool               _has_unicode         { };
};
}

namespace sdl_rdp::clipboard {
using detail::channel::ClipboardChannel;
}
