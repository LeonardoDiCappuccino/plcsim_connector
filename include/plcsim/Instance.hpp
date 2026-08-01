#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "plcsim/Types.hpp"

namespace plcsim {

class RuntimeManager;

/// A connection to one simulated PLC instance.
///
/// Obtained from RuntimeManager::attach(). This connector never creates,
/// powers on or powers off instances - that is the PLCSIM Advanced Control
/// Panel's job. An Instance is a *client* of something that already exists.
///
/// Move-only. Destroying the last Instance for a given PLC releases this
/// process's interface to it; it does not stop the simulation, as the Control
/// Panel holds its own reference.
///
/// Thread-safety: distinct Instance objects are independent. A single Instance
/// must not be used concurrently from multiple threads without external
/// synchronisation.
class Instance {
public:
    Instance(Instance&&) noexcept;
    Instance& operator=(Instance&&) noexcept;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    ~Instance();

    // -- Digital I/O by address ---------------------------------------------
    //
    // Each call is one round trip to the instance process. For a Webots
    // controller stepping at 32 ms this is comfortable up to roughly 50 bits
    // per step; beyond that, batching whole areas would be the next step.

    /// Writes one bit, e.g. setAddressBit("%I10.2", true) to assert a DI.
    ///
    /// Writing %I is how a simulated input device drives the PLC. Writing %Q is
    /// permitted and overrides the user program (Siemens' documented
    /// precedence), which is rarely what you want.
    ///
    /// The instance must be in RUN for the write to reach the process image.
    void setAddressBit(std::string_view address, bool value);

    /// Reads one bit, e.g. getAddressBit("%Q4.1") to sample a DO.
    ///
    /// Note the asymmetry documented by Siemens: reading %I returns what the
    /// API last wrote there, not what STEP 7 or a communication partner wrote.
    [[nodiscard]] bool getAddressBit(std::string_view address) const;

    /// Pre-parsed variants, for hot loops that resolve addresses once.
    void writeBit(Area area, std::uint32_t byte, std::uint8_t bit, bool value);
    [[nodiscard]] bool readBit(Area area, std::uint32_t byte, std::uint8_t bit) const;

    /// Size of an area in bytes. Cached after the first successful call.
    [[nodiscard]] std::uint32_t areaSize(Area area) const;

    // -- State ---------------------------------------------------------------

    /// Puts the instance into RUN. Required before I/O is exchanged.
    void run();

    /// Puts the instance into STOP.
    void stop();

    [[nodiscard]] OperatingState operatingState() const;

    /// Non-throwing liveness probe, for reconnect loops.
    ///
    /// Returns false once the interface has been dropped or the Runtime Manager
    /// has gone away. A false result means this handle is dead for good: get a
    /// new one from RuntimeManager::attach().
    [[nodiscard]] bool isConnected() const noexcept;

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] std::int32_t id() const noexcept;

    /// Releases the interface early. Idempotent; the destructor calls it.
    /// Every subsequent operation throws ErrorKind::ConnectionLost.
    void close() noexcept;

private:
    friend class RuntimeManager;

    struct Impl;
    explicit Instance(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

}  // namespace plcsim
