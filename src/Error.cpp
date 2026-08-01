#include "plcsim/Error.hpp"

#include <utility>

namespace plcsim {

std::string_view to_string(ErrorKind kind) noexcept
{
    switch (kind) {
        case ErrorKind::InvalidAddress:            return "InvalidAddress";
        case ErrorKind::ApiNotInitialized:         return "ApiNotInitialized";
        case ErrorKind::RuntimeManagerUnavailable: return "RuntimeManagerUnavailable";
        case ErrorKind::InstanceNotFound:          return "InstanceNotFound";
        case ErrorKind::ConnectionLost:            return "ConnectionLost";
        case ErrorKind::Timeout:                   return "Timeout";
        case ErrorKind::InstanceNotRunning:        return "InstanceNotRunning";
        case ErrorKind::IndexOutOfRange:           return "IndexOutOfRange";
        case ErrorKind::WrongArgument:             return "WrongArgument";
        case ErrorKind::LimitReached:              return "LimitReached";
        case ErrorKind::Other:                     return "Other";
    }
    return "Other";
}

bool is_retryable(ErrorKind kind) noexcept
{
    switch (kind) {
        // Environmental: the world may change in our favour.
        case ErrorKind::RuntimeManagerUnavailable:
        case ErrorKind::InstanceNotFound:
        case ErrorKind::ConnectionLost:
        case ErrorKind::Timeout:
        case ErrorKind::InstanceNotRunning:
            return true;

        // Programming errors: retrying loops forever.
        case ErrorKind::InvalidAddress:
        case ErrorKind::IndexOutOfRange:
        case ErrorKind::WrongArgument:
        case ErrorKind::LimitReached:
            return false;

        // ApiNotInitialized means the SDK is absent or broken; a retry loop
        // would spin against a missing installation.
        case ErrorKind::ApiNotInitialized:
            return false;

        case ErrorKind::Other:
            return false;
    }
    return false;
}

Error::Error(ErrorKind kind, int code, std::string code_name, const std::string& message)
    : std::runtime_error(message)
    , kind_(kind)
    , code_(code)
    , code_name_(std::move(code_name))
{
}

}  // namespace plcsim
