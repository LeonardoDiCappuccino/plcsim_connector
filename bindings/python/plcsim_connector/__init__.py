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
import sys
import time
import winreg
from typing import Any, Callable, Iterator, List, Optional, Tuple, Union

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
# The DLL name must match this interpreter's bitness, not the machine's: a
# 32-bit Python loads the 32-bit extension module, which can only load the
# x86 API DLL, even on a 64-bit Windows install.
_API_DLL_NAME = (
    "Siemens.Simatic.Simulation.Runtime.Api.x64.dll"
    if sys.maxsize > 2**32
    else "Siemens.Simatic.Simulation.Runtime.Api.x86.dll"
)


def _registry_install_path() -> Tuple[Optional[str], Optional[str]]:
    """(install path, error) from the registry - exactly one is None."""
    try:
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            _REGISTRY_KEY,
            0,
            winreg.KEY_READ | winreg.KEY_WOW64_32KEY,
        ) as key:
            install_path, _ = winreg.QueryValueEx(key, "Path")
        return install_path, None
    except OSError as exc:
        return None, str(exc)


def _candidate_api_dll_dirs() -> List[str]:
    """Directories to check, in the same order InitializeApi() itself would
    (registry install path, then the default install path)."""
    install_path, _ = _registry_install_path()
    candidates = []
    if install_path:
        candidates.append(os.path.join(install_path, "API", __api_version__))
    candidates.append(os.path.join(_DEFAULT_INSTALL_DIR, "API", __api_version__))
    return candidates


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

    for candidate in _candidate_api_dll_dirs():
        if os.path.isfile(os.path.join(candidate, _API_DLL_NAME)):
            return candidate
    return None


def api_dll_dir() -> Optional[str]:
    """The auto-detected S7-PLCSIM Advanced API DLL directory, or None if it
    could not be found.

    This is what :class:`RuntimeManager` uses by default when *api_dll_dir*
    is not given explicitly.
    """
    return _autodetect_api_dll_dir()


def describe_api_dll_search() -> str:
    """Multi-line diagnostic report of the API DLL auto-detection: the API
    version this package was built against, the registry lookup outcome, and
    every candidate directory checked, with which one (if any) was found.

    Print this - ``print(plcsim.describe_api_dll_search())`` - when
    :class:`RuntimeManager` fails to find the SDK on a machine where
    S7-PLCSIM Advanced is installed; it is also printed by
    ``python -m plcsim_connector``.
    """
    lines = [f"built against S7-PLCSIM Advanced API version: {__api_version__ or '(unknown)'}"]
    if not __api_version__:
        lines.append("no API version baked into this build; auto-detection is disabled")
        return "\n".join(lines)

    install_path, error = _registry_install_path()
    if install_path:
        lines.append(f"registry install path ({_REGISTRY_KEY}): {install_path}")
    else:
        lines.append(f"registry lookup failed ({_REGISTRY_KEY}): {error}")

    found = _autodetect_api_dll_dir()
    for candidate in _candidate_api_dll_dirs():
        marker = "FOUND" if candidate == found else "not found"
        lines.append(f"  [{marker}] {os.path.join(candidate, _API_DLL_NAME)}")

    if found is None:
        lines.append(
            "no candidate had the DLL; RuntimeManager() falls back to "
            "InitializeApi()'s own search (app folder, then the same registry path)"
        )
    return "\n".join(lines)


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
    # API DLL diagnostics
    "api_dll_dir",
    "describe_api_dll_search",
    # Exceptions
    "PlcSimError",
    "RetryableError",
    "ApiNotInitializedError",
    "ConnectionLostError",
    "IndexOutOfRangeError",
    "InstanceNotFoundError",
    "InstanceNotRunningError",
    "InvalidAddressError",
    "NotConnectedError",
    "RuntimeManagerUnavailableError",
    "TimeoutError",
    # Reconnect helpers
    "wait_for_instance",
    "ReconnectingInstance",
    "__version__",
]


class NotConnectedError(RetryableError):
    """Raised by :class:`ReconnectingInstance`'s I/O calls when no instance is
    currently attached and the non-blocking (re)attach attempt made for this
    call did not produce one.

    Purely a Python-side signal - unlike the other :class:`RetryableError`
    subclasses, no native call produced it. It exists so a caller can write
    a single ``except RetryableError`` around a simulation step and coast
    through both "PLC not up yet" and "call failed, reconnect also failed"
    without distinguishing them.
    """

    code = -1
    code_name = "NotConnected"
    kind = "NotConnected"
    retryable = True


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
    that should survive a PLC restart - or the PLC not being up yet at all -
    without ever blocking the caller's loop::

        plc = plcsim.ReconnectingInstance("Webots")
        while robot.step(timestep) != -1:
            try:
                plc.setAddressBit("%I10.2", sensor.getValue() > 500)
                motor.setVelocity(5.0 if plc.getAddressBit("%Q4.1") else 0.0)
            except plcsim.RetryableError:
                motor.setVelocity(0.0)  # not connected (yet); coast this step

    Every I/O call (``setAddressBit``, ``getAddressBit``, ``run``, ``stop``,
    ``operating_state``) makes at most one non-blocking attach attempt -
    :meth:`RuntimeManager.try_attach` is a single cheap IPC round trip, the
    same cost as a normal read/write, so no throttling is needed - and raises
    :class:`NotConnectedError` (a :class:`RetryableError`) immediately if that
    doesn't produce a live instance, rather than waiting around for one. Use
    :meth:`connect` instead if you explicitly want to block until a
    connection is ready, e.g. once at startup before entering the loop.
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
        """:param connect_timeout: used only by :meth:`connect`, the explicit
            blocking wait - how long to keep retrying before giving up. The
            non-blocking I/O calls never use it; they make one attempt and
            raise immediately.
        :param poll_interval: used only by :meth:`connect`, the delay between
            retries while blocking.
        :param on_reconnect: called with each freshly attached instance -
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
        """Attach if not already attached, blocking until it succeeds or
        ``connect_timeout`` expires. Idempotent.

        For a simulation step loop, prefer the non-blocking I/O calls -
        this is meant for a one-off wait at startup.
        """
        if self._instance is not None and self._instance.is_connected():
            return self._instance

        self._drop()

        # A dropped Runtime Manager invalidates the manager handle too, so
        # rebuild both rather than reusing a stale one.
        self._runtime = RuntimeManager(self._api_dll_dir)
        instance = wait_for_instance(
            self._name,
            timeout=self._connect_timeout,
            poll_interval=self._poll_interval,
            runtime=self._runtime,
        )
        self._attached(instance)
        return instance

    def poll_connect(self) -> bool:
        """Non-blocking. Attach if not already attached.

        Safe to call every simulation step: it makes at most one
        :meth:`RuntimeManager.try_attach` attempt, which is a single cheap,
        non-blocking IPC round trip - the same cost as a normal read/write -
        and never sleeps or waits. Returns whether a connected instance is
        available afterwards.
        """
        if self._instance is not None and self._instance.is_connected():
            return True
        self._drop()

        if self._runtime is None:
            self._runtime = RuntimeManager(self._api_dll_dir)

        instance = self._runtime.try_attach(self._name)
        if instance is None:
            return False

        self._attached(instance)
        return True

    def _attached(self, instance: Instance) -> None:
        self._instance = instance
        if self._on_reconnect is not None:
            self._on_reconnect(instance)

    def close(self) -> None:
        self._drop()

    def _drop(self) -> None:
        if self._instance is not None:
            self._instance.close()
        self._instance = None
        self._runtime = None

    def _call(self, method: str, *args: Any) -> Any:
        instance = self._require_connected()
        try:
            return getattr(instance, method)(*args)
        except RetryableError:
            # One reconnect, one retry. A second failure is real and propagates.
            self._drop()
            instance = self._require_connected()
            return getattr(instance, method)(*args)

    def _require_connected(self) -> Instance:
        """Non-blocking. Attach if possible, else raise immediately."""
        if not self.poll_connect():
            raise NotConnectedError(f"not connected to instance {self._name!r} yet")
        assert self._instance is not None
        return self._instance

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
        return self._require_connected().operating_state

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
