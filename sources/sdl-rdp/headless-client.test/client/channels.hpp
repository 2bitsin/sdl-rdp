#pragma once
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/scoped.hpp>

#include <freerdp/event.h>
#include <freerdp/freerdp.h>
#include <oxbox/utilities/fixed-string.hpp>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace sdl_rdp::headless_client_test::client::detail::channels {
using oxbox::utilities::FixedString;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::headless_client_test::utilities::OwnerOf;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::RAIIWrap;

auto LoadStaticChannel(freerdp& instance, std::string const& name)                                   -> bool;
auto LoadDynamicChannel(freerdp& instance, std::string const& name)                                  -> bool;
auto SendStaticChannel(freerdp& instance, std::string const& name, std::span<std::byte const> bytes) -> bool;
// abi: pLoadChannels, BOOL is int
template <auto LOAD, FixedString NAME> auto ChannelLoader(freerdp* instance) -> int {
  Expects(instance != nullptr, "channel loading names its client");
  return LOAD(*instance, std::string(NAME.view()));
}
template <class OwnerTy, class InterfaceTy> auto InterfaceOf(void (OwnerTy::*)(InterfaceTy&)) -> InterfaceTy;
// abi: pChannelConnectedEventHandler; CONNECTED is the observer's member taking the channel's interface.
template <FixedString NAME, auto CONNECTED> auto ChannelConnected(void* context, ChannelConnectedEventArgs const* event)
    -> void {
  Expects(context != nullptr, "the channel event names its client context");
  Expects(event != nullptr, "channel event is supplied");
  if (std::string_view(event->name) != NAME.view()) return;
  Expects(event->pInterface != nullptr, "the connected channel carries its interface");
  auto const observer = ObserverSet::Of(*static_cast<rdpContext*>(context)).Held<decltype(OwnerOf(CONNECTED))>();
  std::invoke(CONNECTED, *observer, *static_cast<decltype(InterfaceOf(CONNECTED))*>(event->pInterface));
}
template <auto HANDLER> auto Subscribe(rdpContext& context) -> rdpContext& {
  auto const subscribed = PubSub_SubscribeChannelConnected(context.pubSub, HANDLER);
  Ensures(subscribed == 0, "the client publishes channel connections");
  return context;
}
template <auto HANDLER> auto Unsubscribe(rdpContext& context) noexcept -> void {
  PubSub_UnsubscribeChannelConnected(context.pubSub, HANDLER);
}
// The observer's CONNECTED member receives NAME's interface while the holder lives.
template <FixedString NAME, auto CONNECTED>
using ChannelSubscription = RAIIWrap<rdpContext&, Subscribe<ChannelConnected<NAME, CONNECTED>>,
                                     Unsubscribe<ChannelConnected<NAME, CONNECTED>>>;
}

namespace sdl_rdp::headless_client_test::client {
using detail::channels::ChannelLoader;
using detail::channels::ChannelSubscription;
using detail::channels::LoadDynamicChannel;
using detail::channels::LoadStaticChannel;
using detail::channels::SendStaticChannel;
}
