#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <freerdp/server/cliprdr.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Backend {
class Activation;
class ClipboardStore;
class Diagnostics;
class EventQueue;
class PeerLink;
class ClipboardChannel {
public:
       ClipboardChannel(ClipboardChannel const&)                       = delete;
       ClipboardChannel(ClipboardChannel&&)                            = delete;
       ClipboardChannel(PeerLink& link, Activation const& activation, ClipboardStore& store, EventQueue& events,
                        Diagnostics const& diagnostics) noexcept;
       ~ClipboardChannel();
  auto operator=(ClipboardChannel const&)         -> ClipboardChannel& = delete;
  auto operator=(ClipboardChannel&&)              -> ClipboardChannel& = delete;
  auto Open()                                     -> bool;
  auto Pump(std::span<WaitHandle const> signaled) -> bool;
  auto Event() const                              -> WaitHandle;

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
auto ClipboardAnsi(std::string_view text)            -> std::string;
auto ClipboardUnicode(std::string_view text)         -> std::vector<std::byte>;
auto ClipboardUtf8(std::span<std::byte const> bytes) -> std::string;
} // namespace Backend
