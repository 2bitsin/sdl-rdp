#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/headless-client.test/client/clipboard.hpp>
#include <sdl-rdp/headless-client.test/utilities/octets.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace sdl_rdp::integration::clipboard_test::detail::channel {
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::ClipboardClient;
using sdl_rdp::utilities::Narrowed;
namespace {
using sdl_rdp::headless_client_test::utilities::Octets;
using sdl_rdp::headless_client_test::utilities::UnicodeText;
auto HasClipboardEvent(std::span<sdlrdp_event const> events) -> bool {
  return std::ranges::contains(events, SDLRDP_CLIPBOARD, &sdlrdp_event::type);
}
class Clipboard : public testing::Test {
protected:
  auto Poll(std::span<sdlrdp_event> events) -> std::span<sdlrdp_event const> {
    return events.first(sdlrdp_poll(handle.Handle(), events.data(), Narrowed<std::uint32_t>(events.size())));
  }
  auto Drain() -> void {
    std::array<sdlrdp_event, 32> events{ };
    while (!Poll(events).empty()) {
    }
  }
  auto UntilClipboardEvent() -> bool {
    std::array<sdlrdp_event, 32> events { };
    bool                         changed{ };
    return client->Until([&] {
      changed = changed || HasClipboardEvent(Poll(events));
      return changed;
    });
  }
  auto ThenNonTextOffer() -> void {
    ASSERT_TRUE(clipboard->Offer({ }, false));
    ASSERT_TRUE(UntilClipboardEvent());
    EXPECT_EQ(sdlrdp_has_clipboard_text(handle.Handle()), 0);
    EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.Handle()), "");
  }
  auto ThenReplacedText(char const* retained) -> void {
    EXPECT_STREQ(retained, "hello");
    EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.Handle()), "world");
  }
  auto GivenClipboard() -> void {
    client    = std::make_unique<Client>(sdlrdp_port(handle.Handle()), false);
    clipboard = std::make_unique<ClipboardClient>(*client);
    ConnectClipboard(*client, *clipboard);
  }
  auto ConnectClipboard(Client& client, ClipboardClient& clipboard) -> void {
    ASSERT_TRUE(client.Connect()) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return clipboard.Observed().accepted.load() == 1; }));
  }
  auto SetUp() -> void override {
    auto directory = certificates.Path().string();
    auto config    = LoopbackConfig(directory);
    config.log      = Logs::Collect;
    config.log_user = &logs;
    ASSERT_NO_FATAL_FAILURE(handle.Open(config));
  }
  Logs                             logs;
  oxbox::platform::ScratchArea     certificates{ "clipboard", "sdl-rdp" };
  BackendInstance                  handle;
  std::unique_ptr<Client>          client;
  std::unique_ptr<ClipboardClient> clipboard;
};
auto OfferMalformedText(Client& client, ClipboardClient& clipboard) -> void {
  for (auto const& bytes : { Octets(0x7c), Octets(0, 0xdc, 0, 0), Octets('x', 0) }) {
    auto count = clipboard.Observed().requests.load();
    ASSERT_TRUE(clipboard.Offer(bytes));
    ASSERT_TRUE(client.Until([&] { return clipboard.Observed().requests.load() > count; }));
  }
}
TEST_F(Clipboard, EmptyConnectUnchanged) {
  ASSERT_NO_FATAL_FAILURE(GivenClipboard());
  std::array<sdlrdp_event, 32> events{ };
  for (auto polled = Poll(events); !polled.empty(); polled = Poll(events)) EXPECT_FALSE(HasClipboardEvent(polled));
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.Handle()), "");
  RecordProperty("trace", "empty connect: format list accepted; CLIPBOARD events=0");
}
TEST_F(Clipboard, LocalTextAndErrors) {
  EXPECT_EQ(sdlrdp_has_clipboard_text(handle.Handle()), 0);
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.Handle()), "");
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.Handle(), "żółw"), 0);
  EXPECT_EQ(sdlrdp_has_clipboard_text(handle.Handle()), 1);
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.Handle()), "żółw");
  EXPECT_EQ(sdlrdp_set_clipboard_text(handle.Handle(), "\xc0\xaf"), -1);
  EXPECT_STRNE(sdlrdp_last_error(), "");
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.Handle()), "żółw");
  EXPECT_EQ(sdlrdp_set_clipboard_text(handle.Handle(), nullptr), -1);
  EXPECT_EQ(sdlrdp_set_clipboard_text(nullptr, "hello"), -1);
  EXPECT_EQ(sdlrdp_get_clipboard_text(nullptr), nullptr);
  EXPECT_EQ(sdlrdp_has_clipboard_text(nullptr), -1);
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.Handle(), ""), 0);
  EXPECT_EQ(sdlrdp_has_clipboard_text(handle.Handle()), 0);
}
TEST_F(Clipboard, LiveSetAndMalformedResponse) {
  ASSERT_NO_FATAL_FAILURE(GivenClipboard());
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.Handle(), "hello"), 0);
  ASSERT_TRUE(client->Until([&] { return clipboard->Received(UnicodeText("hello")); }));
  Drain();
  auto const* retained = sdlrdp_get_clipboard_text(handle.Handle());
  ASSERT_NO_FATAL_FAILURE(OfferMalformedText(*client, *clipboard));
  ASSERT_TRUE(clipboard->Offer(UnicodeText("world")));
  ASSERT_TRUE(UntilClipboardEvent());
  ThenReplacedText(retained);
}
TEST_F(Clipboard, FirstOfferRetainsAppText) {
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.Handle(), "app"), 0);
  Client          client(sdlrdp_port(handle.Handle()), false);
  ClipboardClient clipboard(client, UnicodeText("client"));
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received(UnicodeText("app")); }));
  EXPECT_EQ(clipboard.Observed().accepted.load(), 1u);
  EXPECT_EQ(clipboard.Observed().requests.load(), 0u);
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.Handle()), "app");
}
TEST_F(Clipboard, NonTextOfferClearsText) {
  ASSERT_NO_FATAL_FAILURE(GivenClipboard());
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.Handle(), "app"), 0);
  ASSERT_TRUE(client->Until([&] { return clipboard->Received(UnicodeText("app")); }));
  Drain();
  ThenNonTextOffer();
}
TEST(ClipboardTranscode, ByteRanges) {
  using oxbox::utilities::Encoding;
  using sdl_rdp::utilities::TranscodeRange;
  std::string_view const text    = "Aż😀";
  auto                   input   = std::as_bytes(std::span(text));
  auto                   encoded = TranscodeRange<std::vector<std::uint8_t>>(
      input, { }, { .encoding = Encoding::UTF16, .order = std::endian::little });
  EXPECT_EQ(encoded, (std::vector<std::uint8_t>{ 0x41, 0, 0x7c, 1, 0x3d, 0xd8, 0, 0xde }));
  EXPECT_EQ(
      TranscodeRange<std::string>(std::as_bytes(std::span(encoded)), { Encoding::UTF16, std::endian::little }, { }),
      text);
  EXPECT_EQ(TranscodeRange<std::string>(input, { }, { Encoding::UCS1 },
                                        [](char32_t point) { return point < 128 ? point : U'?'; }),
            "A??");
  EXPECT_TRUE(TranscodeRange<std::string>({ }, { }, { }).empty());
  EXPECT_THROW(TranscodeRange<std::string>(input.last(1), { }, { }), std::runtime_error);
  EXPECT_THROW(TranscodeRange<std::string>(std::as_bytes(std::span(encoded)).first(3),
                                           { Encoding::UTF16, std::endian::little }, { }),
               std::runtime_error);
  EXPECT_THROW(TranscodeRange<std::string>(input, { }, { Encoding::UCS1 }), std::runtime_error);
}
}
}
