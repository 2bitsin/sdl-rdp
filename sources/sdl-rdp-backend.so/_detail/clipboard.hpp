#pragma once
#include "rdp-handles.hpp"

#include <freerdp/server/cliprdr.h>
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
       ClipboardChannel(ClipboardChannel const&)                   = delete;
       ClipboardChannel(ClipboardChannel&&)                        = delete;
       ClipboardChannel(PeerLink& link, Activation const& activation, ClipboardStore& store, EventQueue& events,
                        Diagnostics const& diagnostics) noexcept;
       ~ClipboardChannel();
  auto operator = (ClipboardChannel const&)   -> ClipboardChannel& = delete;
  auto operator = (ClipboardChannel&&)        -> ClipboardChannel& = delete;
  auto Open()                                 -> bool;
  auto Pump(std::span<HANDLE const> signaled) -> bool;
  auto Event() const                          -> HANDLE;

private:
  auto        Announce()                                                                                        -> UINT;
  auto        Request()                                                                                         -> UINT;
  auto        RespondToList()                                                                                   -> UINT;
  auto        FirstOfferWhileAppHoldsText() const                                                               -> bool;
  auto        RequestOfferedText(CLIPRDR_FORMAT_LIST const& /*list*/)                                           -> UINT;
  auto        Changed(std::string text)                                                                         -> void;
  static auto Formats(CliprdrServerContext* /*context*/, CLIPRDR_FORMAT_LIST const* /*list*/)                   -> UINT;
  static auto DataRequest(CliprdrServerContext* /*context*/, CLIPRDR_FORMAT_DATA_REQUEST const* /*request*/)    -> UINT;
  static auto DataResponse(CliprdrServerContext* /*context*/, CLIPRDR_FORMAT_DATA_RESPONSE const* /*response*/) -> UINT;
  using ClipboardContext = std::unique_ptr<CliprdrServerContext, Releases<cliprdr_server_context_free>>;
  PeerLink&          _link;
  Activation const&  _activation;
  ClipboardStore&    _store;
  EventQueue&        _events;
  Diagnostics const& _diagnostics;
  ClipboardContext   _context;
  uint64_t           _announced           { };
  uint64_t           _requested           { };
  uint64_t           _offered             { };
  uint64_t           _requested_generation{ };
  uint64_t           _offered_generation  { };
  bool               _ready               { };
  bool               _opened              { };
  bool               _pending             { };
  bool               _has_unicode         { };
};
auto ClipboardAnsi(std::string_view text)       -> std::string;
auto ClipboardUnicode(std::string_view text)    -> std::vector<BYTE>;
auto ClipboardUtf8(std::span<BYTE const> bytes) -> std::string;
} // namespace Backend
