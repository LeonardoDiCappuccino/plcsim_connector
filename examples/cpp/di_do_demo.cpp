// Minimal DI/DO demo.
//
//   di_do_demo <instance-name> [cycles]
//
// Toggles an input bit and samples an output bit each cycle, printing both.
// Expects an instance already registered and running in the PLCSIM Advanced
// Control Panel.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "plcsim/plcsim.hpp"

namespace {

constexpr const char* kInputBit = "%I10.2";
constexpr const char* kOutputBit = "%Q4.1";

int usage(const char* argv0)
{
    std::cerr << "usage: " << argv0 << " <instance-name> [cycles]\n";
    return 2;
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        return usage(argv[0]);
    }

    const std::string instance_name = argv[1];
    const int cycles = (argc > 2) ? std::atoi(argv[2]) : 10;

    try {
        plcsim::RuntimeManager runtime;
        std::cout << "Runtime Manager " << runtime.version() << '\n';

        auto plc = runtime.attach(instance_name);
        std::cout << "attached to \"" << plc.name() << "\" (id " << plc.id() << "), state "
                  << plcsim::to_string(plc.operatingState()) << '\n';

        if (!plcsim::exchanges_io(plc.operatingState())) {
            std::cout << "instance is not in RUN; putting it there\n";
            plc.run();
        }

        std::cout << "areas: %I " << plc.areaSize(plcsim::Area::Input) << " B, %Q "
                  << plc.areaSize(plcsim::Area::Output) << " B\n\n";

        for (int cycle = 0; cycle < cycles; ++cycle) {
            const bool input = (cycle % 2) == 0;

            plc.setAddressBit(kInputBit, input);
            const bool output = plc.getAddressBit(kOutputBit);

            std::cout << "cycle " << cycle << ": " << kInputBit << " <- "
                      << (input ? "true " : "false") << "   " << kOutputBit << " -> "
                      << (output ? "true" : "false") << '\n';

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        return 0;
    } catch (const plcsim::Error& error) {
        std::cerr << "\nPLCSIM error: " << error.what() << '\n'
                  << "  kind=" << plcsim::to_string(error.kind())
                  << " code=" << error.code_name() << " (" << error.code() << ')'
                  << " retryable=" << (error.retryable() ? "yes" : "no") << '\n';
        return 1;
    }
}
