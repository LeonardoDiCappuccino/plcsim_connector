#include "plcsim/Types.hpp"

namespace plcsim {

std::string_view to_string(Area area) noexcept
{
    switch (area) {
        case Area::Input:  return "I";
        case Area::Marker: return "M";
        case Area::Output: return "Q";
    }
    return "?";
}

std::string_view to_string(OperatingState state) noexcept
{
    switch (state) {
        case OperatingState::Invalid:      return "Invalid";
        case OperatingState::Off:          return "Off";
        case OperatingState::Booting:      return "Booting";
        case OperatingState::Stop:         return "Stop";
        case OperatingState::Startup:      return "Startup";
        case OperatingState::Run:          return "Run";
        case OperatingState::Freeze:       return "Freeze";
        case OperatingState::ShuttingDown: return "ShuttingDown";
        case OperatingState::Hold:         return "Hold";
    }
    return "Invalid";
}

}  // namespace plcsim
