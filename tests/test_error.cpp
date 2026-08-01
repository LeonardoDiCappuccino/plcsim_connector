#include <catch2/catch_test_macros.hpp>

#include "plcsim/Error.hpp"

using plcsim::Error;
using plcsim::ErrorKind;
using plcsim::is_retryable;

TEST_CASE("Environmental failures are retryable", "[error][retry]")
{
    // These are exactly the states a reconnect loop should sit and wait out:
    // the Control Panel is not up yet, the instance has not been created yet,
    // the link dropped, or a call timed out.
    CHECK(is_retryable(ErrorKind::RuntimeManagerUnavailable));
    CHECK(is_retryable(ErrorKind::InstanceNotFound));
    CHECK(is_retryable(ErrorKind::ConnectionLost));
    CHECK(is_retryable(ErrorKind::Timeout));
    CHECK(is_retryable(ErrorKind::InstanceNotRunning));
}

TEST_CASE("Caller errors are not retryable", "[error][retry]")
{
    // Retrying any of these spins forever: the program, not the environment,
    // is what needs to change.
    CHECK_FALSE(is_retryable(ErrorKind::InvalidAddress));
    CHECK_FALSE(is_retryable(ErrorKind::IndexOutOfRange));
    CHECK_FALSE(is_retryable(ErrorKind::WrongArgument));
    CHECK_FALSE(is_retryable(ErrorKind::LimitReached));
    CHECK_FALSE(is_retryable(ErrorKind::ApiNotInitialized));
    CHECK_FALSE(is_retryable(ErrorKind::Other));
}

TEST_CASE("Error carries the raw SDK code alongside the classification", "[error]")
{
    const Error error(ErrorKind::InstanceNotFound, -4, "SREC_DOES_NOT_EXIST",
                      "no instance named \"Webots\" is registered");

    CHECK(error.kind() == ErrorKind::InstanceNotFound);
    CHECK(error.code() == -4);
    CHECK(error.code_name() == "SREC_DOES_NOT_EXIST");
    CHECK(error.retryable());
    CHECK(std::string(error.what()) == "no instance named \"Webots\" is registered");
}

TEST_CASE("Error is catchable as std::exception", "[error]")
{
    bool caught = false;
    try {
        throw Error(ErrorKind::Timeout, -11, "SREC_TIMEOUT", "ReadBit failed");
    } catch (const std::exception& generic) {
        caught = true;
        CHECK(std::string(generic.what()) == "ReadBit failed");
    }
    CHECK(caught);
}

TEST_CASE("Every ErrorKind has a name", "[error]")
{
    for (const ErrorKind kind : {ErrorKind::InvalidAddress,
                                 ErrorKind::ApiNotInitialized,
                                 ErrorKind::RuntimeManagerUnavailable,
                                 ErrorKind::InstanceNotFound,
                                 ErrorKind::ConnectionLost,
                                 ErrorKind::Timeout,
                                 ErrorKind::InstanceNotRunning,
                                 ErrorKind::IndexOutOfRange,
                                 ErrorKind::WrongArgument,
                                 ErrorKind::LimitReached,
                                 ErrorKind::Other}) {
        CHECK_FALSE(plcsim::to_string(kind).empty());
    }
}
