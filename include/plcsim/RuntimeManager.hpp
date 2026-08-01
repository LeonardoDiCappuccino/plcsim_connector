#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plcsim/Instance.hpp"
#include "plcsim/Types.hpp"

namespace plcsim {

/// Connection to the S7-PLCSIM Advanced Runtime Manager.
///
/// Construction calls Siemens' InitializeApi(), which loads the API DLL and
/// starts the Runtime Manager process if it is not already up. Destruction
/// releases this process's use of it.
///
/// Multiple RuntimeManager objects are safe: they share one refcounted
/// process-wide API initialisation, because the SDK's own init/teardown state
/// is a set of file-scope statics that would otherwise be torn out from under
/// each other.
///
/// Thread-safety: the object is safe to construct, destroy and query from
/// multiple threads.
class RuntimeManager {
public:
    /// @param api_dll_dir Directory holding
    ///        Siemens.Simatic.Simulation.Runtime.Api.x64.dll. Leave empty to
    ///        let the SDK resolve it: application directory first, then the
    ///        install path recorded in the registry.
    ///
    /// Throws Error(ErrorKind::ApiNotInitialized) if the DLL cannot be loaded,
    /// or Error(ErrorKind::RuntimeManagerUnavailable) if no Runtime Manager is
    /// reachable in this Windows session.
    explicit RuntimeManager(std::optional<std::filesystem::path> api_dll_dir = std::nullopt);

    RuntimeManager(RuntimeManager&&) noexcept;
    RuntimeManager& operator=(RuntimeManager&&) noexcept;
    RuntimeManager(const RuntimeManager&) = delete;
    RuntimeManager& operator=(const RuntimeManager&) = delete;
    ~RuntimeManager();

    /// Whether the Runtime Manager process is still reachable.
    ///
    /// Goes false only when that process closes. Non-throwing, so it is usable
    /// as the cheap outer condition of a reconnect loop.
    [[nodiscard]] bool isRuntimeManagerAvailable() const noexcept;

    /// Instances currently registered, whether running or not.
    ///
    /// Useful both for discovery and for producing a decent diagnostic when an
    /// attach() fails - which is exactly what attach() itself does.
    [[nodiscard]] std::vector<InstanceInfo> registeredInstances() const;

    /// Connects to an already-registered instance by name.
    ///
    /// Throws Error(ErrorKind::InstanceNotFound) if no such instance exists;
    /// the message lists the names that *are* registered.
    [[nodiscard]] Instance attach(std::string_view name) const;

    /// Connects to an already-registered instance by ID.
    [[nodiscard]] Instance attach(std::int32_t id) const;

    /// Non-throwing attach, for polling reconnect loops.
    ///
    /// Returns std::nullopt for any retryable failure (nothing registered yet,
    /// Runtime Manager not up, timeout). Genuine caller errors - an invalid
    /// name, for instance - still throw, so a typo does not silently become an
    /// infinite retry.
    [[nodiscard]] std::optional<Instance> tryAttach(std::string_view name) const;

    /// Version of the connected Runtime Manager, as "major.minor".
    [[nodiscard]] std::string version() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace plcsim
