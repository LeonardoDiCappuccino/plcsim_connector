#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <optional>
#include <string>

#include "plcsim/plcsim.hpp"

namespace py = pybind11;
using namespace plcsim;

namespace {

/// Python exception objects, one per retry-relevant ErrorKind.
///
/// A reconnect loop should be able to write `except plcsim.ConnectionLost:`
/// rather than inspecting an error code, so the classification is reflected
/// into the type hierarchy:
///
///     PlcSimError
///     +-- InvalidAddressError      (never retryable)
///     +-- IndexOutOfRangeError     (never retryable)
///     +-- ApiNotInitializedError   (never retryable)
///     +-- RetryableError           (`except RetryableError` catches them all)
///         +-- RuntimeManagerUnavailableError
///         +-- InstanceNotFoundError
///         +-- ConnectionLostError
///         +-- TimeoutError
///         +-- InstanceNotRunningError
struct ExceptionTypes {
    py::handle base;
    py::handle retryable;
    py::handle invalid_address;
    py::handle index_out_of_range;
    py::handle api_not_initialized;
    py::handle runtime_manager_unavailable;
    py::handle instance_not_found;
    py::handle connection_lost;
    py::handle timeout;
    py::handle instance_not_running;

    [[nodiscard]] py::handle for_kind(ErrorKind kind) const
    {
        switch (kind) {
            case ErrorKind::InvalidAddress:
                return invalid_address;
            case ErrorKind::IndexOutOfRange:
                return index_out_of_range;
            case ErrorKind::ApiNotInitialized:
                return api_not_initialized;
            case ErrorKind::RuntimeManagerUnavailable:
                return runtime_manager_unavailable;
            case ErrorKind::InstanceNotFound:
                return instance_not_found;
            case ErrorKind::ConnectionLost:
                return connection_lost;
            case ErrorKind::Timeout:
                return timeout;
            case ErrorKind::InstanceNotRunning:
                return instance_not_running;
            case ErrorKind::WrongArgument:
            case ErrorKind::LimitReached:
            case ErrorKind::Other:
                break;
        }
        return base;
    }
};

ExceptionTypes g_exceptions;

void raise_as_python(const Error& error)
{
    py::gil_scoped_acquire gil;

    py::object exception_type =
        py::reinterpret_borrow<py::object>(g_exceptions.for_kind(error.kind()));

    py::object instance = exception_type(error.what());
    instance.attr("code") = error.code();
    instance.attr("code_name") = error.code_name();
    instance.attr("kind") = std::string(to_string(error.kind()));
    instance.attr("retryable") = error.retryable();

    PyErr_SetObject(exception_type.ptr(), instance.ptr());
}

}  // namespace

PYBIND11_MODULE(_core, m)
{
    m.doc() = "Native bindings for the S7-PLCSIM Advanced digital I/O connector.";
    m.attr("__version__") = PLCSIM_CONNECTOR_VERSION;
    // The API subdirectory ("8.0", ...) this extension was built against, so
    // the pure-Python layer can auto-locate the matching DLL at runtime
    // instead of relying solely on InitializeApi()'s own search order.
    m.attr("__api_version__") = PLCSIM_API_VERSION;

    // -- Exceptions ---------------------------------------------------------
    g_exceptions.base = py::register_exception<Error>(m, "PlcSimError").release();

    auto derived = [&](const char* name, py::handle parent) {
        return py::exception<void>(m, name, parent).release();
    };

    g_exceptions.retryable = derived("RetryableError", g_exceptions.base);
    g_exceptions.invalid_address = derived("InvalidAddressError", g_exceptions.base);
    g_exceptions.index_out_of_range = derived("IndexOutOfRangeError", g_exceptions.base);
    g_exceptions.api_not_initialized = derived("ApiNotInitializedError", g_exceptions.base);

    g_exceptions.runtime_manager_unavailable =
        derived("RuntimeManagerUnavailableError", g_exceptions.retryable);
    g_exceptions.instance_not_found = derived("InstanceNotFoundError", g_exceptions.retryable);
    g_exceptions.connection_lost = derived("ConnectionLostError", g_exceptions.retryable);
    g_exceptions.timeout = derived("TimeoutError", g_exceptions.retryable);
    g_exceptions.instance_not_running = derived("InstanceNotRunningError", g_exceptions.retryable);

    py::register_exception_translator([](std::exception_ptr p) {
        try {
            if (p) {
                std::rethrow_exception(p);
            }
        } catch (const Error& error) {
            raise_as_python(error);
        }
    });

    // -- Enums --------------------------------------------------------------
    py::enum_<Area>(m, "Area", "PLC memory area addressable by bit.")
        .value("Input", Area::Input, "%I / %E - process image of the inputs")
        .value("Marker", Area::Marker, "%M - bit memory")
        .value("Output", Area::Output, "%Q / %A - process image of the outputs");

    py::enum_<OperatingState>(m, "OperatingState", "Operating state of a simulated PLC.")
        .value("Invalid", OperatingState::Invalid)
        .value("Off", OperatingState::Off)
        .value("Booting", OperatingState::Booting)
        .value("Stop", OperatingState::Stop)
        .value("Startup", OperatingState::Startup)
        .value("Run", OperatingState::Run)
        .value("Freeze", OperatingState::Freeze)
        .value("ShuttingDown", OperatingState::ShuttingDown)
        .value("Hold", OperatingState::Hold);

    m.def("exchanges_io", &exchanges_io, py::arg("state"),
          "True when the instance is in a state that exchanges I/O with the API.");

    // -- InstanceInfo -------------------------------------------------------
    py::class_<InstanceInfo>(m, "InstanceInfo", "Identity of a registered instance.")
        .def_readonly("id", &InstanceInfo::id)
        .def_readonly("name", &InstanceInfo::name)
        .def("__repr__", [](const InstanceInfo& info) {
            return "InstanceInfo(id=" + std::to_string(info.id) + ", name='" + info.name + "')";
        });

    // -- BitAddress ---------------------------------------------------------
    py::class_<BitAddress>(m, "BitAddress", "A parsed single-bit PLC address.")
        .def_readonly("area", &BitAddress::area)
        .def_readonly("byte", &BitAddress::byte)
        .def_readonly("bit", &BitAddress::bit)
        .def("__str__", &BitAddress::to_string)
        .def(
            "__eq__", [](const BitAddress& a, const BitAddress& b) { return a == b; },
            py::is_operator())
        .def("__repr__", [](const BitAddress& a) { return "BitAddress('" + a.to_string() + "')"; });

    m.def("parse_bit_address", &parse_bit_address, py::arg("address"),
          "Parse a bit address such as '%I10.2'. Raises InvalidAddressError.");

    // -- Instance -----------------------------------------------------------
    //
    // Every SDK call is an inter-process round trip, so the GIL is released
    // around all of them. Without this a Webots controller polling I/O would
    // stall every other Python thread in the process.
    py::class_<Instance>(m, "Instance", R"doc(
A connection to one simulated PLC instance.

Obtained from :meth:`RuntimeManager.attach`. This connector never powers
instances on or off - that is the PLCSIM Advanced Control Panel's job.
)doc")
        .def("setAddressBit", &Instance::setAddressBit, py::arg("address"), py::arg("value"),
             py::call_guard<py::gil_scoped_release>(),
             "Write one bit, e.g. setAddressBit('%I10.2', True).")
        .def("getAddressBit", &Instance::getAddressBit, py::arg("address"),
             py::call_guard<py::gil_scoped_release>(),
             "Read one bit, e.g. getAddressBit('%Q4.1') -> bool.")

        // PEP 8 aliases; identical behaviour.
        .def("set_address_bit", &Instance::setAddressBit, py::arg("address"), py::arg("value"),
             py::call_guard<py::gil_scoped_release>(), "Alias of setAddressBit().")
        .def("get_address_bit", &Instance::getAddressBit, py::arg("address"),
             py::call_guard<py::gil_scoped_release>(), "Alias of getAddressBit().")

        .def("write_bit", &Instance::writeBit, py::arg("area"), py::arg("byte"), py::arg("bit"),
             py::arg("value"), py::call_guard<py::gil_scoped_release>(),
             "Pre-parsed write, for hot loops.")
        .def("read_bit", &Instance::readBit, py::arg("area"), py::arg("byte"), py::arg("bit"),
             py::call_guard<py::gil_scoped_release>(), "Pre-parsed read, for hot loops.")

        .def("area_size", &Instance::areaSize, py::arg("area"),
             py::call_guard<py::gil_scoped_release>(), "Size of an area in bytes.")

        .def("run", &Instance::run, py::call_guard<py::gil_scoped_release>(),
             "Put the instance into RUN. Required before I/O is exchanged.")
        .def("stop", &Instance::stop, py::call_guard<py::gil_scoped_release>(),
             "Put the instance into STOP.")

        .def_property_readonly("operating_state", &Instance::operatingState)
        .def_property_readonly("name", &Instance::name)
        .def_property_readonly("id", &Instance::id)

        .def("is_connected", &Instance::isConnected,
             "Non-throwing liveness probe. False means this handle is dead for "
             "good; get a new one from RuntimeManager.attach().")
        .def("close", &Instance::close, "Release the interface. Idempotent.")

        .def("__enter__", [](Instance& self) -> Instance& { return self; })
        .def("__exit__",
             [](Instance& self, py::object, py::object, py::object) {
                 self.close();
                 return false;
             })
        .def("__repr__", [](const Instance& self) {
            return "<Instance name='" + self.name() + "' id=" + std::to_string(self.id()) + ">";
        });

    // -- RuntimeManager -----------------------------------------------------
    py::class_<RuntimeManager>(m, "RuntimeManager", R"doc(
Connection to the S7-PLCSIM Advanced Runtime Manager.

Construction loads the Siemens API DLL. Several RuntimeManager objects can
coexist safely; they share one refcounted process-wide initialisation.
)doc")
        .def(py::init([](std::optional<std::filesystem::path> api_dll_dir) {
                 py::gil_scoped_release release;
                 return std::make_unique<RuntimeManager>(std::move(api_dll_dir));
             }),
             py::arg("api_dll_dir") = py::none(),
             "Connect to the Runtime Manager. Pass api_dll_dir only to force a "
             "specific copy of the Siemens API DLL.")

        .def("is_runtime_manager_available", &RuntimeManager::isRuntimeManagerAvailable,
             "Non-throwing check that the Runtime Manager process is reachable.")

        .def("registered_instances", &RuntimeManager::registeredInstances,
             py::call_guard<py::gil_scoped_release>(),
             "All instances currently registered, running or not.")

        .def("attach", py::overload_cast<std::string_view>(&RuntimeManager::attach, py::const_),
             py::arg("name"), py::call_guard<py::gil_scoped_release>(),
             "Connect to a registered instance by name. Raises "
             "InstanceNotFoundError, whose message lists what is registered.")
        .def("attach_by_id", py::overload_cast<std::int32_t>(&RuntimeManager::attach, py::const_),
             py::arg("id"), py::call_guard<py::gil_scoped_release>(),
             "Connect to a registered instance by ID.")

        .def("try_attach", &RuntimeManager::tryAttach, py::arg("name"),
             py::call_guard<py::gil_scoped_release>(),
             "Like attach(), but returns None for retryable failures instead of "
             "raising. Genuine caller errors still raise.")

        .def_property_readonly("version", &RuntimeManager::version)

        .def("__enter__", [](RuntimeManager& self) -> RuntimeManager& { return self; })
        .def("__exit__", [](RuntimeManager&, py::object, py::object, py::object) { return false; });
}
