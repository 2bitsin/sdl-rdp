#include "_detail/sample-fixture.hpp"
#include <atomic>
#include <winpr/ntlm.h>

namespace SampleGate {
TEST_F(Sample, AuthenticationPair) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--user", "alice", "--password", "sample-secret", "--domain", "LAB"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  Client wrong(port, true);
  wrong.Credentials("alice", "wrong-secret", "LAB", true);
  ASSERT_FALSE(freerdp_connect(wrong.instance.get()));
  ASSERT_TRUE(Read("event AUTH_REJECTED user=alice"));
  Client right(port, true);
  right.Credentials("alice", "sample-secret", "LAB", true);
  ASSERT_TRUE(freerdp_connect(right.instance.get()));
  ASSERT_TRUE(Read("event CONNECTED user=alice domain=LAB authenticated=1"));
  EXPECT_FALSE(process->transcript.contains("sample-secret"));
  EXPECT_FALSE(process->transcript.contains("wrong-secret"));
  ASSERT_NO_FATAL_FAILURE(Escape(right));
}
TEST_F(Sample, AuthenticationPropertyDenies) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--user", "alice", "--password", "sample-secret", "--auth", "tls", "--verify-deny"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true);
  client.Credentials("alice", "sample-secret", "", false);
  ASSERT_FALSE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event AUTH_REJECTED user=alice"));
  EXPECT_FALSE(process->transcript.contains("event CONNECTED"));
}
namespace {
struct PropertyCredentials {
  std::atomic<unsigned> verified = 0, looked_up = 0;
  std::atomic<bool> arguments = true;
  static bool SDLCALL Verify(void* raw, char const* domain, char const* user, char const* password) {
    auto& self = *static_cast<PropertyCredentials*>(raw);
    self.arguments = self.arguments && std::string_view(domain) == "LAB" && std::string_view(user) == "alice"
      && std::string_view(password) == "property-secret";
    ++self.verified;
    return true;
  }
  static bool SDLCALL Lookup(void* raw, char const* domain, char const* user, Uint8 hash[16]) {
    auto& self = *static_cast<PropertyCredentials*>(raw);
    self.arguments = self.arguments && std::string_view(domain) == "LAB" && std::string_view(user) == "alice";
    ++self.looked_up;
    return NTOWFv1A(const_cast<char*>("property-secret"), sizeof("property-secret") - 1, hash);
  }
};
}
TEST(DriverAuthentication, PropertiesReadAtCallTime) {
  oxbox::platform::ScratchArea certificates{"driver-auth", "sdl-rdp"};
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_PORT, "0"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUTH, "nla"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_BACKEND, (BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so").c_str()));
  PropertyCredentials credentials;
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
  struct Quit { ~Quit() { SDL_Quit(); SDL_ResetHints(); } } quit;
  auto properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  auto port = SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0);
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER, &credentials));
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_VERIFY_POINTER, reinterpret_cast<void*>(PropertyCredentials::Verify)));
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER, reinterpret_cast<void*>(PropertyCredentials::Lookup)));
  {
    Client client(port, true);
    client.Credentials("alice", "property-secret", "LAB", false);
    ASSERT_TRUE(freerdp_connect(client.instance.get()));
  }
  {
    Client client(port, true);
    client.Credentials("alice", "property-secret", "LAB", true);
    ASSERT_TRUE(freerdp_connect(client.instance.get()));
  }
  SDL_Quit(); SDL_ResetHints();
  EXPECT_EQ(credentials.verified.load(), 2u); EXPECT_EQ(credentials.looked_up.load(), 1u);
  EXPECT_TRUE(credentials.arguments.load());
  RecordProperty("trace", "post-init display properties: verify=2 lookup=1; domain/user/password/userdata match");
}
}
