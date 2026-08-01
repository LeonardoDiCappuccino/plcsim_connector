#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <string>

#include "plcsim/Address.hpp"
#include "plcsim/Error.hpp"

using Catch::Matchers::ContainsSubstring;
using plcsim::Area;
using plcsim::BitAddress;
using plcsim::Error;
using plcsim::ErrorKind;
using plcsim::parse_bit_address;

namespace {

/// Asserts that `text` is rejected, and with the expected classification.
void check_rejected(const char* text, ErrorKind expected = ErrorKind::InvalidAddress)
{
    INFO("input: \"" << text << '"');

    bool threw = false;
    try {
        (void)parse_bit_address(text);
    } catch (const Error& error) {
        threw = true;
        CHECK(error.kind() == expected);
    }
    CHECK(threw);
}

}  // namespace

TEST_CASE("English mnemonics parse", "[address]")
{
    CHECK(parse_bit_address("%I10.2") == BitAddress{Area::Input, 10, 2});
    CHECK(parse_bit_address("%Q4.1") == BitAddress{Area::Output, 4, 1});
    CHECK(parse_bit_address("%M0.0") == BitAddress{Area::Marker, 0, 0});
}

TEST_CASE("German mnemonics parse to the same areas", "[address]")
{
    CHECK(parse_bit_address("%E10.2") == parse_bit_address("%I10.2"));
    CHECK(parse_bit_address("%A4.1") == parse_bit_address("%Q4.1"));
}

TEST_CASE("Optional syntax is accepted", "[address]")
{
    const BitAddress expected{Area::Input, 10, 2};

    SECTION("leading % may be omitted") { CHECK(parse_bit_address("I10.2") == expected); }
    SECTION("IEC bit prefix X") { CHECK(parse_bit_address("%IX10.2") == expected); }
    SECTION("lower case") { CHECK(parse_bit_address("%i10.2") == expected); }
    SECTION("mixed case with X") { CHECK(parse_bit_address("ix10.2") == expected); }
    SECTION("surrounding whitespace") { CHECK(parse_bit_address("  %I10.2\t") == expected); }
}

TEST_CASE("Bit indices 0 through 7 are valid", "[address]")
{
    for (std::uint8_t bit = 0; bit <= 7; ++bit) {
        const std::string text = "%Q1." + std::to_string(bit);
        CHECK(parse_bit_address(text) == BitAddress{Area::Output, 1, bit});
    }
}

TEST_CASE("Large byte offsets are preserved", "[address]")
{
    CHECK(parse_bit_address("%M65535.7") == BitAddress{Area::Marker, 65535, 7});
    CHECK(parse_bit_address("%I4294967295.0").byte == 4294967295u);
}

TEST_CASE("to_string round-trips through the parser", "[address]")
{
    for (const char* text : {"%I10.2", "%Q4.1", "%M0.0", "%M65535.7"}) {
        CHECK(parse_bit_address(text).to_string() == text);
    }

    SECTION("alternative spellings normalise")
    {
        CHECK(parse_bit_address("e10.2").to_string() == "%I10.2");
        CHECK(parse_bit_address("%AX4.1").to_string() == "%Q4.1");
    }
}

TEST_CASE("Malformed addresses are rejected as InvalidAddress", "[address]")
{
    SECTION("empty and whitespace")
    {
        check_rejected("");
        check_rejected("   ");
    }
    SECTION("unknown area letter")
    {
        check_rejected("%Z1.0");
        check_rejected("%1.0");
    }
    SECTION("missing bit index")
    {
        check_rejected("%I10");
        check_rejected("%I10.");
    }
    SECTION("bit index out of range")
    {
        check_rejected("%I10.8");
        check_rejected("%Q0.99");
    }
    SECTION("byte offset overflows uint32") { check_rejected("%I4294967296.0"); }
    SECTION("trailing junk")
    {
        check_rejected("%I10.2x");
        check_rejected("%I10.2.3");
    }
    SECTION("no digits at all")
    {
        check_rejected("%I");
        check_rejected("%");
    }
}

TEST_CASE("Unsupported addressing modes explain themselves", "[address]")
{
    CHECK_THROWS_WITH(parse_bit_address("%IB10"),
                      ContainsSubstring("byte, word and double-word"));
    CHECK_THROWS_WITH(parse_bit_address("%QW4"),
                      ContainsSubstring("byte, word and double-word"));
    CHECK_THROWS_WITH(parse_bit_address("%MD100"),
                      ContainsSubstring("byte, word and double-word"));
    CHECK_THROWS_WITH(parse_bit_address("DB1.DBX0.0"),
                      ContainsSubstring("data block addressing is not supported"));
}

TEST_CASE("Error messages quote the offending input", "[address]")
{
    CHECK_THROWS_WITH(parse_bit_address("%I10.9"), ContainsSubstring("\"%I10.9\""));
}

TEST_CASE("Address parse errors are never retryable", "[address][retry]")
{
    bool threw = false;
    try {
        (void)parse_bit_address("%I10.9");
    } catch (const Error& error) {
        threw = true;
        CHECK(error.kind() == ErrorKind::InvalidAddress);
        CHECK_FALSE(error.retryable());
        CHECK(error.code() == 0);  // did not originate in the SDK
        CHECK(error.code_name().empty());
    }
    CHECK(threw);
}
