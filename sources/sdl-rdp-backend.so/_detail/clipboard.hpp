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
                    ClipboardChannel(ClipboardChannel const&) = delete;
                    ClipboardChannel(ClipboardChannel&&)      = delete;
  ClipboardChannel(PeerLink& link, Activation const& activation, ClipboardStore& store, EventQueue& events,
                   Diagnostics const& diagnostics) noexcept;
                    ~ClipboardChannel();
  ClipboardChannel& operator = (ClipboardChannel const&)      = delete;
  ClipboardChannel& operator = (ClipboardChannel&&)           = delete;
  bool              Open();
  bool              Pump(std::span<HANDLE const> signaled);
  HANDLE            Event() const;

private:
  UINT        Announce();
  UINT        Request();
  UINT        RespondToList();
  bool        FirstOfferWhileAppHoldsText() const;
  UINT        RequestOfferedText(CLIPRDR_FORMAT_LIST const& /*list*/);
  void        Changed(std::string text);
  static UINT Formats(CliprdrServerContext* /*context*/, CLIPRDR_FORMAT_LIST const* /*list*/);
  static UINT DataRequest(CliprdrServerContext* /*context*/, CLIPRDR_FORMAT_DATA_REQUEST const* /*request*/);
  static UINT DataResponse(CliprdrServerContext* /*context*/, CLIPRDR_FORMAT_DATA_RESPONSE const* /*response*/);
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
std::string       ClipboardAnsi(std::string_view text);
std::vector<BYTE> ClipboardUnicode(std::string_view text);
std::string       ClipboardUtf8(std::span<BYTE const> bytes);
} // namespace Backend
