#pragma once
#include "channels.hpp"
#include "client.hpp"
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/headless-client.test/utilities/published.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/client/cliprdr.h>
#include <freerdp/event.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace sdl_rdp::headless_client_test::client::detail::clipboard {
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::headless_client_test::utilities::Published;
using sdl_rdp::utilities::Pinned;
struct ClipboardCapture {
  std::atomic<std::size_t> requests  = 0;
  std::atomic<std::size_t> responses = 0;
  std::atomic<std::size_t> accepted  = 0;
};
class ClipboardClient : private Pinned {
public:
  explicit ClipboardClient(Client& value, std::vector<std::byte> initial = { });
           ~ClipboardClient();
  auto     Received(std::span<std::byte const> bytes)                   -> bool;
  auto     RequestFormat(std::uint32_t format)                          -> std::uint32_t;
  auto     Offer(std::span<std::byte const> bytes, bool unicode = true) -> bool;
  auto     Observed() const                                             -> ClipboardCapture const&;

private:
  class Callbacks;
  auto Attach(CliprdrClientContext& context)                                              -> void;
  auto Accepted()                                                                         -> std::uint32_t;
  auto Ready(CliprdrClientContext& context)                                               -> std::uint32_t;
  auto Formats(CliprdrClientContext& context, CLIPRDR_FORMAT_LIST const& list)            -> std::uint32_t;
  auto Request(CliprdrClientContext& context, CLIPRDR_FORMAT_DATA_REQUEST const& request) -> std::uint32_t;
  auto Response(CLIPRDR_FORMAT_DATA_RESPONSE const& response)                             -> std::uint32_t;
  ClipboardCapture                                                        observed;
  Client&                                                                 client;
  std::mutex                                                              guard;
  std::vector<std::byte>                                                  outgoing;
  std::vector<std::byte>                                                  incoming;
  std::vector<std::uint32_t>                                              formats;
  Published<CliprdrClientContext>                                         channel;
  Membership<ClipboardClient>                                             membership;
  ChannelSubscription<CLIPRDR_SVC_CHANNEL_NAME, &ClipboardClient::Attach> connections;
};
}

namespace sdl_rdp::headless_client_test::client {
using detail::clipboard::ClipboardClient;
}
