#include "_detail/headless-clipboard.hpp"
#include "_detail/test-config.hpp"
#include "_detail/test-logs.hpp"
#include "_detail/transcode.hpp"
#include "sdl-rdp-backend.h"

#include <cstring>
#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>

namespace {
class Clipboard : public testing::Test {
protected:
  void ThenNonTextOffer(std::array<sdlrdp_event, 32>& events) {
    ASSERT_EQ(clipboard->Offer({ }, false), CHANNEL_RC_OK);
    bool changed = false;
    ASSERT_TRUE(client->Until([&] {
      auto count = sdlrdp_poll(handle.get(), events.data(), 32);
      changed |= std::ranges::any_of(std::span(events.data(), count),
                                     [](auto const& event) { return event.type == SDLRDP_CLIPBOARD; });
      return changed;
    }));
    EXPECT_EQ(sdlrdp_has_clipboard_text(handle.get()), 0);
    EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "");
  }
  void ThenReplacedText(char const* retained) {
    EXPECT_STREQ(retained, "hello");
    EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "world");
  }
  void GivenClipboard() {
    client    = std::make_unique<Headless::Client>(sdlrdp_port(handle.get()), false);
    clipboard = std::make_unique<Headless::ClipboardClient>(*client);
    ConnectClipboard(*client, *clipboard);
  }
  void ConnectClipboard(Headless::Client& client, Headless::ClipboardClient& clipboard) {
    ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return clipboard.Observed().accepted.load() == 1; }));
  }
  void SetUp() override {
    auto directory = certificates.Path().string();
    auto config    = Headless::LoopbackConfig(directory);
    config.log      = Headless::Logs::Collect;
    config.log_user = &logs;
    sdlrdp_handle* opened = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &opened), 0) << sdlrdp_last_error();
    handle.reset(opened);
  }
  Headless::Logs                                          logs;
  oxbox::platform::ScratchArea                            certificates{ "clipboard", "sdl-rdp" };
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle      { nullptr, sdlrdp_close  };
  std::unique_ptr<Headless::Client>                       client;
  std::unique_ptr<Headless::ClipboardClient>              clipboard;
};
void OfferMalformedText(Headless::Client& client, Headless::ClipboardClient& clipboard) {
  for (auto const& bytes : { std::vector<BYTE>{ 0x7c }, { 0, 0xdc, 0, 0 }, { 'x', 0 } }) {
    auto count = clipboard.Observed().requests.load();
    ASSERT_EQ(clipboard.Offer(bytes), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] { return clipboard.Observed().requests.load() > count; }));
  }
}
TEST_F(Clipboard, EmptyConnectUnchanged) {
  GivenClipboard();
  if (::testing::Test::HasFatalFailure()) return;
  std::array<sdlrdp_event, 32> events{ };
  while (auto count = sdlrdp_poll(handle.get(), events.data(), 32)) {
    EXPECT_FALSE(std::ranges::any_of(std::span(events.data(), count),
                                     [](auto const& event) { return event.type == SDLRDP_CLIPBOARD; }));
  }
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "");
  RecordProperty("trace", "empty connect: format list accepted; CLIPBOARD events=0");
}
TEST_F(Clipboard, LocalTextAndErrors) {
  EXPECT_EQ(sdlrdp_has_clipboard_text(handle.get()), 0);
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "");
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.get(), "żółw"), 0);
  EXPECT_EQ(sdlrdp_has_clipboard_text(handle.get()), 1);
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "żółw");
  EXPECT_EQ(sdlrdp_set_clipboard_text(handle.get(), "\xc0\xaf"), -1);
  EXPECT_STRNE(sdlrdp_last_error(), "");
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "żółw");
  EXPECT_EQ(sdlrdp_set_clipboard_text(handle.get(), nullptr), -1);
  EXPECT_EQ(sdlrdp_set_clipboard_text(nullptr, "hello"), -1);
  EXPECT_EQ(sdlrdp_get_clipboard_text(nullptr), nullptr);
  EXPECT_EQ(sdlrdp_has_clipboard_text(nullptr), -1);
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.get(), ""), 0);
  EXPECT_EQ(sdlrdp_has_clipboard_text(handle.get()), 0);
}
TEST_F(Clipboard, LiveSetAndMalformedResponse) {
  GivenClipboard();
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.get(), "hello"), 0);
  ASSERT_TRUE(client->Until([&] { return clipboard->Received({ 'h', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0, 0, 0 }); }));
  std::array<sdlrdp_event, 32> initial{ };
  while (sdlrdp_poll(handle.get(), initial.data(), 32)) {
  }
  auto const* retained = sdlrdp_get_clipboard_text(handle.get());
  OfferMalformedText(*client, *clipboard);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(clipboard->Offer({ 'w', 0, 'o', 0, 'r', 0, 'l', 0, 'd', 0, 0, 0 }), CHANNEL_RC_OK);
  bool changed = false;
  ASSERT_TRUE(client->Until([&] {
    std::array<sdlrdp_event, 32> events { };
    auto                         count  = sdlrdp_poll(handle.get(), events.data(), 32);
    changed |= std::ranges::any_of(std::span(events.data(), count),
                                   [](auto const& event) { return event.type == SDLRDP_CLIPBOARD; });
    return changed;
  }));
  ThenReplacedText(retained);
}
TEST_F(Clipboard, FirstOfferRetainsAppText) {
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.get(), "app"), 0);
  Headless::Client          client(sdlrdp_port(handle.get()), false);
  Headless::ClipboardClient clipboard(client, { 'c', 0, 'l', 0, 'i', 0, 'e', 0, 'n', 0, 't', 0, 0, 0 });
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({ 'a', 0, 'p', 0, 'p', 0, 0, 0 }); }));
  EXPECT_EQ(clipboard.Observed().accepted.load(), 1u);
  EXPECT_EQ(clipboard.Observed().requests.load(), 0u);
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "app");
}
TEST_F(Clipboard, NonTextOfferClearsText) {
  GivenClipboard();
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.get(), "app"), 0);
  ASSERT_TRUE(client->Until([&] { return clipboard->Received({ 'a', 0, 'p', 0, 'p', 0, 0, 0 }); }));
  std::array<sdlrdp_event, 32> events{ };
  while (sdlrdp_poll(handle.get(), events.data(), 32)) {
  }
  ThenNonTextOffer(events);
}
TEST(ClipboardTranscode, ByteRanges) {
  using namespace oxbox::utilities;
  using Backend::TranscodeRange;
  std::string_view const text    = "Aż😀";
  auto                   input   = std::as_bytes(std::span(text));
  auto                   encoded =
      TranscodeRange<std::vector<BYTE>>(input, { }, { .encoding = Encoding::UTF16, .order = std::endian::little });
  EXPECT_EQ(encoded, (std::vector<BYTE>{ 0x41, 0, 0x7c, 1, 0x3d, 0xd8, 0, 0xde }));
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
