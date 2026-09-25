#pragma once
#include <oxbox/utilities/exception.hpp>
#include <oxbox/utilities/fixed-string.hpp>
#include <oxbox/utilities/hash.hpp>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace Backend::detail::exceptions {
using oxbox::utilities::Exception;
using oxbox::utilities::FixedString;
using oxbox::utilities::literals::operator""_hash;

template <auto ID, FixedString FORMAT, typename... ArgsTy>
using RuntimeFailure = Exception<ID, std::runtime_error, FORMAT, ArgsTy...>;
template <auto ID, FixedString FORMAT, typename... ArgsTy>
using ArgumentFailure = Exception<ID, std::invalid_argument, FORMAT, ArgsTy...>;
template <auto ID, FixedString FORMAT, typename... ArgsTy>
using LogicFailure = Exception<ID, std::logic_error, FORMAT, ArgsTy...>;

using AllocationFailed = RuntimeFailure<"AllocationFailed"_hash, "{} allocation failed.", std::string_view>;
using MissingRequired  = ArgumentFailure<"MissingRequired"_hash, "Expected: {}", std::string_view>;
using NullArgument     = ArgumentFailure<"NullArgument"_hash, "{} is null.", std::string_view>;
using InvalidEncoding  = RuntimeFailure<"InvalidEncoding"_hash, "Invalid text encoding.">;
using Unencodable      = RuntimeFailure<"Unencodable"_hash, "Codepoint U+{:04X} is unrepresentable.", std::uint32_t>;
using OutOfBounds = RuntimeFailure<"OutOfBounds"_hash, "{} is outside {} to {}", std::int64_t, std::int64_t,
                                   std::int64_t>;
}
namespace Backend {
// Alias writers in every module name their ids with the literal.
using oxbox::utilities::literals::operator""_hash;
using detail::exceptions::AllocationFailed;
using detail::exceptions::ArgumentFailure;
using detail::exceptions::InvalidEncoding;
using detail::exceptions::LogicFailure;
using detail::exceptions::MissingRequired;
using detail::exceptions::NullArgument;
using detail::exceptions::OutOfBounds;
using detail::exceptions::RuntimeFailure;
using detail::exceptions::Unencodable;
}
