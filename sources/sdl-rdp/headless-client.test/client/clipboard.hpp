#pragma once
#include "client.hpp"

#include <freerdp/client/cliprdr.h>
#include <freerdp/event.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Headless {
struct ClipboardCapture {
  std::atomic<std::size_t> requests  = 0;
  std::atomic<std::size_t> responses = 0;
  std::atomic<std::size_t> accepted  = 0;
};
class ClipboardClient {
public:
           ClipboardClient(ClipboardClient const&)                                         = delete;
           ClipboardClient(ClipboardClient&&)                                              = delete;
  explicit ClipboardClient(Client& value, std::vector<std::uint8_t> initial = { });
           ~ClipboardClient();
  auto     operator=(ClipboardClient const&)                           -> ClipboardClient& = delete;
  auto     operator=(ClipboardClient&&)                                -> ClipboardClient& = delete;
  auto     Received(std::vector<std::uint8_t> const& bytes)            -> bool;
  auto     RequestFormat(std::uint32_t format)                         -> std::uint32_t;
  auto     Offer(std::vector<std::uint8_t> bytes, bool unicode = true) -> bool;
  auto     Observed() const                                            -> ClipboardCapture const&;

private:
  class Callbacks;
  auto Accepted()                                                                         -> std::uint32_t;
  auto Ready(CliprdrClientContext& context)                                               -> std::uint32_t;
  auto Formats(CliprdrClientContext& context, CLIPRDR_FORMAT_LIST const& list)            -> std::uint32_t;
  auto Request(CliprdrClientContext& context, CLIPRDR_FORMAT_DATA_REQUEST const& request) -> std::uint32_t;
  auto Response(CLIPRDR_FORMAT_DATA_RESPONSE const& response)                             -> std::uint32_t;
  ClipboardCapture                   observed;
  Client&                            client;
  std::mutex                         guard;
  std::vector<std::uint8_t>          outgoing;
  std::vector<std::uint8_t>          incoming;
  std::vector<std::uint32_t>         formats;
  std::atomic<CliprdrClientContext*> channel  = nullptr;
};
}
