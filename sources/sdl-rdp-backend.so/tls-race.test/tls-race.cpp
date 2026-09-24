#include "_detail/certificate.hpp"
#include "_detail/contract.hpp"
#include "_detail/headless-client.hpp"
#include "_detail/test-config.hpp"
#include "_detail/tls-accept-refused.hpp"
#include "_detail/tls-rehearsal.hpp"
#include "sdl-rdp-backend.h"
#include "support.test/child-process.hpp"
#include "tls-race.test/injected-faults.hpp"
#include "tls-race.test/method-fill.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <oxbox/platform/scratch-area.hpp>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>
#include <winpr/ssl.h>
#include <winpr/wlog.h>

namespace {
using namespace std::chrono_literals;
using Race::InjectedFaults;
using Race::MethodFill;
using Race::Setter;
using Outcome = auto (*)(int status) -> bool;
constexpr unsigned Children          = 200;
constexpr unsigned FaultChildren     = 10;
constexpr unsigned ChildrenPerWave   = 10;
constexpr unsigned ChildLimitSeconds = 5;
constexpr auto     ShortCallLimit    = 250ms;
constexpr auto     SchedulingSlack   = 1s;
constexpr int      AcceptRefused     = 1;
constexpr int      MethodFilledLate  = 2;
constexpr int      FailedOtherwise   = 4;
constexpr int      Unbounded         = 8;

auto Child(std::function<int()> const& body) -> Headless::ChildProcess {
  return Headless::ChildProcess{ [&] {
    WLog_SetLogLevel(WLog_GetRoot(), WLOG_OFF);
    alarm(ChildLimitSeconds);
    return body();
  } };
}
auto WaveStatuses(unsigned size, std::function<int()> const& body) -> std::vector<int> {
  auto children = std::views::iota(0U, size) | std::views::transform([&](unsigned) { return Child(body); }) |
                  std::ranges::to<std::vector>();
  return children | std::views::transform([](auto& child) { return child.Wait(); }) | std::ranges::to<std::vector>();
}
auto Statuses(unsigned count, std::function<int()> const& body) -> std::vector<int> {
  utilities::Expects(count % ChildrenPerWave == 0, "the children fill whole waves");
  return std::views::iota(0U, count / ChildrenPerWave) |
         std::views::transform([&](unsigned) { return WaveStatuses(ChildrenPerWave, body); }) | std::views::join |
         std::ranges::to<std::vector>();
}
auto ExitedWith(int status, int code) -> bool {
  return WIFEXITED(status) && WEXITSTATUS(status) == code;
}
auto Clean(int status) -> bool {
  return ExitedWith(status, 0);
}
auto HeldWithoutLoss(int status) -> bool {
  return ExitedWith(status, MethodFilledLate);
}
auto LostTlsRace(int status) -> bool {
  return ExitedWith(status, AcceptRefused | MethodFilledLate);
}
// A peer context built while tcp.c's socket method has its ctrl but not its create dies of SIGSEGV in that ctrl.
auto CrashedOnSocketTable(int status) -> bool {
  return WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
}
constexpr std::array<std::pair<std::string_view, Outcome>, 4> Explained{ {
    { "clean"               , Clean                },
    { "held_without_loss"   , HeldWithoutLoss      },
    { "tls_race_lost"       , LostTlsRace          },
    { "socket_table_crashed", CrashedOnSocketTable },
} };
auto Unexplained(int status) -> bool {
  return std::ranges::none_of(Explained, [status](auto const& outcome) { return outcome.second(status); });
}
auto Count(std::vector<int> const& statuses, Outcome outcome) -> unsigned {
  return unsigned(std::ranges::count_if(statuses, outcome));
}
auto RecordOutcomes(std::vector<int> const& statuses) -> void {
  for (auto const& [name, outcome] : Explained)
    testing::Test::RecordProperty(std::string(name), std::to_string(Count(statuses, outcome)));
  testing::Test::RecordProperty("unexplained", std::to_string(Count(statuses, Unexplained)));
}
auto AsRacer(std::function<void()> const& step) -> int {
  auto const outcome = [&] {
    try {
      step();
      return 0;
    } catch (Backend::TlsAcceptRefused const&) {
      return AcceptRefused;
    } catch (std::exception const&) {
      return FailedOtherwise;
    }
  }();
  MethodFill::Shared().RacerDone();
  return outcome;
}
auto RaceTwice(std::function<void()> const& step, std::string_view method, Setter setter) -> int {
  MethodFill::Shared().Arm(method, setter);
  auto       first  = std::async(std::launch::async, AsRacer, std::cref(step));
  auto       second = std::async(std::launch::async, AsRacer, std::cref(step));
  auto const lost   = first.get() | second.get();
  return lost | (MethodFill::Shared().Fills() != 0 ? MethodFilledLate : 0);
}
auto RaceTwoRehearsals(Backend::Credentials const& credentials) -> int {
  return RaceTwice([&] { Backend::TlsRehearsal{ credentials }.Perform(); }, Race::TlsMethod, Setter::Write);
}
auto RaceTwoPeerContexts(Backend::Credentials const& credentials) -> int {
  return RaceTwice([&] { [[maybe_unused]] Backend::TlsRehearsal const built{ credentials }; }, Race::SocketMethod,
                   Setter::Create);
}
auto OpenedBackend(std::string const& certificates) -> std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> {
  auto const     config = Headless::LoopbackConfig(certificates);
  sdlrdp_handle* opened = nullptr;
  std::ignore = sdlrdp_open(&config, &opened);
  return { opened, sdlrdp_close };
}
auto ConnectToOpenedBackend(std::string const& certificates) -> int {
  auto const backend = OpenedBackend(certificates);
  if (!backend) return FailedOtherwise;
  MethodFill::Shared().Arm(Race::TlsMethod, Setter::Write);
  Headless::Client const client(sdlrdp_port(backend.get()), false);
  auto const             connected = freerdp_connect(client.Instance().get()) == TRUE;
  return (connected ? 0 : FailedOtherwise) | (MethodFill::Shared().Fills() != 0 ? MethodFilledLate : 0);
}
auto OpenFailsWithMessage(std::string const& certificates) -> int {
  auto const backend = OpenedBackend(certificates);
  return backend == nullptr && !std::string(sdlrdp_last_error()).empty() ? 0 : FailedOtherwise;
}
auto GivesUpWithin(std::chrono::milliseconds limit, Backend::Credentials const& credentials) -> int {
  Backend::TlsRehearsal rehearsal { credentials, limit };
  auto const            start     = std::chrono::steady_clock::now();
  try {
    std::move(rehearsal).Perform();
    return FailedOtherwise;
  } catch (std::runtime_error const&) {
    return std::chrono::steady_clock::now() - start < limit + SchedulingSlack ? 0 : Unbounded;
  }
}
auto ExpectCleanChildren(unsigned count, std::function<int()> const& body) -> void {
  auto const statuses = Statuses(count, body);
  RecordOutcomes(statuses);
  EXPECT_EQ(Count(statuses, Clean), count) << Count(statuses, Unexplained) << " unexplained";
}
class TlsRehearsalRace : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT));
    if (MethodFill::Shared().Seen(Race::SocketMethod) || MethodFill::Shared().Seen(Race::TlsMethod))
      GTEST_SKIP() << "FreeRDP filled its BIO methods earlier in this process.";
  }
  [[nodiscard]] auto ServerCredentials() const    -> Backend::Credentials const& { return credentials; }
  [[nodiscard]] auto CertificateDirectory() const -> std::string const& { return path; }

private:
  oxbox::platform::ScratchArea directory  { "tls-race", "sdl-rdp"                        };
  std::string const            path       { directory.Path().string()                    };
  Backend::Credentials const   credentials{ Backend::EnsureCertificate(directory.Path()) };
};
TEST_F(TlsRehearsalRace, UnrehearsedTlsAcceptsRace) {
  auto const statuses = Statuses(Children, [this] {
    [[maybe_unused]] Backend::TlsRehearsal const socket_tables_filled{ ServerCredentials() };
    return RaceTwoRehearsals(ServerCredentials());
  });
  RecordOutcomes(statuses);
  EXPECT_GT(Count(statuses, LostTlsRace), 0U) << "FreeRDP no longer races on tls.c's method; the accept can go.";
  EXPECT_EQ(Count(statuses, CrashedOnSocketTable), 0U) << "the socket tables were filled before the race";
  EXPECT_EQ(Count(statuses, Unexplained), 0U);
}
TEST_F(TlsRehearsalRace, UnrehearsedPeerContextsRace) {
  auto const statuses = Statuses(Children, [this] { return RaceTwoPeerContexts(ServerCredentials()); });
  RecordOutcomes(statuses);
  EXPECT_GT(Count(statuses, CrashedOnSocketTable), 0U) << "FreeRDP no longer races on tcp.c's socket method.";
  EXPECT_EQ(Count(statuses, LostTlsRace), 0U) << "no TLS accept ran";
  EXPECT_EQ(Count(statuses, Unexplained), 0U);
}
TEST_F(TlsRehearsalRace, RehearsedFirstUsesNeverRace) {
  ExpectCleanChildren(Children, [this] {
    Backend::TlsRehearsal{ ServerCredentials() }.Perform();
    return RaceTwoPeerContexts(ServerCredentials()) | RaceTwoRehearsals(ServerCredentials());
  });
}
TEST_F(TlsRehearsalRace, OpenedBackendNeverRacesItsFirstClient) {
  ExpectCleanChildren(Children, [this] { return ConnectToOpenedBackend(CertificateDirectory()); });
}
TEST_F(TlsRehearsalRace, RefusedKeyFailsOpen) {
  ExpectCleanChildren(FaultChildren, [this] {
    InjectedFaults::Shared().RefusePrivateKey();
    return OpenFailsWithMessage(CertificateDirectory());
  });
}
TEST_F(TlsRehearsalRace, SilentClientFailsOpen) {
  ExpectCleanChildren(FaultChildren, [this] {
    InjectedFaults::Shared().SilenceClient();
    return OpenFailsWithMessage(CertificateDirectory());
  });
}
TEST_F(TlsRehearsalRace, UnansweredClientGivesUpWithinLimit) {
  ExpectCleanChildren(FaultChildren, [this] {
    InjectedFaults::Shared().DropServerWrites();
    return GivesUpWithin(ShortCallLimit, ServerCredentials());
  });
}
}
