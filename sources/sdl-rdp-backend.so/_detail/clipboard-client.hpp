#pragma once
#include "client.hpp"

#include <atomic>
#include <freerdp/client/cliprdr.h>
#include <freerdp/event.h>
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
                          ClipboardClient(ClipboardClient const&) = delete;
                          ClipboardClient(ClipboardClient&&)      = delete;
  explicit                ClipboardClient(Client& value, std::vector<BYTE> initial = { });
                          ~ClipboardClient();
  ClipboardClient&        operator = (ClipboardClient const&)     = delete;
  ClipboardClient&        operator = (ClipboardClient&&)          = delete;
  bool                    Received(std::vector<BYTE> const& bytes);
  UINT                    RequestFormat(UINT32 format);
  UINT                    Offer(std::vector<BYTE> bytes, bool unicode = true);
  ClipboardCapture const& Observed() const;

private:
  static void Connected(void* /*unused*/, ChannelConnectedEventArgs const* event);
  static UINT Ready(CliprdrClientContext* context, CLIPRDR_MONITOR_READY const* /*unused*/);
  static UINT Formats(CliprdrClientContext* context, CLIPRDR_FORMAT_LIST const* list);
  static UINT Request(CliprdrClientContext* context, CLIPRDR_FORMAT_DATA_REQUEST const* request);
  static UINT Response(CliprdrClientContext* context, CLIPRDR_FORMAT_DATA_RESPONSE const* response);
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
