#pragma once

// [stdc++]
#include <string>
// [tng]
#include <iso8583/config.h>

namespace TNG_NAMESPACE {

/// Enum defining various padding methods.
/// These padding methods can be used to fill or truncate strings.
///
/// \note Available padding variants:
/// - Padder::NONE: No padding
/// - Padder::LEFT_ZERO: Pads with zeros on the left (common in numeric fields)
/// - Padder::RIGHT_SPACE: Pads with spaces on the right (common in alphabetic fields)
/// - Padder::RIGHT_T_SPACE: Pads with spaces on the right but truncates during parsing
enum class TNG_EXPORT Padder
{
    NONE, ///< No padding
    LEFT_ZERO, ///< Pads with zeros on the left
    RIGHT_SPACE, ///< Pads with spaces on the right
    RIGHT_T_SPACE, ///< Pads with spaces on the right with truncation
};

/// Pads the given string `s` using the padding method specified by the template parameter.
/// The padding method is chosen based on the provided Padder variant.
///
/// \param s The string to be padded.
/// \param mlen The desired maximum length of the string.
/// \tparam e The Padder type that defines the padding method.
template < Padder e >
/*static constexpr*/ void TNG_EXPORT pad(std::string& s, std::size_t mlen);

}
