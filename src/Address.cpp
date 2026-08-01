#include "plcsim/Address.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>

#include "plcsim/Error.hpp"

namespace plcsim {
namespace {

[[noreturn]] void invalid(std::string_view address, std::string_view reason)
{
    std::string message = "invalid PLC address \"";
    message.append(address);
    message += "\": ";
    message.append(reason);
    throw Error(ErrorKind::InvalidAddress, 0, {}, message);
}

[[nodiscard]] char upper(char c) noexcept
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

[[nodiscard]] bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

[[nodiscard]] bool is_space(char c) noexcept
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

[[nodiscard]] std::string_view trim(std::string_view s) noexcept
{
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

/// Reads a run of digits into `out`, guarding against overflow.
/// Returns false if there is no digit at `pos` or the value does not fit.
[[nodiscard]] bool read_number(std::string_view s, std::size_t& pos, std::uint32_t& out) noexcept
{
    if (pos >= s.size() || !is_digit(s[pos])) {
        return false;
    }

    constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t value = 0;
    while (pos < s.size() && is_digit(s[pos])) {
        const auto digit = static_cast<std::uint32_t>(s[pos] - '0');
        if (value > (kMax - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        ++pos;
    }

    out = value;
    return true;
}

}  // namespace

std::string BitAddress::to_string() const
{
    std::string out = "%";
    out.append(plcsim::to_string(area));
    out += std::to_string(byte);
    out += '.';
    out += std::to_string(bit);
    return out;
}

BitAddress parse_bit_address(std::string_view address)
{
    const std::string_view input = address;  // kept intact for error messages
    std::string_view s = trim(address);

    if (s.empty()) {
        invalid(input, "address is empty");
    }

    // Data-block addressing (DB1.DBX0.0) is out of scope; say so specifically
    // rather than reporting an unknown area letter.
    if (s.size() >= 2 && upper(s[0]) == 'D' && upper(s[1]) == 'B') {
        invalid(input,
                "data block addressing is not supported; this connector handles "
                "%I, %Q and %M bit addresses only");
    }

    std::size_t pos = 0;
    if (s[pos] == '%') {
        ++pos;
    }

    if (pos >= s.size()) {
        invalid(input, "expected an area letter (I, Q or M) after '%'");
    }

    Area area{};
    switch (upper(s[pos])) {
        case 'I':  // input, English
        case 'E':  // Eingang, German
            area = Area::Input;
            break;
        case 'Q':  // output, English
        case 'A':  // Ausgang, German
            area = Area::Output;
            break;
        case 'M':  // marker / Merker, both
            area = Area::Marker;
            break;
        default:
            invalid(input, "unknown area letter; expected I/E (input), Q/A (output) or M (marker)");
    }
    ++pos;

    // Optional IEC size prefix. 'X' means bit and is the only one we accept;
    // B/W/D are valid PLC syntax for wider accesses we deliberately do not do.
    if (pos < s.size()) {
        const char size_prefix = upper(s[pos]);
        if (size_prefix == 'X') {
            ++pos;
        } else if (size_prefix == 'B' || size_prefix == 'W' || size_prefix == 'D') {
            invalid(input,
                    "byte, word and double-word addressing is not supported; "
                    "use a bit address such as \"%I10.2\"");
        }
    }

    std::uint32_t byte = 0;
    if (!read_number(s, pos, byte)) {
        if (pos < s.size()) {
            invalid(input, "expected a byte offset after the area letter");
        }
        invalid(input, "byte offset is missing or too large");
    }

    if (pos >= s.size()) {
        invalid(input, "missing bit index; a bit address needs a '.', e.g. \"%I10.2\"");
    }
    if (s[pos] != '.') {
        invalid(input, "expected '.' between the byte offset and the bit index");
    }
    ++pos;

    std::uint32_t bit = 0;
    if (!read_number(s, pos, bit)) {
        invalid(input, "expected a bit index after '.'");
    }

    if (pos != s.size()) {
        invalid(input, "unexpected trailing characters");
    }

    if (bit > 7) {
        invalid(input, "bit index must be between 0 and 7");
    }

    return BitAddress{area, byte, static_cast<std::uint8_t>(bit)};
}

}  // namespace plcsim
