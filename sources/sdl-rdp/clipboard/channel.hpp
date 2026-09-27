#pragma once
#include <sdl-rdp/clipboard/forward.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/freerdp-facade/clipboard-channel.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/forward.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace sdl_rdp::clipboard::detail::channel {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::freerdp_facade::ClipboardChannelEvents;
using sdl_rdp::freerdp_facade::ClipboardFormat;
using sdl_rdp::freerdp_facade::FormatData;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::Activation;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;

class ClipboardChannel final : public LoggedFailures<ClipboardChannelEvents> {
public:
       ClipboardChannel(PeerLink& link, Activation const& activation, ClipboardStore& store, EventQueue& events,
                        Diagnostics const& diagnostics) noexcept;
  auto Open()                          -> bool;
  auto Pump(Signalled const& signaled) -> bool;
  auto Event() const                   -> std::optional<WaitHandle>;

private:
  auto ClientFormatList(std::span<ClipboardFormat const> formats)   -> bool override;
  auto ClientFormatDataRequest(ClipboardFormat format)              -> bool override;
  auto ClientFormatDataResponse(FormatData data)                    -> bool override;
  auto Announce()                                                   -> bool;
  auto Request()                                                    -> bool;
  auto RespondToList()                                              -> bool;
  auto FirstOfferWhileAppHoldsText() const                          -> bool;
  auto RequestOfferedText(std::span<ClipboardFormat const> formats) -> bool;
  auto Changed(std::string text)                                    -> void;
  Activation const&                _activation;
  ClipboardStore&                  _store;
  EventQueue&                      _events;
  freerdp_facade::ClipboardChannel _channel;
  std::uint64_t                    _announced           { };
  std::uint64_t                    _requested           { };
  std::uint64_t                    _offered             { };
  std::uint64_t                    _requested_generation{ };
  std::uint64_t                    _offered_generation  { };
  bool                             _ready               { };
  bool                             _opened              { };
  bool                             _pending             { };
  bool                             _has_unicode         { };
};
}

namespace sdl_rdp::clipboard {
using detail::channel::ClipboardChannel;
}
