#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace plcsim {

/// Classification of a failure, coarse enough to switch on.
///
/// Siemens' ERuntimeErrorCode has ~60 members; callers writing a reconnect loop
/// do not want to enumerate them. Every raw code is mapped onto one of these,
/// and the raw code stays available via Error::code() / Error::code_name().
enum class ErrorKind {
    /// The address string could not be parsed. Always a caller bug; never retry.
    InvalidAddress,

    /// InitializeApi() has not run, or the API DLL could not be loaded at all.
    /// Usually means S7-PLCSIM Advanced is not installed.
    ApiNotInitialized,

    /// No Runtime Manager is reachable in this Windows session - the
    /// PLCSIM Advanced Control Panel is not running, or its process died.
    /// Retryable: it may come back.
    RuntimeManagerUnavailable,

    /// The Runtime Manager is up, but no instance with that name/ID is
    /// registered. Retryable: the instance may not be started yet.
    InstanceNotFound,

    /// The connection to a previously working instance was dropped
    /// (SREC_INTERFACE_REMOVED). Retryable, but the Instance handle is dead and
    /// must be replaced by a fresh attach().
    ConnectionLost,

    /// The call did not return in time. Retryable.
    Timeout,

    /// The instance process is not running, or is not in RUN so I/O is not
    /// exchanged. Retryable.
    InstanceNotRunning,

    /// Byte offset or bit index outside the area. Caller bug; never retry.
    IndexOutOfRange,

    /// An argument was rejected by the API. Caller bug; never retry.
    WrongArgument,

    /// Too many instances registered (the Runtime Manager caps at 16).
    LimitReached,

    /// Anything not worth its own bucket. Inspect code() / code_name().
    Other,
};

[[nodiscard]] std::string_view to_string(ErrorKind kind) noexcept;

/// Whether retrying the operation could plausibly succeed later.
///
/// This is the predicate a reconnect loop should branch on: `true` means the
/// failure is environmental (nothing running yet, connection dropped, timed
/// out), `false` means the program is wrong and retrying will loop forever.
[[nodiscard]] bool is_retryable(ErrorKind kind) noexcept;

/// Every failure raised by this library.
class Error : public std::runtime_error {
public:
    Error(ErrorKind kind, int code, std::string code_name, const std::string& message);

    /// Coarse classification. Switch on this, not on code().
    [[nodiscard]] ErrorKind kind() const noexcept { return kind_; }

    /// Raw Siemens ERuntimeErrorCode value, or 0 for errors raised by this
    /// library before reaching the SDK (e.g. address parse failures).
    [[nodiscard]] int code() const noexcept { return code_; }

    /// Raw code's symbolic name, e.g. "SREC_DOES_NOT_EXIST". Empty when the
    /// error did not originate in the SDK.
    [[nodiscard]] const std::string& code_name() const noexcept { return code_name_; }

    /// Convenience for `is_retryable(kind())`.
    [[nodiscard]] bool retryable() const noexcept { return is_retryable(kind_); }

private:
    ErrorKind kind_;
    int code_;
    std::string code_name_;
};

}  // namespace plcsim
