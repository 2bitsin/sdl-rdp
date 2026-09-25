#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <freerdp/freerdp.h>
#include <gtest/gtest.h>
#include <atomic>
#include <latch>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>

namespace sdl_rdp::integration::rig_utilities_test::detail::observer_set {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::utilities::ObserverLease;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
namespace {
struct Probe {
  bool freed = false;
};
static_assert(!std::is_copy_constructible_v<ObserverLease<Probe>>);
static_assert(std::is_nothrow_move_constructible_v<ObserverLease<Probe>>);
}
TEST(ObserverSet, HeldObserverIsTheRegisteredOne) {
  ObserverSet set;
  Probe       probe;
  set.Add(probe);
  EXPECT_EQ(std::addressof(*set.Held<Probe>()), &probe);
  set.Remove<Probe>();
}
TEST(ObserverSet, RemoveWaitsForAnOutstandingLease) {
  ObserverSet set;
  Probe       probe;
  set.Add(probe);
  std::optional    lease  { set.Held<Probe>() };
  std::latch       started{ 1                 };
  std::atomic_bool removed{ false             };
  std::jthread     remover{ [&] {
    started.count_down();
    set.Remove<Probe>();
    removed = true;
  } };
  started.wait();
  EXPECT_FALSE(removed);
  lease.reset();
  remover.join();
  EXPECT_TRUE(removed);
}
TEST(ObserverSet, AMovedLeaseReleasesOnce) {
  ObserverSet set;
  Probe       probe;
  set.Add(probe);
  {
    auto       first  = set.Held<Probe>();
    auto const second = std::move(first);
    EXPECT_EQ(std::addressof(*second), &probe);
  }
  set.Remove<Probe>();
}
TEST(ObserverSet, OutlivesTheClientsFreeRdpTeardown) {
  Probe probe;
  {
    Client const client{ 1, false };
    ObserverSet::Of(*client.Instance()->context).Add(probe);
    // abi: pContextFree
    client.Instance()->ContextFree = [](freerdp*, rdpContext* context) {
      ObserverSet::Of(*context).Held<Probe>()->freed = true;
    };
  }
  EXPECT_TRUE(probe.freed);
}
}
