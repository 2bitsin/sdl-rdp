#pragma once
#include <sdl-rdp/utilities/exceptions.hpp>

#include <oxbox/utilities/hash.hpp>
#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::exceptions {
using oxbox::utilities::literals::operator""_hash;
using sdl_rdp::utilities::RuntimeFailure;
using std::string_view;

using ChannelOpenFailed  = RuntimeFailure<"ChannelOpenFailed"_hash, "Virtual channel {} open failed.", string_view>;
using ChannelQueryFailed = RuntimeFailure<"ChannelQueryFailed"_hash, "Virtual channel event query failed.">;
using CodecSetupFailed   = RuntimeFailure<"CodecSetupFailed"_hash, "{} codec setup failed.", string_view>;
using CredentialFailed   = RuntimeFailure<"CredentialFailed"_hash, "Server credential {} failed.", string_view>;
using EventWaitFailed    = RuntimeFailure<"EventWaitFailed"_hash, "Event readiness wait failed.">;
using LibraryInitFailed  = RuntimeFailure<"LibraryInitFailed"_hash, "{} initialisation failed.", string_view>;
using ListenerFailed     = RuntimeFailure<"ListenerFailed"_hash, "Listener {} failed.", string_view>;
using NtlmHashFailed     = RuntimeFailure<"NtlmHashFailed"_hash, "NTLM {} hash failed.", string_view>;
using PeerContextFailed  = RuntimeFailure<"PeerContextFailed"_hash, "{} peer context failed.", string_view>;
}

namespace sdl_rdp::freerdp_facade {
using detail::exceptions::ChannelOpenFailed;
using detail::exceptions::ChannelQueryFailed;
using detail::exceptions::CodecSetupFailed;
using detail::exceptions::CredentialFailed;
using detail::exceptions::EventWaitFailed;
using detail::exceptions::LibraryInitFailed;
using detail::exceptions::ListenerFailed;
using detail::exceptions::NtlmHashFailed;
using detail::exceptions::PeerContextFailed;
}
