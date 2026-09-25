#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/headless-client.test/client/clipboard.hpp>
#include <sdl-rdp/headless-client.test/utilities/octets.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/transcode.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace sdl_rdp::integration::clipboard_test::detail::channel {
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::ClipboardClient;
using sdl_rdp::link::Event;
using sdl_rdp::utilities::InvalidEncoding;
using sdl_rdp::utilities::Unencodable;
namespace {
using sdl_rdp::headless_client_test::backend::Contains;
using sdl_rdp::headless_client_test::utilities::Octets;
using sdl_rdp::headless_client_test::utilities::UnicodeText;
using sdl_rdp::link::ClipboardChanged;
auto HasClipboardEvent(std::span<Event const> events) -> bool {
  return Contains<ClipboardChanged>(events);
}
class Clipboard : public testing::Test {
protected:
  auto Drain() -> void {
    while (!backend.Poll().empty()) {
    }
  }
  auto UntilClipboardEvent() -> bool {
    bool changed{ };
    return client->Until([&] {
      changed = changed || HasClipboardEvent(backend.Poll());
      return changed;
    });
  }
  auto ThenNonTextOffer() -> void {
    ASSERT_TRUE(clipboard->Offer({ }, false));
    ASSERT_TRUE(UntilClipboardEvent());
    EXPECT_FALSE((*backend).HasClipboardText());
    EXPECT_EQ((*backend).ClipboardText(), "");
  }
  auto ThenReplacedText(std::string_view retained) -> void {
    EXPECT_EQ(retained, "hello");
    EXPECT_EQ((*backend).ClipboardText(), "world");
  }
  auto GivenClipboard() -> void {
    client    = std::make_unique<Client>(backend.Port(), false);
    clipboard = std::make_unique<ClipboardClient>(*client);
    ConnectClipboard(*client, *clipboard);
  }
  auto ConnectClipboard(Client& client, ClipboardClient& clipboard) -> void {
    ASSERT_TRUE(client.Connect()) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return clipboard.Observed().accepted.load() == 1; }));
  }
  auto SetUp() -> void override {
    ASSERT_NO_FATAL_FAILURE(backend.Open(LoopbackConfig(certificates.Path()), logs));
  }
  Logs                             logs;
  oxbox::platform::ScratchArea     certificates{ "clipboard", "sdl-rdp" };
  BackendInstance                  backend;
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
  for (auto polled = backend.Poll(); !polled.empty(); polled = backend.Poll()) EXPECT_FALSE(HasClipboardEvent(polled));
  EXPECT_EQ((*backend).ClipboardText(), "");
  RecordProperty("trace", "empty connect: format list accepted; CLIPBOARD events=0");
}
TEST_F(Clipboard, LocalTextAndErrors) {
  EXPECT_FALSE((*backend).HasClipboardText());
  EXPECT_EQ((*backend).ClipboardText(), "");
  (*backend).SetClipboardText("żółw");
  EXPECT_TRUE((*backend).HasClipboardText());
  EXPECT_EQ((*backend).ClipboardText(), "żółw");
  EXPECT_THROW((*backend).SetClipboardText("\xc0\xaf"), InvalidEncoding);
  EXPECT_EQ((*backend).ClipboardText(), "żółw");
  (*backend).SetClipboardText("");
  EXPECT_FALSE((*backend).HasClipboardText());
}
TEST_F(Clipboard, LiveSetAndMalformedResponse) {
  ASSERT_NO_FATAL_FAILURE(GivenClipboard());
  (*backend).SetClipboardText("hello");
  ASSERT_TRUE(client->Until([&] { return clipboard->Received(UnicodeText("hello")); }));
  Drain();
  auto const retained = (*backend).ClipboardText();
  ASSERT_NO_FATAL_FAILURE(OfferMalformedText(*client, *clipboard));
  ASSERT_TRUE(clipboard->Offer(UnicodeText("world")));
  ASSERT_TRUE(UntilClipboardEvent());
  ThenReplacedText(retained);
}
TEST_F(Clipboard, FirstOfferRetainsAppText) {
  (*backend).SetClipboardText("app");
  Client          client(backend.Port(), false);
  ClipboardClient clipboard(client, UnicodeText("client"));
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received(UnicodeText("app")); }));
  EXPECT_EQ(clipboard.Observed().accepted.load(), 1u);
  EXPECT_EQ(clipboard.Observed().requests.load(), 0u);
  EXPECT_EQ((*backend).ClipboardText(), "app");
}
TEST_F(Clipboard, NonTextOfferClearsText) {
  ASSERT_NO_FATAL_FAILURE(GivenClipboard());
  (*backend).SetClipboardText("app");
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
  EXPECT_THROW(TranscodeRange<std::string>(input.last(1), { }, { }), InvalidEncoding);
  EXPECT_THROW(TranscodeRange<std::string>(std::as_bytes(std::span(encoded)).first(3),
                                           { Encoding::UTF16, std::endian::little }, { }),
               InvalidEncoding);
  EXPECT_THROW(TranscodeRange<std::string>(input, { }, { Encoding::UCS1 }), Unencodable);
}
}
}
