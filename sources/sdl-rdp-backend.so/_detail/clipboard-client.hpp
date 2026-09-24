#pragma once
#include "client.hpp"

#include <freerdp/client/cliprdr.h>
#include <freerdp/event.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace Headless {
struct ClipboardCapture {
  std::atomic_uint requests  = 0;
  std::atomic_uint responses = 0;
  std::atomic_uint accepted  = 0;
};
class ClipboardClient {
public:
           ClipboardClient(ClipboardClient const&)                                 = delete;
           ClipboardClient(ClipboardClient&&)                                      = delete;
  explicit ClipboardClient(Client& value, std::vector<BYTE> initial = { });
           ~ClipboardClient();
  auto     operator=(ClipboardClient const&)                   -> ClipboardClient& = delete;
  auto     operator=(ClipboardClient&&)                        -> ClipboardClient& = delete;
  auto     Received(std::vector<BYTE> const& bytes)            -> bool;
  auto     RequestFormat(UINT32 format)                        -> UINT;
  auto     Offer(std::vector<BYTE> bytes, bool unicode = true) -> UINT;
  auto     Observed() const                                    -> ClipboardCapture const&;

private:
  static auto Connected(void* /*unused*/, ChannelConnectedEventArgs const* event)                   -> void;
  static auto Ready(CliprdrClientContext* context, CLIPRDR_MONITOR_READY const* /*unused*/)         -> UINT;
  static auto Formats(CliprdrClientContext* context, CLIPRDR_FORMAT_LIST const* list)               -> UINT;
  static auto Request(CliprdrClientContext* context, CLIPRDR_FORMAT_DATA_REQUEST const* request)    -> UINT;
  static auto Response(CliprdrClientContext* context, CLIPRDR_FORMAT_DATA_RESPONSE const* response) -> UINT;
  ClipboardCapture                            observed;
  inline static thread_local ClipboardClient* attaching = nullptr;
  Client&                                     client;
  std::mutex                                  guard;
  std::vector<BYTE>                           outgoing;
  std::vector<BYTE>                           incoming;
  std::vector<UINT32>                         formats;
  std::atomic<CliprdrClientContext*>          channel   = nullptr;
};
}
