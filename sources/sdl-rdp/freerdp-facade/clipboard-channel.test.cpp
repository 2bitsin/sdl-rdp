#include <sdl-rdp/freerdp-facade/clipboard-channel.hpp>

#include <sdl-rdp/freerdp-facade/support.test/unjoined-connection.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/support.test/out-of-range-enum.hpp>

#include <freerdp/server/cliprdr.h>
#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <gmock/gmock.h>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::clipboard_channel {
class ClipboardChannelProbe {
public:
  static auto Context(ClipboardChannel const& channel) -> CliprdrServerContext& {
    return channel.Context();
  }
};
namespace {
using sdl_rdp::freerdp_facade::support_test::UnjoinedConnection;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::support_test::OutOfRangeEnum;
using testing::ElementsAre;
using Bytes = std::vector<std::byte>;

struct Recorded {
  bool                              replies  { true };
  std::vector<ClipboardFormat>      formats;
  std::vector<ClipboardFormat>      requests;
  std::vector<std::optional<Bytes>> responses;
};
class Recorder final : public ClipboardChannelEvents {
public:
  explicit Recorder(Recorded& recorded) : _recorded{ recorded } { }
  auto     ClientFormatList(std::span<ClipboardFormat const> formats) -> bool override {
    _recorded.formats.assign(formats.begin(), formats.end());
    return _recorded.replies;
  }
  auto ClientFormatDataRequest(ClipboardFormat format) -> bool override {
    _recorded.requests.push_back(format);
    return _recorded.replies;
  }
  auto ClientFormatDataResponse(FormatData data) -> bool override {
    _recorded.responses.push_back(data.transform([](auto bytes) { return Bytes(bytes.begin(), bytes.end()); }));
    return _recorded.replies;
  }
  auto Failed(OperationName /*operation*/, std::string_view /*failure*/) const -> void override { }

private:
  Recorded& _recorded;
};
struct Wire {
  friend auto operator==(Wire const&, Wire const&) -> bool = default;
  std::uint16_t flags{ };
  Bytes         data;
};
auto Written() -> std::optional<Wire>& {
  static std::optional<Wire> written;
  return written;
}
// abi: psCliprdrServerFormatDataResponse
auto Captured(CliprdrServerContext* /*context*/, CLIPRDR_FORMAT_DATA_RESPONSE const* response) -> std::uint32_t {
  Expects(response != nullptr, "the response is supplied");
  auto const bytes = std::as_bytes(std::span(response->requestedFormatData, response->common.dataLen));
  Written().emplace(response->common.msgFlags, Bytes(bytes.begin(), bytes.end()));
  return CHANNEL_RC_OK;
}
class Unopened : public testing::Test {
protected:
  Recorded           recorded;
  Recorder           events  { recorded                                       };
  UnjoinedConnection unjoined;
  ClipboardChannel   channel { unjoined.channels, unjoined.connection, events };
};
// The open fails while no client has joined cliprdr, but the context and its slots are in place.
class ClipboardSlots : public Unopened {
protected:
  bool                  opened { channel.Open()                          };
  CliprdrServerContext& context{ ClipboardChannelProbe::Context(channel) };
};
auto List(std::span<CLIPRDR_FORMAT> formats) -> CLIPRDR_FORMAT_LIST {
  CLIPRDR_FORMAT_LIST list{ .common = { .msgType = CB_FORMAT_LIST } };
  list.numFormats = Narrowed<std::uint32_t>(formats.size());
  list.formats    = formats.data();
  return list;
}
auto Response(std::uint16_t flags, std::span<std::uint8_t const> data) -> CLIPRDR_FORMAT_DATA_RESPONSE {
  CLIPRDR_FORMAT_DATA_RESPONSE response{ .common = { .msgType = CB_FORMAT_DATA_RESPONSE, .msgFlags = flags } };
  response.common.dataLen      = Narrowed<std::uint32_t>(data.size());
  response.requestedFormatData = data.data();
  return response;
}
}
TEST_F(Unopened, OpeningBeforeTheClientJoinsTheChannelIsRefused) {
  EXPECT_FALSE(channel.Open());
}
TEST_F(Unopened, AChannelOpensOnce) {
  std::ignore = channel.Open();
  EXPECT_DEATH(std::ignore = channel.Open(), "clipboard opens once");
}
TEST_F(Unopened, SendingBeforeOpeningIsAContractFailure) {
  EXPECT_DEATH(std::ignore = channel.MonitorReady(), "the clipboard channel is open");
}
TEST_F(ClipboardSlots, AFormatListReachesTheHandlerInOrder) {
  std::array<CLIPRDR_FORMAT, 3> formats{ { { .formatId = 13 }, { .formatId = 1 }, { .formatId = 0xC0DE } } };
  auto const                    list   { List(formats)                                                     };
  EXPECT_EQ(context.ClientFormatList(&context, &list), CHANNEL_RC_OK);
  EXPECT_THAT(recorded.formats, ElementsAre(ClipboardFormat::UnicodeText, ClipboardFormat::Text,
                                            OutOfRangeEnum<ClipboardFormat>(0xC0DE)));
}
TEST_F(ClipboardSlots, ADataRequestReachesTheHandler) {
  CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  request.requestedFormatId = 13;
  EXPECT_EQ(context.ClientFormatDataRequest(&context, &request), CHANNEL_RC_OK);
  EXPECT_THAT(recorded.requests, ElementsAre(ClipboardFormat::UnicodeText));
}
TEST_F(ClipboardSlots, AFailedDataResponseCarriesNoData) {
  std::array<std::uint8_t, 2> const data    { 'h', 'i'                         };
  auto const                        failed  { Response(CB_RESPONSE_FAIL, data) };
  auto const                        answered{ Response(CB_RESPONSE_OK, data)   };
  EXPECT_EQ(context.ClientFormatDataResponse(&context, &failed), CHANNEL_RC_OK);
  EXPECT_EQ(context.ClientFormatDataResponse(&context, &answered), CHANNEL_RC_OK);
  EXPECT_THAT(recorded.responses, ElementsAre(std::nullopt, Bytes{ std::byte{ 'h' }, std::byte{ 'i' } }));
}
TEST_F(ClipboardSlots, AHandlerWhoseReplyWasRefusedFailsTheSlot) {
  recorded.replies = false;
  std::array<CLIPRDR_FORMAT, 1>     formats { { { .formatId = 1 } }                           };
  std::array<std::uint8_t, 0> const data    { };
  auto const                        list    { List(formats)                                   };
  auto const                        response{ Response(CB_RESPONSE_OK, data)                  };
  CLIPRDR_FORMAT_DATA_REQUEST const request { .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  EXPECT_EQ(context.ClientFormatList(&context, &list), ERROR_INTERNAL_ERROR);
  EXPECT_EQ(context.ClientFormatDataRequest(&context, &request), ERROR_INTERNAL_ERROR);
  EXPECT_EQ(context.ClientFormatDataResponse(&context, &response), ERROR_INTERNAL_ERROR);
}
TEST_F(ClipboardSlots, NoDataIsSentAsAFailedResponse) {
  Written().reset();
  context.ServerFormatDataResponse = Captured;
  EXPECT_TRUE(channel.ServerFormatDataResponse(std::nullopt));
  EXPECT_EQ(Written(), (Wire{ .flags = CB_RESPONSE_FAIL }));
}
TEST_F(ClipboardSlots, DataIsSentWithItsLength) {
  Written().reset();
  context.ServerFormatDataResponse = Captured;
  Bytes const data{ std::byte{ 'o' }, std::byte{ 'k' }, std::byte{ 0 } };
  EXPECT_TRUE(channel.ServerFormatDataResponse(data));
  EXPECT_EQ(Written(), (Wire{ .flags = CB_RESPONSE_OK, .data = data }));
}
}
