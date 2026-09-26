#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::RuntimeFailure;

using ChannelQueryFailed = RuntimeFailure<"ChannelQueryFailed"_hash, "Virtual channel event query failed.">;
using EventWaitFailed    = RuntimeFailure<"EventWaitFailed"_hash, "Event readiness wait failed.">;
using NtlmHashFailed     = RuntimeFailure<"NtlmHashFailed"_hash, "NTLM {} hash failed.", std::string_view>;
}

namespace sdl_rdp::freerdp_facade {
using detail::exceptions::ChannelQueryFailed;
using detail::exceptions::EventWaitFailed;
using detail::exceptions::NtlmHashFailed;
}
