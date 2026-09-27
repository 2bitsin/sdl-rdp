#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::RuntimeFailure;

using ChannelQueryFailed = RuntimeFailure<"ChannelQueryFailed"_hash, "Virtual channel event query failed.">;
using CredentialFailed   = RuntimeFailure<"CredentialFailed"_hash, "Server credential {} failed.", std::string_view>;
using EventWaitFailed    = RuntimeFailure<"EventWaitFailed"_hash, "Event readiness wait failed.">;
using LibraryInitFailed  = RuntimeFailure<"LibraryInitFailed"_hash, "{} initialisation failed.", std::string_view>;
using ListenerFailed     = RuntimeFailure<"ListenerFailed"_hash, "Listener {} failed.", std::string_view>;
using NtlmHashFailed     = RuntimeFailure<"NtlmHashFailed"_hash, "NTLM {} hash failed.", std::string_view>;
using PeerContextFailed  = RuntimeFailure<"PeerContextFailed"_hash, "{} peer context failed.", std::string_view>;
}

namespace sdl_rdp::freerdp_facade {
using detail::exceptions::ChannelQueryFailed;
using detail::exceptions::CredentialFailed;
using detail::exceptions::EventWaitFailed;
using detail::exceptions::LibraryInitFailed;
using detail::exceptions::ListenerFailed;
using detail::exceptions::NtlmHashFailed;
using detail::exceptions::PeerContextFailed;
}
