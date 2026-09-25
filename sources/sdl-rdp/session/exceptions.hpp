#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace Backend::detail::exceptions {
using std::string_view;

using AudioDeviceState    = LogicFailure<"AudioDeviceState"_hash, "Audio device is {}.", string_view>;
using ListenerSetupFailed = RuntimeFailure<"ListenerSetupFailed"_hash, "Listener {} failed.", string_view>;
using AddressNotIpv4      = ArgumentFailure<"AddressNotIpv4"_hash, "Listener address {} is not IPv4.", string_view>;
}
namespace Backend {
using detail::exceptions::AddressNotIpv4;
using detail::exceptions::AudioDeviceState;
using detail::exceptions::ListenerSetupFailed;
}
