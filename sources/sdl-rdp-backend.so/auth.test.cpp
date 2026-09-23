#include "_detail/test-auth.hpp"

namespace AuthenticationGate {
namespace {
void RejectCertificate(unsigned port, bool& rejected) {
  static thread_local bool verified;
  verified = false;
  Headless::Client const client(port, false);
  client.Credentials("alice", "correct-secret", "LAB", true);
  auto* settings = client.Instance()->context->settings;
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, TRUE));
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, FALSE));
  client.Instance()->VerifyCertificateEx = [](freerdp*, char const*, UINT16, char const*, char const*, char const*,
                                              char const*, DWORD) -> DWORD {
    verified = true;
    return 0;
  };
  EXPECT_FALSE(freerdp_connect(client.Instance().get()));
  rejected = verified;
}
}
TEST_F(Authentication, NoneReportsIdentity) {
  Open(SDLRDP_AUTH_NONE, false);
  Attempt("žąsis", "irrelevant-secret", "LAB", false, true);
  RecordProperty("trace", "none: user=žąsis domain=LAB authenticated=0");
}
TEST_F(Authentication, TlsFixedPair) {
  Open(SDLRDP_AUTH_TLS);
  Attempt("alice", "correct-secret", "LAB", false, true);
  RecordProperty("trace", "tls fixed pair: user=alice domain=LAB authenticated=1");
}
TEST_F(Authentication, DnsDomain) {
  std::string const domain = std::string(63, 'a') + "." + std::string(63, 'b') + ".example.org";
  config.domain            = domain.c_str();
  config.user              = "alice";
  config.password          = "correct-secret";
  Open(SDLRDP_AUTH_TLS, false);
  Attempt("alice", "correct-secret", domain.c_str(), false, true);
}
TEST_F(Authentication, WrongPassword) {
  Open(SDLRDP_AUTH_TLS);
  Attempt("alice", "wrong-secret", "LAB", false, false);
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, WrongUser) {
  Open(SDLRDP_AUTH_TLS);
  Attempt("mallory", "correct-secret", "LAB", false, false);
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, WrongDomain) {
  Open(SDLRDP_AUTH_TLS);
  Attempt("alice", "correct-secret", "OTHER", false, false);
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, UnsetDomain) {
  config.user     = "alice";
  config.password = "correct-secret";
  Open(SDLRDP_AUTH_TLS, false);
  Attempt("alice", "correct-secret", "OTHER", false, true);
  RecordProperty("trace", "tls fixed pair: unset domain admits OTHER; authenticated=1");
}
TEST_F(Authentication, VerifyDecides) {
  config.verify = Verify;
  Open(SDLRDP_AUTH_TLS, false);
  Attempt("žąsis", "callback-secret", "ŽEMĖ", false, true);
  EXPECT_EQ(seen_user, "žąsis");
  EXPECT_EQ(seen_domain, "ŽEMĖ");
  EXPECT_TRUE(seen_password == "callback-secret");
  EXPECT_NE(callback_thread, std::this_thread::get_id());
  permit = false;
  Attempt("žąsis", "callback-secret", "ŽEMĖ", false, false);
  RejectionLogs("callback-secret");
}
TEST_F(Authentication, MissingVerify) {
  Open(SDLRDP_AUTH_TLS, false);
  Attempt("alice", "missing-secret", "LAB", false, false);
  RejectionLogs("missing-secret");
}
TEST_F(Authentication, NlaFixedPair) {
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "correct-secret", "LAB", true, true);
  RecordProperty("trace", "nla fixed pair: user=alice domain=LAB authenticated=1");
}
TEST_F(Authentication, NlaUnicode) {
  config.user     = "žąsis";
  config.password = "unicode-secret";
  config.domain   = "ŽEMĖ";
  Open(SDLRDP_AUTH_NLA, false);
  Attempt("žąsis", "unicode-secret", "ŽEMĖ", true, true);
  RecordProperty("trace", "nla UTF-8 user=žąsis domain=ŽEMĖ authenticated=1");
}
TEST_F(Authentication, NlaVerifyDenies) {
  config.verify = Verify;
  config.lookup = Lookup;
  permit        = false;
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "correct-secret", "LAB", true, false);
  handle.reset();
  EXPECT_EQ(order, "LV");
  RejectionLogs("correct-secret");
}
TEST_F(Authentication, NlaWrongPassword) {
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "wrong-secret", "LAB", true, false);
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, NlaHashBeforePassword) {
  config.verify = Verify;
  config.lookup = Lookup;
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "wrong-secret", "LAB", true, false);
  handle.reset();
  EXPECT_EQ(order, "L");
  EXPECT_TRUE(seen_password.empty());
  RejectionLogs("wrong-secret");
}
TEST_F(Authentication, NlaCallbackOrder) {
  config.verify = Verify;
  config.lookup = Lookup;
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "correct-secret", "LAB", true, true);
  handle.reset();
  EXPECT_EQ(order, "LV");
  EXPECT_TRUE(seen_password == "correct-secret");
  RecordProperty("trace", "nla: lookup -> hash check -> verify; authenticated=1");
}
TEST_F(Authentication, NlaAcceptsTls) {
  config.verify = Verify;
  Open(SDLRDP_AUTH_NLA);
  Attempt("alice", "correct-secret", "LAB", false, true);
  handle.reset();
  EXPECT_EQ(order, "V");
  RecordProperty("trace", "nla server + tls client: verify only; authenticated=1");
}
TEST_F(Authentication, NlaMissingLookup) {
  config.verify = Verify;
  Open(SDLRDP_AUTH_NLA, false);
  Attempt("alice", "missing-secret", "LAB", true, false);
  EXPECT_TRUE(order.empty());
  Attempt("alice", "missing-secret", "LAB", false, true);
  handle.reset();
  EXPECT_EQ(order, "V");
  RejectionLogs("missing-secret");
}
namespace {
void DisconnectWithPending(Backend::State& state, UINT32 code) {
  {
    std::scoped_lock const lock(state.session_guard);
    auto& peer = *state.current;
    {
      std::scoped_lock const frame(state.frame_guard);
      peer.dirty.Add({ 0, 0, 1, 1 });
    }
    freerdp_set_last_error(peer.client->context, code);
    peer.client->CheckFileDescriptor = [](freerdp_peer*) -> BOOL { return FALSE; };
    peer.wake.Transition(Backend::WakeEvent::Phase::Pending);
  }
}
}
TEST_F(Authentication, RefusedSecurityLogs) {
  for (bool const nla : { true, false }) {
    Open(SDLRDP_AUTH_TLS);
    {
      Headless::Client const client(sdlrdp_port(handle.get()), false);
      client.Credentials("alice", "correct-secret", "LAB", nla);
      auto* settings = client.Instance()->context->settings;
      ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, FALSE));
      ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, !nla));
      EXPECT_FALSE(freerdp_connect(client.Instance().get()));
    }
    handle.reset();
    ThenSecurityWarning(nla);
    logs.clear();
  }
}
TEST_F(Authentication, RejectedCertificateLogs) {
  Open(SDLRDP_AUTH_TLS);
  bool verified = false;
  RejectCertificate(sdlrdp_port(handle.get()), verified);
  if (::testing::Test::HasFatalFailure()) return;
  constexpr auto closed = "Connection closed before activation: ERRCONNECT_CONNECT_TRANSPORT_FAILED.";
  EXPECT_TRUE(
      Until([&] { return std::ranges::any_of(logs, [&](auto const& entry) { return entry.second == closed; }); }));
  handle.reset();
  EXPECT_TRUE(verified);
  ThenCertificateDisconnect(closed);
}
TEST_F(Authentication, NlaUnknownIdentities) {
  Open(SDLRDP_AUTH_NLA);
  Attempt("bob", "correct-secret", "LAB", true, false);
  Attempt("alice", "correct-secret", "OTHER", true, false);
  Attempt("alice", "correct-secret", "", true, false);
  RejectionLogs("correct-secret", 3);
}
TEST_F(Authentication, PendingDisconnectLogLevels) {
  for (auto code :
       { FREERDP_ERROR_CONNECT_TRANSPORT_FAILED, FREERDP_ERROR_LOGOFF_BY_USER, FREERDP_ERROR_CONNECT_FAILED }) {
    Open(SDLRDP_AUTH_TLS);
    Headless::Client client(sdlrdp_port(handle.get()), false);
    client.Credentials("alice", "correct-secret", "LAB");
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
    auto& state = *handle->state;
    ASSERT_TRUE(client.Until([&] {
      std::scoped_lock const lock(state.session_guard);
      return state.current != nullptr;
    }));
    DisconnectWithPending(state, code);
    ThenPendingDisconnect(code);
    handle.reset();
    logs.clear();
  }
}
TEST_F(Authentication, TenRejectionsThenSuccess) {
  Open(SDLRDP_AUTH_TLS);
  for (unsigned i = 0; i < 10; ++i)
    Attempt("alice", "wrong-secret", "LAB", false, false);
  Attempt("alice", "correct-secret", "LAB", false, true);
  RejectionLogs("wrong-secret", 10);
}
}
