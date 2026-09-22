#pragma once
#include "rdp-handles.hpp"
#include <freerdp/server/cliprdr.h>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace Backend {
class Peer;
struct Clipboard {
  std::string text, exported;
  std::vector<BYTE> unicode{0, 0};
  uint64_t generation = 0;
};
class ClipboardChannel {
public:
  explicit ClipboardChannel(Peer& peer);
  ~ClipboardChannel();
  bool Open();
  bool Pump();
  HANDLE Event() const;
private:
  UINT Announce();
  UINT Request();
  UINT RespondToList();
  bool FirstOfferWhileAppHoldsText() const;
  UINT RequestOfferedText(CLIPRDR_FORMAT_LIST const&);
  void Changed(std::string text);
  static UINT Formats(CliprdrServerContext*, CLIPRDR_FORMAT_LIST const*);
  static UINT DataRequest(CliprdrServerContext*, CLIPRDR_FORMAT_DATA_REQUEST const*);
  static UINT DataResponse(CliprdrServerContext*, CLIPRDR_FORMAT_DATA_RESPONSE const*);
  Peer& peer;
  std::unique_ptr<CliprdrServerContext, Releases<cliprdr_server_context_free>> context;
  uint64_t announced = 0, requested = 0, offered = 0, requested_generation = 0, offered_generation = 0;
  bool ready = false, opened = false, pending = false, has_unicode = false;
};
std::string ClipboardAnsi(std::string_view text);
std::vector<BYTE> ClipboardUnicode(std::string_view text);
std::string ClipboardUtf8(std::span<BYTE const> bytes);
}
