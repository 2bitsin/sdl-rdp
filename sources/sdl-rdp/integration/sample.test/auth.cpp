#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>

#include <SDL3/SDL.h>
#include <winpr/ntlm.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace sdl_rdp::integration::sample_test::detail::auth {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::sample_gate_test::process::InitializedSdl;
using sdl_rdp::sample_gate_test::sample::AnnouncedPort;
using sdl_rdp::sample_gate_test::sample::Hint;
using sdl_rdp::sample_gate_test::sample::PrimaryDisplayPort;
using sdl_rdp::sample_gate_test::sample::Sample;
using sdl_rdp::sample_gate_test::sample::SetLoopbackHints;
using sdl_rdp::utilities::Expects;

namespace {
class AuthenticationSample : public Sample {
protected:
  auto ThenWrongPassword(std::uint32_t port) -> void {
    Client wrong(port, true);
    wrong.Credentials({ .user = "alice", .password = "wrong-secret", .domain = "LAB" }, true);
    ASSERT_FALSE(wrong.Connect());
    ASSERT_TRUE(Read("event AUTH_REJECTED user=alice"));
  }
};
TEST_F(AuthenticationSample, AuthenticationPair) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ }, { "--user", "alice", "--password", "sample-secret", "--domain", "LAB" }));
  auto port = AnnouncedPort(line);
  ASSERT_NO_FATAL_FAILURE(ThenWrongPassword(port));
  Client right(port, true);
  right.Credentials({ .user = "alice", .password = "sample-secret", .domain = "LAB" }, true);
  ASSERT_TRUE(right.Connect());
  ASSERT_TRUE(Read("event CONNECTED user=alice domain=LAB authenticated=1"));
  EXPECT_FALSE(process->Transcript().contains("sample-secret"));
  EXPECT_FALSE(process->Transcript().contains("wrong-secret"));
  Escape(right);
}
TEST_F(AuthenticationSample, AuthenticationPropertyDenies) {
  ASSERT_NO_FATAL_FAILURE(
      GivenProcess({ }, { "--user", "alice", "--password", "sample-secret", "--auth", "tls", "--verify-deny" }));
  auto client = AnnouncedClient(320, 200);
  client.Credentials({ .user = "alice", .password = "sample-secret", .domain = "" }, false);
  ASSERT_FALSE(client.Connect());
  ASSERT_TRUE(Read("event AUTH_REJECTED user=alice"));
  EXPECT_FALSE(process->Transcript().contains("event CONNECTED"));
}
namespace {
struct PropertyCredentials {
public:
  explicit PropertyCredentials(std::string_view expected = "property-secret") : password{ expected } { }
  // abi: SDL_PROP_DISPLAY_RDP_VERIFY_POINTER, the driver calls it with the userdata property
  static auto SDLCALL Verify(void* raw, char const* domain, char const* user, char const* password) -> bool {
    Expects(raw != nullptr, "the verifier names its credentials");
    Expects(domain != nullptr, "the verifier is handed a domain");
    Expects(user != nullptr, "the verifier is handed a user");
    Expects(password != nullptr, "the verifier is handed a password");
    auto& self = *static_cast<PropertyCredentials*>(raw);
    self.arguments = self.arguments && std::string_view(domain) == "LAB" && std::string_view(user) == "alice"
                     && std::string_view(password) == self.password;
    ++self.verified;
    return true;
  }
  // abi: SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER, the driver calls it with the userdata property
  static auto SDLCALL Lookup(void* raw, char const* domain, char const* user, std::uint8_t* hash) -> bool {
    Expects(raw != nullptr, "the lookup names its credentials");
    Expects(domain != nullptr, "the lookup is handed a domain");
    Expects(user != nullptr, "the lookup is handed a user");
    Expects(hash != nullptr, "the lookup is handed a hash buffer");
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
  std::string              password;
  std::atomic<std::size_t> verified  = 0;
  std::atomic<std::size_t> looked_up = 0;
  std::atomic<bool>        arguments = true;
};
auto GivenVerifyProperty(SDL_PropertiesID properties, PropertyCredentials& credentials) -> void {
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER, &credentials));
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_VERIFY_POINTER,
                                     reinterpret_cast<void*>(PropertyCredentials::Verify)));
}
auto GivenPropertyCredentials(SDL_PropertiesID properties, PropertyCredentials& credentials) -> void {
  ASSERT_NO_FATAL_FAILURE(GivenVerifyProperty(properties, credentials));
  ASSERT_TRUE(SDL_SetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER,
                                     reinterpret_cast<void*>(PropertyCredentials::Lookup)));
}
auto ConnectNla(std::uint32_t port, std::string_view password) -> bool {
  Client client(port, true);
  client.Credentials({ .user = "alice", .password = password, .domain = "LAB" }, true);
  return client.Connect();
}
auto ConnectPropertyCredentials(std::uint32_t port) -> void {
  {
    Client client(port, true);
    client.Credentials({ .user = "alice", .password = "property-secret", .domain = "LAB" }, false);
    ASSERT_TRUE(client.Connect());
  }
  {
    Client client(port, true);
    client.Credentials({ .user = "alice", .password = "property-secret", .domain = "LAB" }, true);
    ASSERT_TRUE(client.Connect());
  }
}
// SDL quits when use returns, so the caller reads the tallies after every verifier call has finished.
auto WithRdpDisplay(std::filesystem::path const& certificates, std::initializer_list<Hint> hints, auto const& use)
    -> void {
  InitializedSdl const sdl{ [&] { return SetLoopbackHints(certificates, hints) && SDL_Init(SDL_INIT_VIDEO); } };
  ASSERT_TRUE(sdl.Get()) << SDL_GetError();
  use(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), PrimaryDisplayPort());
}
}
TEST(DriverAuthentication, PropertiesReadAtCallTime) {
  oxbox::platform::ScratchArea const certificates{ "driver-auth", "sdl-rdp" };
  PropertyCredentials                credentials;
  ASSERT_NO_FATAL_FAILURE(WithRdpDisplay(certificates.Path(), { { .name = SDL_HINT_RDP_AUTH, .value = "nla" } },
                                         [&](SDL_PropertiesID properties, std::uint32_t port) {
                                           ASSERT_NO_FATAL_FAILURE(GivenPropertyCredentials(properties, credentials));
                                           ASSERT_NO_FATAL_FAILURE(ConnectPropertyCredentials(port));
                                         }));
  EXPECT_EQ(credentials.Verified(), 2u);
  EXPECT_EQ(credentials.LookedUp(), 1u);
  EXPECT_TRUE(credentials.Arguments());
  RecordProperty("trace", "post-init display properties: verify=2 lookup=1; domain/user/password/userdata match");
}
// The relay's own rule: a published verifier and no lookup leaves NLA's hash to the configured account.
TEST(DriverAuthentication, VerifyOnlyLeavesTheHashToTheAccount) {
  oxbox::platform::ScratchArea const certificates{ "driver-auth-account", "sdl-rdp" };
  PropertyCredentials                credentials { "account-secret"                 };
  std::initializer_list<Hint> const  account     { { .name = SDL_HINT_RDP_AUTH    , .value = "nla"            },
                                                   { .name = SDL_HINT_RDP_USER    , .value = "alice"          },
                                                   { .name = SDL_HINT_RDP_PASSWORD, .value = "account-secret" },
                                                   { .name = SDL_HINT_RDP_DOMAIN  , .value = "LAB"            } };
  ASSERT_NO_FATAL_FAILURE(
      WithRdpDisplay(certificates.Path(), account, [&](SDL_PropertiesID properties, std::uint32_t port) {
        ASSERT_NO_FATAL_FAILURE(GivenVerifyProperty(properties, credentials));
        EXPECT_FALSE(ConnectNla(port, "property-secret"));
        EXPECT_TRUE(ConnectNla(port, "account-secret"));
      }));
  EXPECT_EQ(credentials.Verified(), 1u);
  EXPECT_EQ(credentials.LookedUp(), 0u);
  EXPECT_TRUE(credentials.Arguments());
  RecordProperty("trace", "verify published, lookup absent, nla: the account's hash rejects then admits; verify=1");
}
}
}
