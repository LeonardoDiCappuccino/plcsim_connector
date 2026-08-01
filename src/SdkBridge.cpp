// ---------------------------------------------------------------------------
// The ONLY translation unit that includes SimulationRuntimeApi.h.
//
// This is not a style preference. The Siemens header declares
//
//     static ApiEntry_DestroyInterface _DestroyInterface = NULL;
//     static HMODULE _SimulationRuntimeApiDllHandle = NULL;
//
// at file scope, so every translation unit that includes it gets its *own*
// copy. InitializeApi() in one TU leaves DestroyInterface() in another TU with
// a null function pointer, which fails at runtime with SREC_API_NOT_INITIALIZED
// and no useful diagnostic. Keeping the header confined to one TU makes that
// class of bug impossible.
//
// The pimpl bodies for RuntimeManager and Instance therefore both live here.
// ---------------------------------------------------------------------------

#include <SimulationRuntimeApi.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "plcsim/Address.hpp"
#include "plcsim/Error.hpp"
#include "plcsim/Instance.hpp"
#include "plcsim/RuntimeManager.hpp"
#include "plcsim/Types.hpp"

namespace plcsim {
namespace {

// -- String conversion ------------------------------------------------------

std::string to_utf8(const WCHAR* wide)
{
    if (wide == nullptr || *wide == L'\0') {
        return {};
    }

    const int needed =
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }

    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

std::vector<WCHAR> to_wide(std::string_view utf8)
{
    if (utf8.empty()) {
        return std::vector<WCHAR>{L'\0'};
    }

    const int needed = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return std::vector<WCHAR>{L'\0'};
    }

    std::vector<WCHAR> out(static_cast<std::size_t>(needed) + 1, L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

// -- Error mapping ----------------------------------------------------------

ErrorKind classify(ERuntimeErrorCode code) noexcept
{
    switch (code) {
        case SREC_DOES_NOT_EXIST:
            return ErrorKind::InstanceNotFound;

        case SREC_INTERFACE_REMOVED:
            return ErrorKind::ConnectionLost;

        case SREC_TIMEOUT:
            return ErrorKind::Timeout;

        case SREC_INSTANCE_NOT_RUNNING:
        case SREC_NOT_RUNNING:
            return ErrorKind::InstanceNotRunning;

        case SREC_INDEX_OUT_OF_RANGE:
            return ErrorKind::IndexOutOfRange;

        case SREC_WRONG_ARGUMENT:
            return ErrorKind::WrongArgument;

        case SREC_LIMIT_REACHED:
            return ErrorKind::LimitReached;

        case SREC_RUNTIME_NOT_AVAILABLE:
        case SREC_CONNECTION_ERROR:
            return ErrorKind::RuntimeManagerUnavailable;

        case SREC_API_NOT_INITIALIZED:
        case SREC_ERROR_LOADING_DLL:
        case SREC_WRONG_VERSION:
            return ErrorKind::ApiNotInitialized;

        default:
            return ErrorKind::Other;
    }
}

std::string code_name_of(ERuntimeErrorCode code)
{
    return to_utf8(::GetNameOfErrorCode(code));
}

/// Siemens convention: 0 is success, positive values are warnings, negative
/// values are errors. Warnings (trial licence active, for instance) must not
/// abort an otherwise working call.
[[nodiscard]] bool succeeded(ERuntimeErrorCode code) noexcept
{
    return static_cast<int>(code) >= 0;
}

[[noreturn]] void throw_error(ERuntimeErrorCode code, const std::string& context,
                               const std::string& extra = {})
{
    std::string name = code_name_of(code);
    std::string message = context;
    message += " failed: ";
    message += name.empty() ? "unknown error" : name;
    message += " (";
    message += std::to_string(static_cast<int>(code));
    message += ')';
    if (!extra.empty()) {
        message += " [";
        message += extra;
        message += ']';
    }

    throw Error(classify(code), static_cast<int>(code), std::move(name), message);
}

// -- Win32 diagnostics for InitializeApi() failures --------------------------
//
// InitializeApi() is one opaque call into the Siemens DLL; there is nothing
// of *ours* to log around it. The only genuinely new information available is
// what Win32 already tracks and the SDK doesn't surface: GetLastError() right
// after the call (set by LoadLibraryW/GetProcAddress if that's where it
// actually failed), and an independent LoadLibraryW probe of the resolved DLL
// path, which separates "the DLL itself won't load" (missing dependency,
// architecture mismatch) from "it loads fine but the handshake with the
// Runtime Manager fails" (licensing, permissions, a version skew the header
// doesn't classify).

constexpr const wchar_t* kApiDllName = L"Siemens.Simatic.Simulation.Runtime.Api.x64.dll";

std::string win32_error_message(DWORD code)
{
    if (code == 0) {
        return "0 (no error)";
    }

    LPWSTR buffer = nullptr;
    const DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::string text;
    if (len > 0 && buffer != nullptr) {
        text = to_utf8(buffer);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
    }
    if (buffer != nullptr) {
        ::LocalFree(buffer);
    }

    return std::to_string(code) + (text.empty() ? " (unknown error)" : " (" + text + ")");
}

std::string probe_dll_load(const std::optional<std::filesystem::path>& dll_dir)
{
    if (!dll_dir.has_value()) {
        return "api_dll_dir not given, probe skipped (InitializeApi() searches the app "
               "folder, then the registry-reported install directory)";
    }

    const std::filesystem::path dll_path = *dll_dir / kApiDllName;

    ::SetLastError(0);
    HMODULE handle = ::LoadLibraryW(dll_path.c_str());
    if (handle != nullptr) {
        ::FreeLibrary(handle);
        return "LoadLibraryW succeeded on " + dll_path.string() +
               " (the DLL itself loads fine; the failure is inside InitializeApi()'s own "
               "handshake with the Runtime Manager)";
    }

    const DWORD error = ::GetLastError();
    return "LoadLibraryW(\"" + dll_path.string() + "\") failed: " + win32_error_message(error);
}

void check(ERuntimeErrorCode code, const std::string& context)
{
    if (!succeeded(code)) {
        throw_error(code, context);
    }
}

// -- Enum mapping -----------------------------------------------------------

EArea to_sdk(Area area) noexcept
{
    switch (area) {
        case Area::Input:  return SRA_INPUT;
        case Area::Marker: return SRA_MARKER;
        case Area::Output: return SRA_OUTPUT;
    }
    return SRA_INVALID_AREA;
}

OperatingState from_sdk(EOperatingState state) noexcept
{
    switch (state) {
        case SROS_OFF:           return OperatingState::Off;
        case SROS_BOOTING:       return OperatingState::Booting;
        case SROS_STOP:          return OperatingState::Stop;
        case SROS_STARTUP:       return OperatingState::Startup;
        case SROS_RUN:           return OperatingState::Run;
        case SROS_FREEZE:        return OperatingState::Freeze;
        case SROS_SHUTTING_DOWN: return OperatingState::ShuttingDown;
        case SROS_HOLD:          return OperatingState::Hold;
        default:                 return OperatingState::Invalid;
    }
}

// -- Process-wide API lifetime ----------------------------------------------
//
// InitializeApi()/ShutdownAndFreeApi() mutate the file-scope statics above, so
// they are effectively process-global. Two independent RuntimeManager objects
// tearing down at different times would FreeLibrary the DLL out from under each
// other. A mutex-guarded refcount serialises init and teardown so that cannot
// happen; ApiHandle is the RAII front end.

std::mutex g_api_mutex;
int g_api_refcount = 0;
ISimulationRuntimeManager* g_api_manager = nullptr;

class ApiHandle {
public:
    ApiHandle() noexcept = default;

    static ApiHandle acquire(const std::optional<std::filesystem::path>& dll_dir)
    {
        std::lock_guard<std::mutex> lock(g_api_mutex);

        if (g_api_refcount == 0) {
            ISimulationRuntimeManager* manager = nullptr;
            ERuntimeErrorCode result = SREC_INVALID_ERROR_CODE;

            ::SetLastError(0);
            if (dll_dir.has_value()) {
                std::vector<WCHAR> path = to_wide(dll_dir->string());
                result = ::InitializeApi(path.data(), &manager);
            } else {
                result = ::InitializeApi(&manager);
            }
            const DWORD last_error = ::GetLastError();

            if (!succeeded(result) || manager == nullptr) {
                std::string diagnostics =
                    "GetLastError() after InitializeApi(): " + win32_error_message(last_error);
                diagnostics += "; direct LoadLibraryW probe: " + probe_dll_load(dll_dir);
                throw_error(result, "InitializeApi", diagnostics);
            }
            g_api_manager = manager;
        }

        ++g_api_refcount;
        return ApiHandle{true};
    }

    ApiHandle(const ApiHandle& other) noexcept : owns_(other.owns_)
    {
        if (owns_) {
            std::lock_guard<std::mutex> lock(g_api_mutex);
            ++g_api_refcount;
        }
    }

    ApiHandle(ApiHandle&& other) noexcept : owns_(other.owns_) { other.owns_ = false; }

    ApiHandle& operator=(ApiHandle other) noexcept
    {
        std::swap(owns_, other.owns_);
        return *this;
    }

    ~ApiHandle() { release(); }

    void release() noexcept
    {
        if (!owns_) {
            return;
        }
        owns_ = false;

        std::lock_guard<std::mutex> lock(g_api_mutex);
        if (--g_api_refcount == 0 && g_api_manager != nullptr) {
            // Shutdown() ends *this process's* conversation with the Runtime
            // Manager. It does not stop anyone's simulation: the Runtime
            // Manager process only exits once no application holds the API.
            ::ShutdownAndFreeApi(g_api_manager);
            g_api_manager = nullptr;
        }
    }

    [[nodiscard]] ISimulationRuntimeManager* manager() const noexcept
    {
        return owns_ ? g_api_manager : nullptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return owns_; }

private:
    explicit ApiHandle(bool owns) noexcept : owns_(owns) {}

    bool owns_ = false;
};

}  // namespace

// ===========================================================================
// Instance
// ===========================================================================

struct Instance::Impl {
    ApiHandle api;
    IInstance* raw = nullptr;
    std::int32_t id = 0;
    std::string name;

    /// Sticky: once the Runtime Manager reports the interface gone, this handle
    /// never recovers. isConnected() reports it without throwing so callers can
    /// decide to re-attach.
    mutable bool interface_removed = false;

    /// Indexed by Area. Area sizes are fixed for a given downloaded
    /// configuration, so one query each is enough.
    mutable std::array<std::optional<std::uint32_t>, 3> area_sizes{};

    ~Impl() { destroy(); }

    void destroy() noexcept
    {
        if (raw != nullptr) {
            ::DestroyInterface(raw);
            raw = nullptr;
        }
        api.release();
    }

    [[nodiscard]] IInstance* live(const char* operation) const
    {
        if (raw == nullptr) {
            throw Error(ErrorKind::ConnectionLost, static_cast<int>(SREC_INTERFACE_REMOVED),
                        code_name_of(SREC_INTERFACE_REMOVED),
                        std::string(operation) + " failed: the instance handle is closed");
        }
        return raw;
    }

    /// Runs `code` through check(), tagging the interface dead if the Runtime
    /// Manager says it went away.
    void verify(ERuntimeErrorCode code, const std::string& context) const
    {
        if (code == SREC_INTERFACE_REMOVED) {
            interface_removed = true;
        }
        check(code, context);
    }

    [[nodiscard]] std::uint32_t area_size(Area area) const
    {
        auto& cached = area_sizes[static_cast<std::size_t>(area)];
        if (!cached.has_value()) {
            const std::uint32_t size = live("GetAreaSize")->GetAreaSize(to_sdk(area));
            if (size == 0) {
                // Documented failure signal; the SDK returns no error code here.
                throw Error(ErrorKind::InstanceNotRunning, 0, {},
                            "GetAreaSize failed for area %" + std::string(to_string(area)) +
                                ": the instance is not running, or no configuration is loaded");
            }
            cached = size;
        }
        return *cached;
    }

    /// Local bounds check, so an out-of-range address reports the actual area
    /// size instead of a bare SREC_INDEX_OUT_OF_RANGE from a round trip.
    void check_bounds(const BitAddress& address, const char* operation) const
    {
        const std::uint32_t size = area_size(address.area);
        if (address.byte >= size) {
            throw Error(ErrorKind::IndexOutOfRange, static_cast<int>(SREC_INDEX_OUT_OF_RANGE),
                        code_name_of(SREC_INDEX_OUT_OF_RANGE),
                        std::string(operation) + " failed: byte offset " +
                            std::to_string(address.byte) + " is outside area %" +
                            std::string(to_string(address.area)) + ", which is " +
                            std::to_string(size) + " bytes (valid offsets 0.." +
                            std::to_string(size - 1) + ")");
        }
    }
};

Instance::Instance(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Instance::Instance(Instance&&) noexcept = default;
Instance& Instance::operator=(Instance&&) noexcept = default;
Instance::~Instance() = default;

void Instance::setAddressBit(std::string_view address, bool value)
{
    const BitAddress parsed = parse_bit_address(address);
    impl_->check_bounds(parsed, "setAddressBit");
    writeBit(parsed.area, parsed.byte, parsed.bit, value);
}

bool Instance::getAddressBit(std::string_view address) const
{
    const BitAddress parsed = parse_bit_address(address);
    impl_->check_bounds(parsed, "getAddressBit");
    return readBit(parsed.area, parsed.byte, parsed.bit);
}

void Instance::writeBit(Area area, std::uint32_t byte, std::uint8_t bit, bool value)
{
    const ERuntimeErrorCode result =
        impl_->live("WriteBit")->WriteBit(to_sdk(area), byte, bit, value);
    impl_->verify(result, "WriteBit");
}

bool Instance::readBit(Area area, std::uint32_t byte, std::uint8_t bit) const
{
    bool value = false;
    const ERuntimeErrorCode result =
        impl_->live("ReadBit")->ReadBit(to_sdk(area), byte, bit, &value);
    impl_->verify(result, "ReadBit");
    return value;
}

std::uint32_t Instance::areaSize(Area area) const
{
    return impl_->area_size(area);
}

void Instance::run()
{
    impl_->verify(impl_->live("Run")->Run(), "Run");
}

void Instance::stop()
{
    impl_->verify(impl_->live("Stop")->Stop(), "Stop");
}

OperatingState Instance::operatingState() const
{
    return from_sdk(impl_->live("GetOperatingState")->GetOperatingState());
}

bool Instance::isConnected() const noexcept
{
    if (impl_ == nullptr || impl_->raw == nullptr || impl_->interface_removed) {
        return false;
    }

    ISimulationRuntimeManager* manager = impl_->api.manager();
    return manager != nullptr && manager->IsRuntimeManagerAvailable();
}

const std::string& Instance::name() const noexcept
{
    return impl_->name;
}

std::int32_t Instance::id() const noexcept
{
    return impl_->id;
}

void Instance::close() noexcept
{
    if (impl_ != nullptr) {
        impl_->destroy();
    }
}

// ===========================================================================
// RuntimeManager
// ===========================================================================

struct RuntimeManager::Impl {
    ApiHandle api;

    [[nodiscard]] ISimulationRuntimeManager* manager(const char* operation) const
    {
        ISimulationRuntimeManager* m = api.manager();
        if (m == nullptr) {
            throw Error(ErrorKind::ApiNotInitialized, static_cast<int>(SREC_API_NOT_INITIALIZED),
                        code_name_of(SREC_API_NOT_INITIALIZED),
                        std::string(operation) + " failed: the API is not initialized");
        }
        return m;
    }
};

RuntimeManager::RuntimeManager(std::optional<std::filesystem::path> api_dll_dir)
    : impl_(std::make_unique<Impl>())
{
    impl_->api = ApiHandle::acquire(api_dll_dir);

    if (!impl_->api.manager()->IsRuntimeManagerAvailable()) {
        throw Error(ErrorKind::RuntimeManagerUnavailable,
                    static_cast<int>(SREC_RUNTIME_NOT_AVAILABLE),
                    code_name_of(SREC_RUNTIME_NOT_AVAILABLE),
                    "no S7-PLCSIM Advanced Runtime Manager is running in this Windows session; "
                    "start the PLCSIM Advanced Control Panel");
    }
}

RuntimeManager::RuntimeManager(RuntimeManager&&) noexcept = default;
RuntimeManager& RuntimeManager::operator=(RuntimeManager&&) noexcept = default;
RuntimeManager::~RuntimeManager() = default;

bool RuntimeManager::isRuntimeManagerAvailable() const noexcept
{
    if (impl_ == nullptr) {
        return false;
    }
    ISimulationRuntimeManager* manager = impl_->api.manager();
    return manager != nullptr && manager->IsRuntimeManagerAvailable();
}

std::vector<InstanceInfo> RuntimeManager::registeredInstances() const
{
    ISimulationRuntimeManager* manager = impl_->manager("GetRegisteredInstancesCount");

    const UINT32 count = manager->GetRegisteredInstancesCount();

    std::vector<InstanceInfo> infos;
    infos.reserve(count);

    for (UINT32 index = 0; index < count; ++index) {
        SInstanceInfo info{};
        const ERuntimeErrorCode result = manager->GetRegisteredInstanceInfoAt(index, &info);

        // The set can shrink between the count and the query; a stale index is
        // not an error worth propagating.
        if (result == SREC_INDEX_OUT_OF_RANGE) {
            break;
        }
        check(result, "GetRegisteredInstanceInfoAt");

        infos.push_back(InstanceInfo{info.ID, to_utf8(info.Name)});
    }

    return infos;
}

namespace {

/// "found 'A', 'B'" / "none are registered" - the tail of a not-found message.
std::string describe_registered(const std::vector<InstanceInfo>& infos)
{
    if (infos.empty()) {
        return "no instances are registered in the Runtime Manager";
    }

    std::string out = "registered instances: ";
    for (std::size_t i = 0; i < infos.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += '"';
        out += infos[i].name;
        out += "\" (id ";
        out += std::to_string(infos[i].id);
        out += ')';
    }
    return out;
}

}  // namespace

Instance RuntimeManager::attach(std::string_view name) const
{
    ISimulationRuntimeManager* manager = impl_->manager("CreateInterface");

    std::vector<WCHAR> wide_name = to_wide(name);
    IInstance* raw = nullptr;
    const ERuntimeErrorCode result = manager->CreateInterface(wide_name.data(), &raw);

    if (result == SREC_DOES_NOT_EXIST || (succeeded(result) && raw == nullptr)) {
        // The common failure by far, and the one a reconnect loop hits while
        // waiting for the Control Panel. Make it self-explanatory.
        std::string message = "no simulated PLC instance named \"";
        message.append(name);
        message += "\" is registered; ";
        message += describe_registered(registeredInstances());

        throw Error(ErrorKind::InstanceNotFound, static_cast<int>(SREC_DOES_NOT_EXIST),
                    code_name_of(SREC_DOES_NOT_EXIST), message);
    }

    check(result, "CreateInterface");

    auto impl = std::make_unique<Instance::Impl>();
    impl->api = impl_->api;
    impl->raw = raw;
    impl->id = raw->GetID();
    impl->name = std::string(name);

    return Instance(std::move(impl));
}

Instance RuntimeManager::attach(std::int32_t id) const
{
    ISimulationRuntimeManager* manager = impl_->manager("CreateInterface");

    IInstance* raw = nullptr;
    const ERuntimeErrorCode result = manager->CreateInterface(id, &raw);

    if (result == SREC_DOES_NOT_EXIST || (succeeded(result) && raw == nullptr)) {
        std::string message = "no simulated PLC instance with id ";
        message += std::to_string(id);
        message += " is registered; ";
        message += describe_registered(registeredInstances());

        throw Error(ErrorKind::InstanceNotFound, static_cast<int>(SREC_DOES_NOT_EXIST),
                    code_name_of(SREC_DOES_NOT_EXIST), message);
    }

    check(result, "CreateInterface");

    auto impl = std::make_unique<Instance::Impl>();
    impl->api = impl_->api;
    impl->raw = raw;
    impl->id = id;

    std::array<WCHAR, DINSTANCE_NAME_MAX_LENGTH> name_buffer{};
    if (succeeded(raw->GetName(name_buffer.data(),
                               static_cast<UINT32>(name_buffer.size())))) {
        impl->name = to_utf8(name_buffer.data());
    }

    return Instance(std::move(impl));
}

std::optional<Instance> RuntimeManager::tryAttach(std::string_view name) const
{
    try {
        return attach(name);
    } catch (const Error& error) {
        if (error.retryable()) {
            return std::nullopt;
        }
        throw;  // a typo'd name must not become a silent infinite retry
    }
}

std::string RuntimeManager::version() const
{
    const UINT32 packed = impl_->manager("GetVersion")->GetVersion();
    return std::to_string(packed >> 16) + "." + std::to_string(packed & 0xFFFFu);
}

}  // namespace plcsim
