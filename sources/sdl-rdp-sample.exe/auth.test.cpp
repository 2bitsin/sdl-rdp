#include "support.test/sample-launch.hpp"
#include "support.test/sample.hpp"

#include <SDL3/SDL.h>
#include <atomic>
#include <filesystem>
#include <winpr/ntlm.h>

namespace SampleGate {
namespace {
class AuthenticationSample : public SampleGate::Sample {
protected:
  auto ThenWrongPassword(unsigned port) -> void {
    Client const wrong(port, true);
    wrong.Credentials("alice", "wrong-secret", "LAB", true);
    ASSERT_FALSE(freerdp_connect(wrong.Instance().get()));
    ASSERT_TRUE(Read("event AUTH_REJECTED user=alice"));
  }
};
TEST_F(AuthenticationSample, AuthenticationPair) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--user", "alice", "--password", "sample-secret", "--domain", "LAB" });
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  auto port = AnnouncedPort(line);
  ThenWrongPassword(port);
  if (::testing::Test::HasFatalFailure()) return;
  Client const right(port, true);
  right.Credentials("alice", "sample-secret", "LAB", true);
  ASSERT_TRUE(freerdp_connect(right.Instance().get()));
  ASSERT_TRUE(Read("event CONNECTED user=alice domain=LAB authenticated=1"));
  EXPECT_FALSE(process->Transcript().contains("sample-secret"));
  EXPECT_FALSE(process->Transcript().contains("wrong-secret"));
  Escape(right);
}
TEST_F(AuthenticationSample, AuthenticationPropertyDenies) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(),
                   { "--user", "alice", "--password", "sample-secret", "--auth", "tls", "--verify-deny" });
  GivenProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  Client const client(AnnouncedPort(line), true);
  client.Credentials("alice", "sample-secret", "", false);
  ASSERT_FALSE(freerdp_connect(client.Instance().get()));
  ASSERT_TRUE(Read("event AUTH_REJECTED user=alice"));
  EXPECT_FALSE(process->Transcript().contains("event CONNECTED"));
}
namespace {
struct PropertyCredentials {
public:
  static auto SDLCALL Verify(void* raw, char const* domain, char const* user, char const* password) -> bool {
    auto& self = *static_cast<PropertyCredentials*>(raw);
    self.arguments = self.arguments && std::string_view(domain) == "LAB" && std::string_view(user) == "alice" &&
                     std::string_view(password) == "property-secret";
    ++self.verified;
    return true;
  }
  static auto SDLCALL Lookup(void* raw, char const* domain, char const* user, Uint8* hash) -> bool {
    auto& self = *static_cast<PropertyCredentials*>(raw);
    self.arguments = self.arguments && std::string_view(domain) == "LAB" && std::string_view(user) == "alice";
    ++self.looked_up;
    auto secret = std::to_array("property-secret");
    return NTOWFv1A(secret.data(), secret.size() - 1, hash);
  }
  auto Verified() const  -> unsigned { return verified.load(); }
  auto LookedUp() const  -> unsigned { return looked_up.load(); }
  auto Arguments() const -> bool { return arguments.load(); }

private:
  std::atomic<unsigned> verified  = 0;
  std::atomic<unsigned> looked_up = 0;
  std::atomic<bool>     arguments = true;
};
}
namespace {
struct Quit {
public:
  Quit(Quit const&) = delete;
  Quit(Quit&&)      = delete;
  Quit()            = default;
  ~Quit() {
    SDL_Quit();
    SDL_ResetHints();
  }
  auto operator = (Quit const&) -> Quit& = delete;
  auto operator = (Quit&&)      -> Quit& = delete;
};
auto GivenAuthenticationHints(fs::path const& certificates) -> void {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_PORT, "0"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUTH, "nla"));
  ASSERT_TRUE(
      SDL_SetHint(SDL_HINT_RDP_BACKEND, (BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so").c_str()));
}
auto GivenPropertyCredentials(SDL_PropertiesID properties, PropertyCredentials& credentials) -> void {
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER, &credentials));
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_VERIFY_POINTER,
                                     reinterpret_cast<void*>(PropertyCredentials::Verify)));
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER,
                                     reinterpret_cast<void*>(PropertyCredentials::Lookup)));
}
auto ConnectPropertyCredentials(unsigned port) -> void {
  {
    Client const client(port, true);
    client.Credentials("alice", "property-secret", "LAB", false);
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  }
  {
    Client const client(port, true);
    client.Credentials("alice", "property-secret", "LAB", true);
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  }
}
}
TEST(DriverAuthentication, PropertiesReadAtCallTime) {
  oxbox::platform::ScratchArea const certificates{ "driver-auth", "sdl-rdp" };
  GivenAuthenticationHints(certificates.Path());
  if (::testing::Test::HasFatalFailure()) return;
  PropertyCredentials credentials;
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
  Quit const quit;
  auto       properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  auto       port       = SDL_GetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0);
  GivenPropertyCredentials(properties, credentials);
  if (::testing::Test::HasFatalFailure()) return;
  ConnectPropertyCredentials(port);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Quit();
  SDL_ResetHints();
  EXPECT_EQ(credentials.Verified(), 2u);
  EXPECT_EQ(credentials.LookedUp(), 1u);
  EXPECT_TRUE(credentials.Arguments());
  RecordProperty("trace", "post-init display properties: verify=2 lookup=1; domain/user/password/userdata match");
}
}
}
