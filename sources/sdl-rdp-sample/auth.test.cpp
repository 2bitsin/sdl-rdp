#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>

#include <SDL3/SDL.h>
#include <winpr/ntlm.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace SampleGate {
namespace {
class AuthenticationSample : public SampleGate::Sample {
protected:
  auto ThenWrongPassword(std::uint32_t port) -> void {
    Client const wrong(port, true);
    wrong.Credentials("alice", "wrong-secret", "LAB", true);
    ASSERT_FALSE(wrong.Connect());
    ASSERT_TRUE(Read("event AUTH_REJECTED user=alice"));
  }
};
TEST_F(AuthenticationSample, AuthenticationPair) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ }, { "--user", "alice", "--password", "sample-secret", "--domain", "LAB" }));
  auto port = AnnouncedPort(line);
  ASSERT_NO_FATAL_FAILURE(ThenWrongPassword(port));
  Client const right(port, true);
  right.Credentials("alice", "sample-secret", "LAB", true);
  ASSERT_TRUE(right.Connect());
  ASSERT_TRUE(Read("event CONNECTED user=alice domain=LAB authenticated=1"));
  EXPECT_FALSE(process->Transcript().contains("sample-secret"));
  EXPECT_FALSE(process->Transcript().contains("wrong-secret"));
  Escape(right);
}
TEST_F(AuthenticationSample, AuthenticationPropertyDenies) {
  ASSERT_NO_FATAL_FAILURE(
      GivenProcess({ }, { "--user", "alice", "--password", "sample-secret", "--auth", "tls", "--verify-deny" }));
  auto const client = AnnouncedClient(320, 200);
  client.Credentials("alice", "sample-secret", "", false);
  ASSERT_FALSE(client.Connect());
  ASSERT_TRUE(Read("event AUTH_REJECTED user=alice"));
  EXPECT_FALSE(process->Transcript().contains("event CONNECTED"));
}
namespace {
struct PropertyCredentials {
public:
  static auto SDLCALL Verify(void* raw, char const* domain, char const* user, char const* password) -> bool {
    auto& self = *static_cast<PropertyCredentials*>(raw);
    self.arguments = self.arguments && std::string_view(domain) == "LAB" && std::string_view(user) == "alice"
                     && std::string_view(password) == "property-secret";
    ++self.verified;
    return true;
  }
  static auto SDLCALL Lookup(void* raw, char const* domain, char const* user, std::uint8_t* hash) -> bool {
    auto& self = *static_cast<PropertyCredentials*>(raw);
    self.arguments = self.arguments && std::string_view(domain) == "LAB" && std::string_view(user) == "alice";
    ++self.looked_up;
    auto secret = std::to_array("property-secret");
    return NTOWFv1A(secret.data(), secret.size() - 1, hash);
  }
  auto Verified() const -> std::size_t {
    return verified.load();
  }
  auto LookedUp() const -> std::size_t {
    return looked_up.load();
  }
  auto Arguments() const -> bool {
    return arguments.load();
  }

private:
  std::atomic<std::size_t> verified  = 0;
  std::atomic<std::size_t> looked_up = 0;
  std::atomic<bool>        arguments = true;
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
  auto operator=(Quit const&) -> Quit& = delete;
  auto operator=(Quit&&)      -> Quit& = delete;
};
auto GivenAuthenticationHints(fs::path const& certificates) -> void {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_PORT, "0"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_AUTH, "nla"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_BACKEND, BackendLibrary().c_str()));
}
auto GivenPropertyCredentials(SDL_PropertiesID properties, PropertyCredentials& credentials) -> void {
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER, &credentials));
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_VERIFY_POINTER,
                                     reinterpret_cast<void*>(PropertyCredentials::Verify)));
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER,
                                     reinterpret_cast<void*>(PropertyCredentials::Lookup)));
}
auto ConnectPropertyCredentials(std::uint32_t port) -> void {
  {
    Client const client(port, true);
    client.Credentials("alice", "property-secret", "LAB", false);
    ASSERT_TRUE(client.Connect());
  }
  {
    Client const client(port, true);
    client.Credentials("alice", "property-secret", "LAB", true);
    ASSERT_TRUE(client.Connect());
  }
}
}
TEST(DriverAuthentication, PropertiesReadAtCallTime) {
  oxbox::platform::ScratchArea const certificates{ "driver-auth", "sdl-rdp" };
  ASSERT_NO_FATAL_FAILURE(GivenAuthenticationHints(certificates.Path()));
  PropertyCredentials credentials;
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
  Quit const quit;
  auto       properties = SDL_GetDisplayProperties(SDL_GetPrimaryDisplay());
  auto       port       = PrimaryDisplayPort();
  ASSERT_NO_FATAL_FAILURE(GivenPropertyCredentials(properties, credentials));
  ASSERT_NO_FATAL_FAILURE(ConnectPropertyCredentials(port));
  SDL_Quit();
  SDL_ResetHints();
  EXPECT_EQ(credentials.Verified(), 2u);
  EXPECT_EQ(credentials.LookedUp(), 1u);
  EXPECT_TRUE(credentials.Arguments());
  RecordProperty("trace", "post-init display properties: verify=2 lookup=1; domain/user/password/userdata match");
}
}
}
