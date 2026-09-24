#pragma once
#include "rdp-handles.hpp"

#include <freerdp/server/cliprdr.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Backend {
class Peer;
struct Clipboard {
  std::string       text;
  std::string       exported;
  std::vector<BYTE> unicode    { 0, 0 };
  uint64_t          generation = 0;
};
class ClipboardChannel {
public:
                    ClipboardChannel(ClipboardChannel const&) = delete;
                    ClipboardChannel(ClipboardChannel&&)      = delete;
  explicit          ClipboardChannel(Peer& value);
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
  Peer&                                                                        peer;
  std::unique_ptr<CliprdrServerContext, Releases<cliprdr_server_context_free>> context;
  uint64_t                                                                     announced            = 0;
  uint64_t                                                                     requested            = 0;
  uint64_t                                                                     offered              = 0;
  uint64_t                                                                     requested_generation = 0;
  uint64_t                                                                     offered_generation   = 0;
  bool                                                                         ready                = false;
  bool                                                                         opened               = false;
  bool                                                                         pending              = false;
  bool                                                                         has_unicode          = false;
};
std::string       ClipboardAnsi(std::string_view text);
std::vector<BYTE> ClipboardUnicode(std::string_view text);
std::string       ClipboardUtf8(std::span<BYTE const> bytes);
} // namespace Backend
