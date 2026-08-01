"""Python connector for SIMATIC S7-PLCSIM Advanced digital I/O.

Attaches to a PLC instance that the PLCSIM Advanced Control Panel already
manages, and reads/writes single bits by PLC address::

    import plcsim_connector as plcsim

    rt = plcsim.RuntimeManager()
    plc = rt.attach("Webots")

    plc.setAddressBit("%I10.2", True)    # drive a digital input
    lamp = plc.getAddressBit("%Q4.1")    # sample a digital output

This package never powers instances on or off; the Control Panel owns their
lifecycle.
"""

from __future__ import annotations

import functools
import os
import time
import winreg
from typing import Any, Callable, Iterator, Optional, Union

from ._core import (
    Area,
    BitAddress,
    Instance,
    InstanceInfo,
    OperatingState,
    exchanges_io,
    parse_bit_address,
)
from ._core import RuntimeManager as _NativeRuntimeManager
from ._core import (
    ApiNotInitializedError,
    ConnectionLostError,
    IndexOutOfRangeError,
    InstanceNotFoundError,
    InstanceNotRunningError,
    InvalidAddressError,
    PlcSimError,
    RetryableError,
    RuntimeManagerUnavailableError,
    TimeoutError,
)
from ._core import __api_version__, __version__

# -- API DLL directory auto-detection ----------------------------------------
#
# Mirrors cmake/FindPlcSimAdvancedApi.cmake's search: the registry install
# path first, then the default installation directory. InitializeApi() does
# its own search too (app folder, then this same registry path), but it stops
# at the first folder where *a* same-named DLL exists - not the first
# *correct* one - so a stale copy anywhere ahead of the real install (a
# leftover from a previous version, a different app's bundled copy) causes an
# unclassified failure instead of a clear one. Resolving the exact directory
# ourselves and passing it explicitly sidesteps that.
_REGISTRY_KEY = r"SOFTWARE\Wow6432Node\Siemens\Shared Tools\PLCSIMADV_SimRT"
_DEFAULT_INSTALL_DIR = r"C:\Program Files (x86)\Common Files\Siemens\PLCSIMADV"
_API_DLL_NAME = "Siemens.Simatic.Simulation.Runtime.Api.x64.dll"


@functools.lru_cache(maxsize=1)
def _autodetect_api_dll_dir() -> Optional[str]:
    """Best-effort locate of the S7-PLCSIM Advanced API directory matching
    the version this package was built against.

    Returns None - leaving InitializeApi()'s own search to run - if no
    directory holding the DLL can be found, so this is a strict improvement
    over the default behaviour rather than a new way to fail. Cached for the
    life of the process: it does one registry read and a couple of
    filesystem checks, not one per connection attempt.
    """
    if not __api_version__:
        return None

    candidates = []
    try:
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            _REGISTRY_KEY,
            0,
            winreg.KEY_READ | winreg.KEY_WOW64_32KEY,
        ) as key:
            install_path, _ = winreg.QueryValueEx(key, "Path")
            candidates.append(os.path.join(install_path, "API", __api_version__))
    except OSError:
        pass
    candidates.append(os.path.join(_DEFAULT_INSTALL_DIR, "API", __api_version__))

    for candidate in candidates:
        if os.path.isfile(os.path.join(candidate, _API_DLL_NAME)):
            return candidate
    return None


class RuntimeManager(_NativeRuntimeManager):
    """Connection to the S7-PLCSIM Advanced Runtime Manager.

    Same as the native :class:`RuntimeManager`, except when *api_dll_dir* is
    not given, it is auto-detected from the registry (see
    :func:`_autodetect_api_dll_dir`) instead of being left to
    ``InitializeApi()``'s own, less reliable, search.
    """

    def __init__(
        self, api_dll_dir: Optional[Union[str, "os.PathLike[str]"]] = None
    ) -> None:
        if api_dll_dir is None:
            api_dll_dir = _autodetect_api_dll_dir()
        super().__init__(api_dll_dir)


__all__ = [
    # Core types
    "Area",
    "BitAddress",
    "Instance",
    "InstanceInfo",
    "OperatingState",
    "RuntimeManager",
    "exchanges_io",
    "parse_bit_address",
    # Exceptions
    "PlcSimError",
    "RetryableError",
    "ApiNotInitializedError",
    "ConnectionLostError",
    "IndexOutOfRangeError",
    "InstanceNotFoundError",
    "InstanceNotRunningError",
    "InvalidAddressError",
    "RuntimeManagerUnavailableError",
    "TimeoutError",
    # Reconnect helpers
    "wait_for_instance",
    "ReconnectingInstance",
    "__version__",
]


def wait_for_instance(
    name: str,
    *,
    timeout: Optional[float] = 30.0,
    poll_interval: float = 0.5,
    runtime: Optional[RuntimeManager] = None,
    api_dll_dir: Optional[Union[str, "os.PathLike[str]"]] = None,
) -> Instance:
    """Block until an instance called *name* is registered, then attach to it.

    Polls :meth:`RuntimeManager.try_attach`, so it tolerates the Control Panel
    not being up yet, the instance not being created yet, and transient
    timeouts. Non-retryable failures - a malformed name, a missing
    S7-PLCSIM Advanced installation - propagate immediately rather than
    spinning.

    :param timeout: seconds to keep trying, or ``None`` to wait forever.
    :param api_dll_dir: forwarded to :class:`RuntimeManager` when *runtime* is
        not given. Set this if the Siemens API DLL is not found by the
        default search order, e.g. a non-default S7-PLCSIM Advanced install.
    :raises InstanceNotFoundError: if the timeout expires.
    """
    deadline = None if timeout is None else time.monotonic() + timeout
    last_error: Optional[PlcSimError] = None

    while True:
        try:
            rt = runtime if runtime is not None else RuntimeManager(api_dll_dir)
            instance = rt.try_attach(name)
            if instance is not None:
                return instance
        except RetryableError as exc:
            # The Runtime Manager itself is not up yet. Keep waiting.
            last_error = exc

        if deadline is not None and time.monotonic() >= deadline:
            message = f"no instance named {name!r} appeared within {timeout:g}s"
            if last_error is not None:
                message += f" (last error: {last_error})"
            raise InstanceNotFoundError(message)

        time.sleep(poll_interval)


class ReconnectingInstance:
    """An :class:`Instance` that re-attaches after the connection drops.

    Wraps the same ``setAddressBit`` / ``getAddressBit`` surface. When a call
    fails with a retryable error, the underlying handle is discarded and a new
    one is attached before the call is retried once. Non-retryable errors -
    a bad address, an out-of-range offset - propagate untouched.

    Intended for long-running simulation loops (Webots controllers, HIL rigs)
    that should survive a PLC restart without tearing down the whole
    controller::

        plc = plcsim.ReconnectingInstance("Webots")
        while robot.step(timestep) != -1:
            plc.setAddressBit("%I10.2", sensor.getValue() > 500)
            motor.setVelocity(5.0 if plc.getAddressBit("%Q4.1") else 0.0)

    Reads return *stale* values while disconnected if ``default_on_failure`` is
    set; by default the exception propagates once reconnection also fails.
    """

    def __init__(
        self,
        name: str,
        *,
        connect_timeout: Optional[float] = 30.0,
        poll_interval: float = 0.5,
        on_reconnect: Optional[Callable[[Instance], None]] = None,
        api_dll_dir: Optional[Union[str, "os.PathLike[str]"]] = None,
    ) -> None:
        """:param on_reconnect: called with each freshly attached instance -
        the hook for re-asserting known input state after a PLC restart.
        :param api_dll_dir: forwarded to every :class:`RuntimeManager` this
            creates. Set this if the Siemens API DLL is not found by the
            default search order, e.g. a non-default S7-PLCSIM Advanced
            install.
        """
        self._name = name
        self._connect_timeout = connect_timeout
        self._poll_interval = poll_interval
        self._on_reconnect = on_reconnect
        self._api_dll_dir = api_dll_dir
        self._runtime: Optional[RuntimeManager] = None
        self._instance: Optional[Instance] = None

    # -- Connection management ------------------------------------------

    @property
    def name(self) -> str:
        return self._name

    def is_connected(self) -> bool:
        """Never raises; safe to poll every simulation step."""
        return self._instance is not None and self._instance.is_connected()

    def connect(self) -> Instance:
        """Attach if not already attached. Idempotent."""
        if self._instance is not None and self._instance.is_connected():
            return self._instance

        self._drop()

        # A dropped Runtime Manager invalidates the manager handle too, so
        # rebuild both rather than reusing a stale one.
        self._runtime = RuntimeManager(self._api_dll_dir)
        self._instance = wait_for_instance(
            self._name,
            timeout=self._connect_timeout,
            poll_interval=self._poll_interval,
            runtime=self._runtime,
        )

        if self._on_reconnect is not None:
            self._on_reconnect(self._instance)

        return self._instance

    def close(self) -> None:
        self._drop()

    def _drop(self) -> None:
        if self._instance is not None:
            self._instance.close()
        self._instance = None
        self._runtime = None

    def _call(self, method: str, *args: Any) -> Any:
        instance = self.connect()
        try:
            return getattr(instance, method)(*args)
        except RetryableError:
            # One reconnect, one retry. A second failure is real and propagates.
            self._drop()
            instance = self.connect()
            return getattr(instance, method)(*args)

    # -- I/O -------------------------------------------------------------

    def setAddressBit(self, address: str, value: bool) -> None:
        self._call("setAddressBit", address, value)

    def getAddressBit(self, address: str) -> bool:
        return bool(self._call("getAddressBit", address))

    set_address_bit = setAddressBit
    get_address_bit = getAddressBit

    def area_size(self, area: Area) -> int:
        return int(self._call("area_size", area))

    @property
    def operating_state(self) -> OperatingState:
        return self.connect().operating_state

    def run(self) -> None:
        self._call("run")

    def stop(self) -> None:
        self._call("stop")

    # -- Context manager --------------------------------------------------

    def __enter__(self) -> "ReconnectingInstance":
        self.connect()
        return self

    def __exit__(self, *exc_info: object) -> bool:
        self.close()
        return False

    def __repr__(self) -> str:
        state = "connected" if self.is_connected() else "disconnected"
        return f"<ReconnectingInstance {self._name!r} {state}>"
