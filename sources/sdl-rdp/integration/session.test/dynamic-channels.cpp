#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/headless-client.test/backend/contract-run.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/link/channel-slot.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>

namespace {
constexpr std::uint32_t AssignedId = 7;
constexpr int           Continued  = 3;
class Recorded final : public Backend::DynamicChannel {
public:
  auto Activate() -> bool override {
    ++_activations;
    return true;
  }
  auto Reject() -> void override {
    ++_rejections;
  }
  auto Activations() const -> std::size_t {
    return _activations;
  }
  auto Rejections() const -> std::size_t {
    return _rejections;
  }

private:
  std::size_t _activations{ };
  std::size_t _rejections { };
};
auto ActivateUnowned() -> int {
  Backend::DynamicChannels registry;
  return registry.Activate(AssignedId) ? 0 : Continued;
}
auto RejectUnowned() -> int {
  Backend::DynamicChannels registry;
  registry.Reject(AssignedId);
  return Continued;
}
auto AssignDuplicate() -> int {
  Backend::DynamicChannels registry;
  Recorded                 first;
  Recorded                 second;
  auto const               kept     = registry.Assign(AssignedId, first);
  {
    auto const duplicate = registry.Assign(AssignedId, second);
  }
  return registry.Activate(AssignedId) && first.Activations() == 1 ? Continued : 0;
}
}
TEST(DynamicChannels, ActivationReachesTheChannelThatOwnsTheId) {
  Backend::DynamicChannels registry;
  Recorded                 channel;
  Backend::ChannelSlot     slot    { registry, channel };
  EXPECT_TRUE(slot.Assign(AssignedId));
  EXPECT_TRUE(registry.Activate(AssignedId));
  EXPECT_EQ(channel.Activations(), 1U);
  EXPECT_EQ(channel.Rejections(), 0U);
}
TEST(DynamicChannels, RejectionReachesTheChannelThatOwnsTheId) {
  Backend::DynamicChannels registry;
  Recorded                 channel;
  Backend::ChannelSlot     slot    { registry, channel };
  slot.Assign(AssignedId);
  registry.Reject(AssignedId);
  EXPECT_EQ(channel.Rejections(), 1U);
  EXPECT_EQ(channel.Activations(), 0U);
}
TEST(DynamicChannels, DestroyedSlotFreesItsIdForTheNextChannel) {
  Backend::DynamicChannels registry;
  Recorded                 first;
  Recorded                 second;
  {
    Backend::ChannelSlot{ registry, first }.Assign(AssignedId);
  }
  Backend::ChannelSlot slot{ registry, second };
  slot.Assign(AssignedId);
  EXPECT_TRUE(registry.Activate(AssignedId));
  EXPECT_EQ(first.Activations(), 0U);
  EXPECT_EQ(second.Activations(), 1U);
}
TEST(DynamicChannels, ActivationForAnIdNoChannelOwnsFailsTheContract) {
  Headless::ContractRun{ ActivateUnowned }.ExpectBroken("channel id was assigned at open", Continued);
}
TEST(DynamicChannels, RejectionForAnIdNoChannelOwnsFailsTheContract) {
  Headless::ContractRun{ RejectUnowned }.ExpectBroken("channel id was assigned at open", Continued);
}
TEST(DynamicChannels, DuplicateAssignmentFailsTheContractAndKeepsTheFirstOwner) {
  Headless::ContractRun{ AssignDuplicate }.ExpectBroken("channel id is assigned once", Continued);
}
