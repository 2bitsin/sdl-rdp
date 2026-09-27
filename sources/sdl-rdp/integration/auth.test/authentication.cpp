#include <sdl-rdp/headless-client.test/backend/authentication.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/freerdp-facade/connection.test.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/session/backend.hpp>

#include <freerdp/peer.h>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <ranges>

namespace sdl_rdp::integration::auth_test::detail::authentication {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::ConnectionProbe;
using sdl_rdp::headless_client_test::backend::Authentication;
using sdl_rdp::headless_client_test::backend::CurrentStatus;
using sdl_rdp::headless_client_test::backend::FixedPair;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::SettingsOf;
using sdl_rdp::session::Backend;
using sdl_rdp::utilities::Required;

namespace {
auto RejectCertificate(std::uint32_t port, bool& rejected) -> void {
  static thread_local bool verified;
  verified = false;
  Client client(port, false);
  client.Credentials({ .user = "alice", .password = "correct-secret", .domain = "LAB" }, true);
  auto const settings = SettingsOf(client);
  settings.Set(BoolKey::TlsSecurity, true);
  settings.Set(BoolKey::IgnoreCertificate, false);
  client.Instance()->VerifyCertificateEx = [](freerdp*, char const*, std::uint16_t, char const*, char const*,
                                              char const*, char const*, std::uint32_t) -> std::uint32_t {
    verified = true;
    return 0;
  };
  EXPECT_FALSE(client.Connect());
  rejected = verified;
}
}
TEST_F(Authentication, NoneReportsIdentity) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::None, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("žąsis", "irrelevant-secret", "LAB", false, true));
  RecordProperty("trace", "none: user=žąsis domain=LAB authenticated=0");
}
TEST_F(Authentication, TlsFixedPair) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", false, true));
  RecordProperty("trace", "tls fixed pair: user=alice domain=LAB authenticated=1");
}
TEST_F(Authentication, DnsDomain) {
  std::string const domain = std::string(63, 'a') + "." + std::string(63, 'b') + ".example.org";
  setup.pair = FixedPair{ .user = "alice", .password = "correct-secret", .domain = domain };
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls, false));
  Attempt("alice", "correct-secret", domain, false, true);
}
TEST_F(Authentication, WrongPassword) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "wrong-secret", "LAB", false, false));
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, WrongUser) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls));
  ASSERT_NO_FATAL_FAILURE(Attempt("mallory", "correct-secret", "LAB", false, false));
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, WrongDomain) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "OTHER", false, false));
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, UnsetDomain) {
  setup.pair = FixedPair{ .user = "alice", .password = "correct-secret", .domain = std::nullopt };
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "OTHER", false, true));
  RecordProperty("trace", "tls fixed pair: unset domain admits OTHER; authenticated=1");
}
TEST_F(Authentication, VerifyDecides) {
  setup.verifies = true;
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("žąsis", "callback-secret", "ŽEMĖ", false, true));
  EXPECT_EQ(seen.user, "žąsis");
  EXPECT_EQ(seen.domain, "ŽEMĖ");
  EXPECT_TRUE(seen.password == "callback-secret");
  EXPECT_NE(seen.thread, std::this_thread::get_id());
  permit = false;
  ASSERT_NO_FATAL_FAILURE(Attempt("žąsis", "callback-secret", "ŽEMĖ", false, false));
  RejectionLogs("callback-secret");
}
TEST_F(Authentication, MissingVerify) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "missing-secret", "LAB", false, false));
  RejectionLogs("missing-secret");
}
TEST_F(Authentication, NlaFixedPair) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", true, true));
  RecordProperty("trace", "nla fixed pair: user=alice domain=LAB authenticated=1");
}
TEST_F(Authentication, NlaUnicode) {
  setup.pair = FixedPair{ .user = "žąsis", .password = "unicode-secret", .domain = "ŽEMĖ" };
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("žąsis", "unicode-secret", "ŽEMĖ", true, true));
  RecordProperty("trace", "nla UTF-8 user=žąsis domain=ŽEMĖ authenticated=1");
}
TEST_F(Authentication, NlaVerifyDenies) {
  setup.verifies = true;
  setup.looks_up = true;
  permit         = false;
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", true, false));
  backend.Close();
  EXPECT_EQ(seen.order, "LV");
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, NlaWrongPassword) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "wrong-secret", "LAB", true, false));
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, NlaHashBeforePassword) {
  setup.verifies = true;
  setup.looks_up = true;
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "wrong-secret", "LAB", true, false));
  backend.Close();
  EXPECT_EQ(seen.order, "L");
  EXPECT_TRUE(seen.password.empty());
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, NlaCallbackOrder) {
  setup.verifies = true;
  setup.looks_up = true;
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", true, true));
  backend.Close();
  EXPECT_EQ(seen.order, "LV");
  EXPECT_TRUE(seen.password == "correct-secret");
  RecordProperty("trace", "nla: lookup -> hash check -> verify; authenticated=1");
}
TEST_F(Authentication, NlaAcceptsTls) {
  setup.verifies = true;
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", false, true));
  backend.Close();
  EXPECT_EQ(seen.order, "V");
  RecordProperty("trace", "nla server + tls client: verify only; authenticated=1");
}
TEST_F(Authentication, NlaVerifyOnlyUsesTheAccountHash) {
  setup.verifies = true;
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "wrong-secret", "LAB", true, false));
  EXPECT_TRUE(seen.order.empty());
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", true, true));
  backend.Close();
  EXPECT_EQ(seen.order, "V");
  RejectionLogs("wrong-secret");
  RecordProperty("trace", "nla: verify published, no lookup: the account's hash admits, then verify; authenticated=1");
}
TEST_F(Authentication, NlaMissingLookup) {
  setup.verifies = true;
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "missing-secret", "LAB", true, false));
  EXPECT_TRUE(seen.order.empty());
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "missing-secret", "LAB", false, true));
  backend.Close();
  EXPECT_EQ(seen.order, "V");
  RejectionLogs("missing-secret");
}
namespace {
auto DisconnectWithPending(Backend& backend, std::uint32_t code) -> void {
  auto const session = backend.Session().Lock();
  auto const frame   = backend.Frames().Lock();
  auto&      current = Required(backend.Session().Current(frame), "a client is current").get();
  current.Repaint(frame, { .x = 0, .y = 0, .w = 1, .h = 1 });
  auto& context = ConnectionProbe::Context(current.Status(frame).connection);
  freerdp_set_last_error(&context, code);
  // abi: psPeerCheckFileDescriptor, BOOL is int
  context.peer->CheckFileDescriptor = [](freerdp_peer*) -> int { return false; };
  current.Signal();
}
}
TEST_F(Authentication, RefusedSecurityLogs) {
  for (bool const nla : { true, false }) {
    ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls));
    {
      Client client(backend.Port(), false);
      client.Credentials({ .user = "alice", .password = "correct-secret", .domain = "LAB" }, nla);
      auto const settings = SettingsOf(client);
      settings.Set(BoolKey::TlsSecurity, false);
      settings.Set(BoolKey::RdpSecurity, !nla);
      EXPECT_FALSE(client.Connect());
    }
    backend.Close();
    ThenSecurityWarning(nla);
    logs.clear();
  }
}
TEST_F(Authentication, RejectedCertificateLogs) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls));
  bool verified = false;
  ASSERT_NO_FATAL_FAILURE(RejectCertificate(backend.Port(), verified));
  constexpr auto closed = "Connection closed before activation: ERRCONNECT_CONNECT_TRANSPORT_FAILED.";
  EXPECT_TRUE(Logged([&](auto const& entry) { return entry.second == closed; }));
  backend.Close();
  EXPECT_TRUE(verified);
  ThenCertificateDisconnect(closed);
}
TEST_F(Authentication, NlaUnknownIdentities) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Nla));
  ASSERT_NO_FATAL_FAILURE(Attempt("bob", "correct-secret", "LAB", true, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "OTHER", true, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "", true, false));
  RejectionLogs("correct-secret", 3);
}
TEST_F(Authentication, PendingDisconnectLogLevels) {
  for (auto code :
       { FREERDP_ERROR_CONNECT_TRANSPORT_FAILED, FREERDP_ERROR_LOGOFF_BY_USER, FREERDP_ERROR_CONNECT_FAILED }) {
    ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls));
    Client client(backend.Port(), false);
    client.Credentials({ .user = "alice", .password = "correct-secret", .domain = "LAB" });
    ASSERT_TRUE(client.Connect());
    ASSERT_TRUE(client.Until([&] { return CurrentStatus(*backend).has_value(); }));
    DisconnectWithPending(*backend, code);
    ThenPendingDisconnect(code);
    backend.Close();
    logs.clear();
  }
}
TEST_F(Authentication, TenRejectionsThenSuccess) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::Tls));
  for (std::size_t i = 0; i < 10; ++i) Attempt("alice", "wrong-secret", "LAB", false, false);
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", false, true));
  RejectionLogs("wrong-secret", 10);
}
TEST_F(Authentication, UnreadableKeyEndsThePeerWithItsReason) {
  ASSERT_NO_FATAL_FAILURE(Open(AuthMode::None, false));
  std::ofstream{ certificates.Path() / "server.key", std::ios::trunc } << "not a private key\n";
  Client client(backend.Port(), false);
  EXPECT_FALSE(client.Connect());
  EXPECT_TRUE(Logged([](auto const& entry) { return entry.second.contains("private key loading"); }));
}
}
