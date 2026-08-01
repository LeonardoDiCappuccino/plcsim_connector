#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "plcsim/Types.hpp"

namespace plcsim {

/// A parsed single-bit PLC address, e.g. %I10.2 -> {Input, 10, 2}.
struct BitAddress {
    Area area{Area::Input};
    std::uint32_t byte{};
    std::uint8_t bit{};

    /// Canonical rendering, always with '%' and the English mnemonic: "%I10.2".
    [[nodiscard]] std::string to_string() const;

    friend bool operator==(const BitAddress&, const BitAddress&) = default;
};

/// Parses a single-bit PLC address.
///
/// Accepted, case-insensitively, with optional surrounding whitespace:
///   - the leading '%' is optional:            "%I10.2" or "I10.2"
///   - English or German mnemonics:            I/E (input), Q/A (output), M (marker)
///   - the IEC bit-size prefix 'X' is optional: "%IX10.2"
///
/// Throws Error(ErrorKind::InvalidAddress) on anything else, including
/// byte/word/dword forms (%IB10, %QW4), data-block addresses (DB1.DBX0.0) and
/// bit indices above 7. Range against the actual area size is *not* checked
/// here - that needs a live instance; see Instance::setAddressBit().
[[nodiscard]] BitAddress parse_bit_address(std::string_view address);

}  // namespace plcsim
