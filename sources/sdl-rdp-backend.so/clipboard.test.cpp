#include "sdl-rdp-backend.h"
#include "_detail/headless-clipboard.hpp"
#include "_detail/transcode.hpp"
#include "_detail/test-logs.hpp"
#include <oxbox/platform/scratch-area.hpp>
#include <gtest/gtest.h>
#include <cstring>

namespace {
class Clipboard : public testing::Test {
protected:
  void SetUp() override
  {
    auto          directory = certificates.Path().string();
    sdlrdp_config config   { };
    config.log            = Headless::Logs::Collect;
    config.log_user       = &logs;
    config.bind           = "127.0.0.1";
    config.cert_dir       = directory.c_str();
    config.width          = 320;
    config.height         = 200;
    sdlrdp_handle* opened = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &opened), 0) << sdlrdp_last_error();
    handle.reset(opened);
  }
  Headless::Logs               logs;
  oxbox::platform::ScratchArea certificates{ "clipboard", "sdl-rdp" };
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> handle{ nullptr, sdlrdp_close };
};
TEST_F(Clipboard, EmptyConnectUnchanged)
{
  Headless::Client          client(sdlrdp_port(handle.get()), false);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return clipboard.accepted.load() == 1; }));
  sdlrdp_event events[32];
  while (auto count = sdlrdp_poll(handle.get(), events, 32)) {
    EXPECT_FALSE(std::ranges::any_of(std::span(events, count),
                                     [](auto const& event) { return event.type == SDLRDP_CLIPBOARD; }));
  }
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "");
  RecordProperty("trace", "empty connect: format list accepted; CLIPBOARD events=0");
}
TEST_F(Clipboard, LocalTextAndErrors)
{
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
TEST_F(Clipboard, LiveSetAndMalformedResponse)
{
  Headless::Client          client(sdlrdp_port(handle.get()), false);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return clipboard.accepted.load() == 1; }));
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.get(), "hello"), 0);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({ 'h', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0, 0, 0 }); }));
  sdlrdp_event initial[32];
  while (sdlrdp_poll(handle.get(), initial, 32)) {}
  const auto* retained = sdlrdp_get_clipboard_text(handle.get());
  for (const auto& bytes : {
           std::vector<BYTE>{ 0x7c },
           { 0, 0xdc, 0, 0 },
           { 'x', 0 }
  }) {
    auto count = clipboard.requests.load();
    ASSERT_EQ(clipboard.Offer(bytes), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] { return clipboard.requests.load() > count; }));
  }
  ASSERT_EQ(clipboard.Offer({ 'w', 0, 'o', 0, 'r', 0, 'l', 0, 'd', 0, 0, 0 }), CHANNEL_RC_OK);
  bool changed = false;
  ASSERT_TRUE(client.Until([&] {
    sdlrdp_event events[32];
    auto count = sdlrdp_poll(handle.get(), events, 32);
    changed |= std::ranges::any_of(std::span(events, count), [](auto const& event) { return event.type == SDLRDP_CLIPBOARD; });
    return changed;
  }));
  EXPECT_STREQ(retained, "hello");
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "world");
}
TEST_F(Clipboard, FirstOfferRetainsAppText)
{
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.get(), "app"), 0);
  Headless::Client          client(sdlrdp_port(handle.get()), false);
  Headless::ClipboardClient clipboard(client, { 'c', 0, 'l', 0, 'i', 0, 'e', 0, 'n', 0, 't', 0, 0, 0 });
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({ 'a', 0, 'p', 0, 'p', 0, 0, 0 }); }));
  EXPECT_EQ(clipboard.accepted.load(), 1u);
  EXPECT_EQ(clipboard.requests.load(), 0u);
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "app");
}
TEST_F(Clipboard, NonTextOfferClearsText)
{
  Headless::Client          client(sdlrdp_port(handle.get()), false);
  Headless::ClipboardClient clipboard(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return clipboard.accepted.load() == 1; }));
  ASSERT_EQ(sdlrdp_set_clipboard_text(handle.get(), "app"), 0);
  ASSERT_TRUE(client.Until([&] { return clipboard.Received({ 'a', 0, 'p', 0, 'p', 0, 0, 0 }); }));
  sdlrdp_event events[32];
  while (sdlrdp_poll(handle.get(), events, 32)) {}
  ASSERT_EQ(clipboard.Offer({}, false), CHANNEL_RC_OK);
  bool changed = false;
  ASSERT_TRUE(client.Until([&] {
    auto count = sdlrdp_poll(handle.get(), events, 32);
    changed |= std::ranges::any_of(std::span(events, count),
                                   [](auto const& event) { return event.type == SDLRDP_CLIPBOARD; });
    return changed;
  }));
  EXPECT_EQ(sdlrdp_has_clipboard_text(handle.get()), 0);
  EXPECT_STREQ(sdlrdp_get_clipboard_text(handle.get()), "");
}
TEST(ClipboardTranscode, ByteRanges)
{
  using namespace oxbox::utilities;
  using Backend::TranscodeRange;
  std::string_view const text    = "Aż😀";
  auto                   input   = std::as_bytes(std::span(text));
  auto                   encoded = TranscodeRange<std::vector<BYTE>>(input, {}, { .encoding = Encoding::UTF16, .order = std::endian::little });
  EXPECT_EQ(encoded, (std::vector<BYTE>{ 0x41, 0, 0x7c, 1, 0x3d, 0xd8, 0, 0xde }));
  EXPECT_EQ(TranscodeRange<std::string>(std::as_bytes(std::span(encoded)),
                                        { Encoding::UTF16, std::endian::little }, {}),
            text);
  EXPECT_EQ(TranscodeRange<std::string>(input, {}, { Encoding::UCS1 },
                                        [](char32_t point) { return point < 128 ? point : U'?'; }),
            "A??");
  EXPECT_TRUE(TranscodeRange<std::string>({}, {}, {}).empty());
  EXPECT_THROW(TranscodeRange<std::string>(input.last(1), {}, {}), std::runtime_error);
  EXPECT_THROW(TranscodeRange<std::string>(std::as_bytes(std::span(encoded)).first(3),
                                           { Encoding::UTF16, std::endian::little }, {}),
               std::runtime_error);
  EXPECT_THROW(TranscodeRange<std::string>(input, {}, { Encoding::UCS1 }), std::runtime_error);
}
}
