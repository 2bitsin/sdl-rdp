#include <sdl-rdp/freerdp-facade/connection.hpp>

#include <sdl-rdp/freerdp-facade/connection.test.hpp>
#include <sdl-rdp/freerdp-facade/ntlm.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/freerdp-facade/support.test/unjoined-connection.hpp>
#include <sdl-rdp/utilities/posix.hpp>

#include <freerdp/error.h>
#include <freerdp/freerdp.h>
#include <freerdp/input.h>
#include <freerdp/peer.h>
#include <gtest/gtest.h>
#include <winpr/sspi.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <gmock/gmock.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::connection {
namespace {
using sdl_rdp::freerdp_facade::support_test::UnjoinedConnection;
using sdl_rdp::utilities::ConnectedSockets;
using sdl_rdp::utilities::NtOwf;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::SocketPair;
using testing::HasSubstr;

static_assert(std::is_same_v<decltype(std::declval<Connection const&>().Settings()), SettingsReader>);
static_assert(std::is_same_v<decltype(std::declval<Connection&>().Settings()), SettingsView>);

struct Recorded {
  std::optional<NtOwf>         hash;
  bool                         hash_throws   { };
  bool                         refusal_throws{ };
  std::vector<Identity>        claims;
  std::vector<std::string>     refusals;
  std::optional<std::uint32_t> acknowledged;
  std::optional<bool>          allowed;
  std::vector<KeyEvent>        keys;
  std::vector<UnicodeEvent>    characters;
  std::vector<PointerEvent>    pointers;
  std::vector<std::string>     failures;
};
class Recorder final : public ConnectionEvents, public InputSink {
public:
  explicit Recorder(Recorded& recorded) : _recorded{ recorded } { }
  auto     Activate() -> bool override {
    return true;
  }
  auto Capabilities() -> bool override {
    return true;
  }
  auto Logon(bool automatic) -> bool override {
    return automatic;
  }
  auto NtlmHash(Identity const& identity) -> std::optional<NtOwf> override {
    _recorded.claims.push_back(identity);
    if (_recorded.hash_throws) throw std::runtime_error("no account store");
    return _recorded.hash;
  }
  auto NtlmRefused(std::string_view cause) -> void override {
    _recorded.refusals.emplace_back(cause);
    if (_recorded.refusal_throws) throw std::runtime_error("no log");
  }
  auto FrameAcknowledged(std::uint32_t frame) -> void override {
    _recorded.acknowledged = frame;
  }
  auto SuppressOutput(bool allow) -> void override {
    _recorded.allowed = allow;
  }
  auto Key(KeyEvent event) -> void override {
    if (event.code == 0) throw std::runtime_error("no scancode");
    _recorded.keys.push_back(event);
  }
  auto Unicode(UnicodeEvent event) -> void override {
    _recorded.characters.push_back(event);
  }
  auto Pointer(PointerEvent const& event) -> bool override {
    _recorded.pointers.push_back(event);
    return true;
  }
  auto Failed(OperationName operation, std::string_view failure) const -> void override {
    _recorded.failures.push_back(std::string{ operation.View() } + ": " + std::string{ failure });
  }

private:
  Recorded& _recorded;
};
class ConnectionSlots : public testing::Test {
protected:
  Recorded           recorder;
  Recorder           events     { recorder                             };
  UnjoinedConnection unjoined;
  Connection&        connection { unjoined.connection                  };
  Observation        observation{ connection.Observe(events, events)   };
  rdp_context&       context    { ConnectionProbe::Context(connection) };
  rdpInput&          input      { *context.input                       };
  freerdp_peer&      peer       { *context.peer                        };
};
template <std::size_t N> auto Units(std::u16string_view text) -> std::array<std::uint16_t, N> {
  std::array<std::uint16_t, N> units{ };
  std::ranges::copy(text, units.begin());
  return units;
}
auto Hashed(freerdp_peer& peer, SEC_WINNT_AUTH_IDENTITY const& identity, NtOwf& response) -> std::int32_t {
  SecBuffer const                    empty{ };
  std::array<std::uint8_t, 16> const key  { };
  return peer.SspiNtlmHashCallback(&peer, &identity, &empty, key.data(), key.data(), &empty, response.Bytes().data());
}
}
TEST_F(ConnectionSlots, KeyFlagsDecodeOnce) {
  EXPECT_TRUE(input.KeyboardEvent(&input, KBD_FLAGS_EXTENDED | KBD_FLAGS_RELEASE, 0x1c));
  EXPECT_TRUE(input.KeyboardEvent(&input, KBD_FLAGS_DOWN, 0x1e));
  EXPECT_TRUE(input.UnicodeKeyboardEvent(&input, KBD_FLAGS_RELEASE, u'ž'));
  ASSERT_EQ(recorder.keys.size(), 2);
  EXPECT_EQ(recorder.keys[0].code, 0x1c);
  EXPECT_TRUE(recorder.keys[0].extended);
  EXPECT_FALSE(recorder.keys[0].down);
  EXPECT_FALSE(recorder.keys[1].extended);
  EXPECT_TRUE(recorder.keys[1].down);
  ASSERT_EQ(recorder.characters.size(), 1);
  EXPECT_EQ(recorder.characters[0].code, u'ž');
  EXPECT_FALSE(recorder.characters[0].down);
}
TEST_F(ConnectionSlots, PointerButtonsDecode) {
  EXPECT_TRUE(input.MouseEvent(&input, PTR_FLAGS_BUTTON2 | PTR_FLAGS_DOWN | PTR_FLAGS_MOVE, 10, 20));
  EXPECT_TRUE(input.ExtendedMouseEvent(&input, PTR_XFLAGS_BUTTON2 | PTR_XFLAGS_DOWN, 3, 4));
  ASSERT_EQ(recorder.pointers.size(), 2);
  auto const& press = recorder.pointers[0];
  EXPECT_TRUE(press.buttons.test(std::to_underlying(PointerButton::Right)));
  EXPECT_EQ(press.buttons.count(), 1);
  EXPECT_TRUE(press.down);
  EXPECT_TRUE(press.moved);
  EXPECT_EQ(press.x, 10);
  EXPECT_EQ(press.y, 20);
  EXPECT_FALSE(press.wheel);
  auto const& extended = recorder.pointers[1];
  EXPECT_TRUE(extended.buttons.test(std::to_underlying(PointerButton::Forward)));
  EXPECT_TRUE(extended.down);
  EXPECT_FALSE(extended.moved);
}
TEST_F(ConnectionSlots, MiddleAndBackButtonsDecode) {
  EXPECT_TRUE(input.MouseEvent(&input, PTR_FLAGS_BUTTON3 | PTR_FLAGS_DOWN, 1, 2));
  EXPECT_TRUE(input.ExtendedMouseEvent(&input, PTR_XFLAGS_BUTTON1, 3, 4));
  ASSERT_EQ(recorder.pointers.size(), 2);
  EXPECT_TRUE(recorder.pointers[0].buttons.test(std::to_underlying(PointerButton::Middle)));
  EXPECT_EQ(recorder.pointers[0].buttons.count(), 1);
  EXPECT_TRUE(recorder.pointers[1].buttons.test(std::to_underlying(PointerButton::Back)));
  EXPECT_EQ(recorder.pointers[1].buttons.count(), 1);
  EXPECT_FALSE(recorder.pointers[1].down);
}
TEST_F(ConnectionSlots, WheelRotationDecodesToNotches) {
  EXPECT_TRUE(input.MouseEvent(&input, PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 0x188, 0, 0));
  EXPECT_TRUE(input.MouseEvent(&input, PTR_FLAGS_HWHEEL | 0x78, 0, 0));
  ASSERT_EQ(recorder.pointers.size(), 2);
  ASSERT_TRUE(recorder.pointers[0].wheel);
  ASSERT_TRUE(recorder.pointers[1].wheel);
  auto const vertical   = recorder.pointers[0].wheel.value_or(WheelTurn{ });
  auto const horizontal = recorder.pointers[1].wheel.value_or(WheelTurn{ });
  EXPECT_FLOAT_EQ(vertical.vertical, -1.0F);
  EXPECT_FLOAT_EQ(vertical.horizontal, 0.0F);
  EXPECT_FLOAT_EQ(horizontal.horizontal, 1.0F);
}
TEST_F(ConnectionSlots, HandlerThrowIsTheSlotsFailure) {
  EXPECT_FALSE(input.KeyboardEvent(&input, KBD_FLAGS_DOWN, 0));
  ASSERT_EQ(recorder.failures.size(), 1);
  EXPECT_THAT(recorder.failures[0], HasSubstr("Keyboard event"));
  EXPECT_THAT(recorder.failures[0], HasSubstr("no scancode"));
}
TEST_F(ConnectionSlots, PeerAndUpdateSlotsReachTheEvents) {
  EXPECT_TRUE(peer.Logon(&peer, &peer.identity, true));
  EXPECT_FALSE(peer.Logon(&peer, &peer.identity, false));
  EXPECT_TRUE(peer.PostConnect(&peer));
  EXPECT_TRUE(context.update->SurfaceFrameAcknowledge(&context, 42));
  EXPECT_TRUE(context.update->SuppressOutput(&context, 0, nullptr));
  EXPECT_EQ(recorder.acknowledged, 42);
  EXPECT_EQ(recorder.allowed, false);
}
TEST_F(ConnectionSlots, UnicodeIdentityDecodesAndUnknownUserIsDenied) {
  auto                          user     = Units<5>(u"žąsis");
  auto                          domain   = Units<3>(u"LAB");
  SEC_WINNT_AUTH_IDENTITY const identity { .User         = user.data(),
                                           .UserLength   = 5,
                                           .Domain       = domain.data(),
                                           .DomainLength = 3,
                                           .Flags        = SEC_WINNT_AUTH_IDENTITY_UNICODE };
  NtOwf                         response;
  EXPECT_EQ(Hashed(peer, identity, response), SEC_E_LOGON_DENIED);
  ASSERT_EQ(recorder.claims.size(), 1);
  EXPECT_EQ(recorder.claims[0].user, "žąsis");
  EXPECT_EQ(recorder.claims[0].domain, "LAB");
}
TEST_F(ConnectionSlots, UndecodableIdentityIsARefusal) {
  std::array<std::uint16_t, 2>  user    { u'a', 0xd800 };
  SEC_WINNT_AUTH_IDENTITY const identity{ .User       = user.data(),
                                          .UserLength = 2,
                                          .Flags      = SEC_WINNT_AUTH_IDENTITY_UNICODE };
  NtOwf                         response;
  EXPECT_EQ(Hashed(peer, identity, response), SEC_E_LOGON_DENIED);
  EXPECT_TRUE(recorder.claims.empty());
  EXPECT_EQ(recorder.refusals.size(), 1);
  EXPECT_TRUE(recorder.failures.empty());
}
TEST_F(ConnectionSlots, ThrowingRefusalIsTheSlotsFailure) {
  std::array<std::uint16_t, 1>  user    { 0xdc00 };
  SEC_WINNT_AUTH_IDENTITY const identity{ .User       = user.data(),
                                          .UserLength = 1,
                                          .Flags      = SEC_WINNT_AUTH_IDENTITY_UNICODE };
  NtOwf                         response;
  recorder.refusal_throws = true;
  EXPECT_EQ(Hashed(peer, identity, response), SEC_E_INTERNAL_ERROR);
  EXPECT_EQ(recorder.refusals.size(), 1);
  ASSERT_EQ(recorder.failures.size(), 1);
  EXPECT_THAT(recorder.failures[0], HasSubstr("no log"));
}
TEST_F(ConnectionSlots, ThrowingHashIsTheSlotsFailure) {
  auto                          user     = Units<5>(u"alice");
  SEC_WINNT_AUTH_IDENTITY const identity { .User       = user.data(),
                                           .UserLength = 5,
                                           .Flags      = SEC_WINNT_AUTH_IDENTITY_UNICODE };
  NtOwf                         response;
  recorder.hash_throws = true;
  EXPECT_EQ(Hashed(peer, identity, response), SEC_E_INTERNAL_ERROR);
  EXPECT_TRUE(recorder.refusals.empty());
  ASSERT_EQ(recorder.failures.size(), 1);
  EXPECT_THAT(recorder.failures[0], HasSubstr("NTLM hash"));
  EXPECT_THAT(recorder.failures[0], HasSubstr("no account store"));
}
TEST_F(ConnectionSlots, AnsiIdentityAnswersWithTheV2Key) {
  std::array<std::uint8_t, 3>     user    { 'A', 0xc3, 0xa9 };
  std::array<std::uint8_t, 3>     domain  { 'L', 'A', 'B'   };
  SEC_WINNT_AUTH_IDENTITY_A const identity{ .User         = user.data(),
                                            .UserLength   = 3,
                                            .Domain       = domain.data(),
                                            .DomainLength = 3,
                                            .Flags        = SEC_WINNT_AUTH_IDENTITY_ANSI };
  recorder.hash = NtOwfV1(u"secret");
  NtOwf response;
  EXPECT_EQ(Hashed(peer, std::bit_cast<SEC_WINNT_AUTH_IDENTITY>(identity), response), SEC_E_OK);
  ASSERT_EQ(recorder.claims.size(), 1);
  EXPECT_EQ(recorder.claims[0].user, "Aé");
  EXPECT_TRUE(std::ranges::equal(response.Bytes(), NtOwfV2(*recorder.hash, u"Aé", u"LAB").Bytes()));
}
TEST_F(ConnectionSlots, IdentifyIsWhatIsClaimed) {
  connection.Identify({ .user = "žąsis", .domain = "LAB" });
  connection.SetAuthenticated(true);
  auto const claim = std::as_const(connection).Claimed();
  EXPECT_EQ(claim.user, "žąsis");
  EXPECT_EQ(claim.domain, "LAB");
  EXPECT_TRUE(std::as_const(connection).Authenticated());
}
TEST_F(ConnectionSlots, LastErrorMapsToItsCause) {
  EXPECT_EQ(connection.Error().cause, Cause::None);
  freerdp_set_last_error(&context, FREERDP_ERROR_AUTHENTICATION_FAILED);
  EXPECT_EQ(connection.Error().cause, Cause::AuthenticationFailed);
  EXPECT_EQ(connection.Error().name, "ERRCONNECT_AUTHENTICATION_FAILED");
  freerdp_set_last_error(&context, FREERDP_ERROR_SUCCESS);
  freerdp_set_last_error(&context, FREERDP_ERROR_CONNECT_CANCELLED);
  EXPECT_EQ(connection.Error().cause, Cause::Other);
}
TEST(Adopted, SocketIsTheAdoptedDescriptor) {
  SocketPair       sockets   { ConnectedSockets()        };
  int const        adopted   { sockets.server.Get()      };
  Connection const connection{ std::move(sockets.server) };
  EXPECT_EQ(connection.Socket(), adopted);
}
TEST_F(ConnectionSlots, ObservingTwiceIsAContractFailure) {
  EXPECT_DEATH(std::ignore = connection.Observe(events, events), "observed once");
}
TEST_F(ConnectionSlots, EndingTheObservationUnhooksTheOwners) {
  observation.reset();
  EXPECT_EQ(peer.ContextExtra, nullptr);
  EXPECT_EQ(input.param1, nullptr);
}
}
