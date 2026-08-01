// Live tests. These need S7-PLCSIM Advanced installed, the Control Panel
// running, and an instance to attach to. They are excluded from the default
// CTest run; enable with -DPLCSIM_LIVE_INSTANCE=<name>.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "plcsim/plcsim.hpp"

namespace {
std::string g_instance_name;
}

int main(int argc, char* argv[])
{
    Catch::Session session;

    using namespace Catch::Clara;
    session.cli(session.cli() | Opt(g_instance_name, "name")["--instance"](
                                   "name of a registered PLCSIM Advanced instance"));

    if (const int code = session.applyCommandLine(argc, argv); code != 0) {
        return code;
    }

    return session.run();
}

TEST_CASE("Runtime Manager is reachable", "[live]")
{
    plcsim::RuntimeManager runtime;
    CHECK(runtime.isRuntimeManagerAvailable());
    INFO("Runtime Manager version " << runtime.version());
    CHECK_FALSE(runtime.version().empty());
}

TEST_CASE("Attaching to a missing instance reports what is registered", "[live]")
{
    plcsim::RuntimeManager runtime;

    bool threw = false;
    try {
        (void)runtime.attach("definitely-not-a-real-instance");
    } catch (const plcsim::Error& error) {
        threw = true;
        CHECK(error.kind() == plcsim::ErrorKind::InstanceNotFound);
        CHECK(error.retryable());
        INFO(error.what());
        CHECK(std::string(error.what()).find("registered") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("tryAttach returns nullopt instead of throwing", "[live]")
{
    plcsim::RuntimeManager runtime;
    CHECK_FALSE(runtime.tryAttach("definitely-not-a-real-instance").has_value());
}

TEST_CASE("Digital I/O round trip", "[live]")
{
    if (g_instance_name.empty()) {
        SKIP("pass --instance <name> to run the I/O tests");
    }

    plcsim::RuntimeManager runtime;
    auto plc = runtime.attach(g_instance_name);

    REQUIRE(plc.isConnected());
    INFO("instance \"" << plc.name() << "\" id " << plc.id() << " state "
                       << plcsim::to_string(plc.operatingState()));

    if (!plcsim::exchanges_io(plc.operatingState())) {
        SKIP("instance is not in RUN; I/O is not exchanged");
    }

    CHECK(plc.areaSize(plcsim::Area::Input) > 0);
    CHECK(plc.areaSize(plcsim::Area::Output) > 0);

    SECTION("markers round-trip without disturbing the user program")
    {
        // Write to the very last bit of the marker area. %M is already the
        // safe area - unlike %I/%Q it drives no simulated device - and the
        // top of a 16 KB marker area is somewhere no real STEP 7 program
        // allocates. The original value is restored either way.
        const std::uint32_t marker_bytes = plc.areaSize(plcsim::Area::Marker);
        REQUIRE(marker_bytes > 0);

        const std::string address = "%M" + std::to_string(marker_bytes - 1) + ".7";
        INFO("scratch bit: " << address);

        const bool original = plc.getAddressBit(address);

        plc.setAddressBit(address, !original);
        CHECK(plc.getAddressBit(address) == !original);

        plc.setAddressBit(address, original);
        CHECK(plc.getAddressBit(address) == original);
    }

    SECTION("out-of-range offsets name the actual area size")
    {
        const std::uint32_t size = plc.areaSize(plcsim::Area::Input);
        const std::string too_far = "%I" + std::to_string(size) + ".0";

        bool threw = false;
        try {
            (void)plc.getAddressBit(too_far);
        } catch (const plcsim::Error& error) {
            threw = true;
            CHECK(error.kind() == plcsim::ErrorKind::IndexOutOfRange);
            CHECK_FALSE(error.retryable());
            INFO(error.what());
            CHECK(std::string(error.what()).find(std::to_string(size)) != std::string::npos);
        }
        CHECK(threw);
    }
}

TEST_CASE("A closed handle reports itself disconnected", "[live]")
{
    if (g_instance_name.empty()) {
        SKIP("pass --instance <name> to run the I/O tests");
    }

    plcsim::RuntimeManager runtime;
    auto plc = runtime.attach(g_instance_name);
    REQUIRE(plc.isConnected());

    plc.close();

    CHECK_FALSE(plc.isConnected());
    CHECK_THROWS_AS((void)plc.getAddressBit("%M0.0"), plcsim::Error);
}
