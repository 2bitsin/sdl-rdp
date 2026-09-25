#include <sdl-rdp/headless-client.test/backend/authentication.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <ranges>

namespace sdl_rdp::integration::auth_test::detail::authentication {
using sdl_rdp::headless_client_test::backend::Authentication;
using sdl_rdp::headless_client_test::backend::CurrentStatus;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::utilities::Required;

namespace {
auto RejectCertificate(std::uint32_t port, bool& rejected) -> void {
  static thread_local bool verified;
  verified = false;
  Client client(port, false);
  client.Credentials({ .user = "alice", .password = "correct-secret", .domain = "LAB" }, true);
  auto* settings = client.Instance()->context->settings;
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, true));
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, false));
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
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NONE, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("žąsis", "irrelevant-secret", "LAB", false, true));
  RecordProperty("trace", "none: user=žąsis domain=LAB authenticated=0");
}
TEST_F(Authentication, TlsFixedPair) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", false, true));
  RecordProperty("trace", "tls fixed pair: user=alice domain=LAB authenticated=1");
}
TEST_F(Authentication, DnsDomain) {
  std::string const domain = std::string(63, 'a') + "." + std::string(63, 'b') + ".example.org";
  config.domain   = domain.c_str();
  config.user     = "alice";
  config.password = "correct-secret";
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS, false));
  Attempt("alice", "correct-secret", domain.c_str(), false, true);
}
TEST_F(Authentication, WrongPassword) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "wrong-secret", "LAB", false, false));
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, WrongUser) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS));
  ASSERT_NO_FATAL_FAILURE(Attempt("mallory", "correct-secret", "LAB", false, false));
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, WrongDomain) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "OTHER", false, false));
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, UnsetDomain) {
  config.user     = "alice";
  config.password = "correct-secret";
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "OTHER", false, true));
  RecordProperty("trace", "tls fixed pair: unset domain admits OTHER; authenticated=1");
}
TEST_F(Authentication, VerifyDecides) {
  config.verify = Verify;
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS, false));
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
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "missing-secret", "LAB", false, false));
  RejectionLogs("missing-secret");
}
TEST_F(Authentication, NlaFixedPair) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", true, true));
  RecordProperty("trace", "nla fixed pair: user=alice domain=LAB authenticated=1");
}
TEST_F(Authentication, NlaUnicode) {
  config.user     = "žąsis";
  config.password = "unicode-secret";
  config.domain   = "ŽEMĖ";
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("žąsis", "unicode-secret", "ŽEMĖ", true, true));
  RecordProperty("trace", "nla UTF-8 user=žąsis domain=ŽEMĖ authenticated=1");
}
TEST_F(Authentication, NlaVerifyDenies) {
  config.verify = Verify;
  config.lookup = Lookup;
  permit        = false;
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", true, false));
  handle.Close();
  EXPECT_EQ(seen.order, "LV");
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, NlaWrongPassword) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "wrong-secret", "LAB", true, false));
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, NlaHashBeforePassword) {
  config.verify = Verify;
  config.lookup = Lookup;
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "wrong-secret", "LAB", true, false));
  handle.Close();
  EXPECT_EQ(seen.order, "L");
  EXPECT_TRUE(seen.password.empty());
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, NlaCallbackOrder) {
  config.verify = Verify;
  config.lookup = Lookup;
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", true, true));
  handle.Close();
  EXPECT_EQ(seen.order, "LV");
  EXPECT_TRUE(seen.password == "correct-secret");
  RecordProperty("trace", "nla: lookup -> hash check -> verify; authenticated=1");
}
TEST_F(Authentication, NlaAcceptsTls) {
  config.verify = Verify;
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", false, true));
  handle.Close();
  EXPECT_EQ(seen.order, "V");
  RecordProperty("trace", "nla server + tls client: verify only; authenticated=1");
}
TEST_F(Authentication, NlaMissingLookup) {
  config.verify = Verify;
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "missing-secret", "LAB", true, false));
  EXPECT_TRUE(seen.order.empty());
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "missing-secret", "LAB", false, true));
  handle.Close();
  EXPECT_EQ(seen.order, "V");
  RejectionLogs("missing-secret");
}
namespace {
auto DisconnectWithPending(sdlrdp_handle& handle, std::uint32_t code) -> void {
  auto const session = handle.Session().Lock();
  auto const frame   = handle.Frames().Lock();
  auto&      current = Required(handle.Session().Current(frame), "a client is current").get();
  current.Repaint(frame, { 0, 0, 1, 1 });
  auto& client = current.Status(frame).client.get();
  freerdp_set_last_error(client.context, code);
  // abi: psPeerCheckFileDescriptor, BOOL is int
  client.CheckFileDescriptor = [](freerdp_peer*) -> int { return false; };
  current.Signal();
}
}
TEST_F(Authentication, RefusedSecurityLogs) {
  for (bool const nla : { true, false }) {
    ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS));
    {
      Client client(sdlrdp_port(handle.Handle()), false);
      client.Credentials({ .user = "alice", .password = "correct-secret", .domain = "LAB" }, nla);
      auto* settings = client.Instance()->context->settings;
      ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, false));
      ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, !nla));
      EXPECT_FALSE(client.Connect());
    }
    handle.Close();
    ThenSecurityWarning(nla);
    logs.clear();
  }
}
TEST_F(Authentication, RejectedCertificateLogs) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS));
  bool verified = false;
  ASSERT_NO_FATAL_FAILURE(RejectCertificate(sdlrdp_port(handle.Handle()), verified));
  constexpr auto closed = "Connection closed before activation: ERRCONNECT_CONNECT_TRANSPORT_FAILED.";
  EXPECT_TRUE(
      Until([&] { return std::ranges::any_of(logs, [&](auto const& entry) { return entry.second == closed; }); }));
  handle.Close();
  EXPECT_TRUE(verified);
  ThenCertificateDisconnect(closed);
}
TEST_F(Authentication, NlaUnknownIdentities) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NLA));
  ASSERT_NO_FATAL_FAILURE(Attempt("bob", "correct-secret", "LAB", true, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "OTHER", true, false));
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "", true, false));
  RejectionLogs("correct-secret", 3);
}
TEST_F(Authentication, PendingDisconnectLogLevels) {
  for (auto code :
       { FREERDP_ERROR_CONNECT_TRANSPORT_FAILED, FREERDP_ERROR_LOGOFF_BY_USER, FREERDP_ERROR_CONNECT_FAILED }) {
    ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS));
    Client client(sdlrdp_port(handle.Handle()), false);
    client.Credentials({ .user = "alice", .password = "correct-secret", .domain = "LAB" });
    ASSERT_TRUE(client.Connect());
    ASSERT_TRUE(client.Until([&] { return CurrentStatus(*handle).has_value(); }));
    DisconnectWithPending(*handle, code);
    ThenPendingDisconnect(code);
    handle.Close();
    logs.clear();
  }
}
TEST_F(Authentication, TenRejectionsThenSuccess) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_TLS));
  for (std::size_t i = 0; i < 10; ++i) Attempt("alice", "wrong-secret", "LAB", false, false);
  ASSERT_NO_FATAL_FAILURE(Attempt("alice", "correct-secret", "LAB", false, true));
  RejectionLogs("wrong-secret", 10);
}
TEST_F(Authentication, UnreadableKeyEndsThePeerWithItsReason) {
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_AUTH_NONE, false));
  std::ofstream{ certificates.Path() / "server.key", std::ios::trunc } << "not a private key\n";
  Client client(sdlrdp_port(handle.Handle()), false);
  EXPECT_FALSE(client.Connect());
  auto const reported = [&] {
    return std::ranges::any_of(logs, [](auto const& entry) { return entry.second.contains("private key loading"); });
  };
  EXPECT_TRUE(Until(reported));
}
}
