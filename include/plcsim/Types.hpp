#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace plcsim {

/// PLC memory areas addressable by bit through this connector.
///
/// Mirrors the subset of Siemens' EArea that ReadBit()/WriteBit() accept.
enum class Area {
    Input,   ///< %I / %E - process image of the inputs
    Marker,  ///< %M     - bit memory
    Output,  ///< %Q / %A - process image of the outputs
};

/// Operating state of a simulated PLC instance (Siemens EOperatingState).
enum class OperatingState {
    Invalid,
    Off,
    Booting,
    Stop,
    Startup,
    Run,
    Freeze,
    ShuttingDown,
    Hold,
};

/// Identity of an instance registered in the Runtime Manager.
struct InstanceInfo {
    std::int32_t id{};
    std::string name;
};

/// Canonical single-letter mnemonic: "I", "M" or "Q".
[[nodiscard]] std::string_view to_string(Area area) noexcept;

[[nodiscard]] std::string_view to_string(OperatingState state) noexcept;

/// True when the instance can exchange I/O with the API.
///
/// Writes are only applied to the simulated process image while the instance
/// is in RUN (see the PLCSIM Advanced manual, "Simulate peripheral I/O").
[[nodiscard]] constexpr bool exchanges_io(OperatingState state) noexcept
{
    return state == OperatingState::Run;
}

}  // namespace plcsim
