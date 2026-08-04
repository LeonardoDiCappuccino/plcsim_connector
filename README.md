# plcsim_connector

A C++ connector with Python bindings for **SIMATIC S7-PLCSIM Advanced**, aimed
at driving digital I/O from a simulation environment such as Webots.

Python users can skip the C++ toolchain entirely: grab the prebuilt wheel from
[GitHub Releases](https://github.com/LeonardoDiCappuccino/plcsim_connector/releases)
instead of building from source. Install for your python version (cp313 -> 3.13):

```bash
pip install https://github.com/LeonardoDiCappuccino/plcsim_connector/releases/download/v1.0.0/plcsim_connector-1.0.0-cp313-cp313-win_amd64.whl
```

(the exact filename depends on your Python version — pick the matching `.whl`
from the release assets). Requires S7-PLCSIM Advanced **V8.0** installed
locally (the wheel loads its runtime DLL dynamically) and Python **≥ 3.9** —
see [Requirements](#requirements) below.

```python
import plcsim_connector as plcsim

rt = plcsim.RuntimeManager()
plc = rt.attach("PLC_1")

plc.setAddressBit("%I10.2", True)     # drive a digital input
lamp = plc.getAddressBit("%Q4.1")     # sample a digital output  -> bool
```

```cpp
#include "plcsim/plcsim.hpp"

plcsim::RuntimeManager runtime;
auto plc = runtime.attach("PLC_1");

plc.setAddressBit("%I10.2", true);
const bool lamp = plc.getAddressBit("%Q4.1");
```

## Scope

This is a **client** of a PLC instance that already exists. The PLCSIM Advanced
Control Panel owns the instance lifecycle: creating it, powering it on,
downloading a project. The connector attaches to it by name, reads and writes
single bits, and can put it into RUN or STOP.

Supported today: single-bit access to `%I`, `%Q` and `%M`. Not supported:
byte/word/dword access, data blocks, tag-name access, events, cycle control. All
are reachable through the SDK and are natural v2 additions.

## Requirements

|                    |                                                                                                              |
| ------------------ | ------------------------------------------------------------------------------------------------------------ |
| OS                 | Windows (x64 or x86)                                                                                         |
| Toolchain          | MSVC (Visual Studio 2022). The Siemens API exposes C++ classes with virtual functions, so the ABI is MSVC's. |
| CMake              | >= 3.24                                                                                                      |
| S7-PLCSIM Advanced | Installed. The build reads its path from the registry.                                                       |
| Python             | ≥ 3.9, matching the target architecture (optional)                                                           |

The Siemens SDK is **not vendored**. `cmake/FindPlcSimAdvancedApi.cmake` locates
`SimulationRuntimeApi.h` from the local installation:

1. `-DPLCSIM_API_DIR=<dir>` if you set it,
2. else `HKLM\SOFTWARE\Wow6432Node\Siemens\Shared Tools\PLCSIMADV_SimRT` →
   `Path`, plus `API/<version>`,
3. else `C:/Program Files (x86)/Common Files/Siemens/PLCSIMADV/API/<version>`.

Select a different API version with `-DPLCSIM_API_VERSION=7.0` (default `8.0`).

## Build

```bash
cmake --preset msvc-x64-debug && cmake --build --preset msvc-x64-debug
```

A 32-bit build is available as `msvc-x86-debug` / `msvc-x86-release`. If
`PLCSIM_BUILD_PYTHON` is on, CMake's `find_package(Python)` must resolve to a
32-bit Python interpreter for an x86 configure — put one first on `PATH` or pass
`-DPython_EXECUTABLE=<path-to-32-bit-python.exe>`, otherwise the pybind11 module
fails to link against the wrong-bitness Python libs.

Then make the Python package importable from the build tree:

```bash
set PYTHONPATH=%CD%\build\msvc-x64-debug\python
```

Or install it properly:

```bash
pip install .
```

Check the installation end to end — this lists every registered instance, its
operating state and its area sizes:

```bash
python -m plcsim_connector
```

## Tests

The suite splits by whether it needs a live PLC:

```bash
ctest --preset unit    # address parsing + error classification; no PLCSIM needed
ctest --preset live    # needs a running instance
```

`unit-x86`/`live-x86` run the same suites against the `msvc-x86-debug`
configure.

The live suite defaults to expecting an instance named `Connector_Tests`. Point
it at a different name, or pass an empty value to disable the suite entirely
(excluded even from a plain, unfiltered `ctest`):

```bash
cmake --preset msvc-x64-debug -DPLCSIM_LIVE_INSTANCE=Connector_Tests   # default, shown explicitly
cmake --preset msvc-x64-debug -DPLCSIM_LIVE_INSTANCE=""                # disable the live suite
```

The named instance must already be registered in the Runtime Manager (create it
from a downloaded project via the PLCSIM Advanced Control Panel) and needs:

- **At least one input byte and one output byte** in its hardware configuration
  (`%I`/`%Q`). A bare CPU with no I/O modules reports 0-byte areas, and the
  "Digital I/O round trip" test fails rather than skips on that —
  `areaSize(Input)`/`areaSize(Output)` must both be non-zero.
- **To be put into RUN** before the suite runs. If it is not, the I/O round-trip
  test is skipped (not failed) since inputs/outputs are not exchanged outside
  RUN — `Runtime Manager is reachable` and the two not-found tests still run
  regardless of state.
- **Marker memory available**, which any default S7-1200/1500 project already
  reserves. The test toggles the last bit of the last `%M` byte and restores it
  afterwards, so it is safe to reuse a project with a real user program as long
  as nothing else touches that specific bit.

Catch2 is fetched automatically (or reused from a `find_package`-able install)
and registered with CTest via `catch_discover_tests`, so each `TEST_CASE` shows
up individually in CTest and in VS Code's Test Explorer.

## Addresses

Accepted, case-insensitively, with optional surrounding whitespace:

| Form              | Meaning                                      |
| ----------------- | -------------------------------------------- |
| `%I10.2`, `I10.2` | input, byte 10, bit 2 — leading `%` optional |
| `%IX10.2`         | IEC bit prefix `X` optional                  |
| `%E10.2`, `%A4.1` | German mnemonics (Eingang / Ausgang)         |
| `%M0.0`           | marker                                       |

Rejected with an explanatory message: `%IB10` / `%QW4` / `%MD100` (wider
accesses), `DB1.DBX0.0` (data blocks), bit indices above 7.

Offsets are also bounds-checked locally against the live area size, so a typo
reports what is actually available:

```
getAddressBit failed: byte offset 99999 is outside area %I,
which is 32768 bytes (valid offsets 0..32767)
```

## Semantics worth knowing

These come from the PLCSIM Advanced manual and will bite otherwise:

- **The instance must be in RUN** for writes to reach the process image.
- **Inputs are API-private.** Values that STEP 7 or a communication partner
  writes to `%I` are _not_ visible to the API. Reading `%I` returns what the API
  last wrote. This is fine for the DI use case — you are the input device.
- **API writes beat the user program.** If both write the same area, the API
  wins. Writing `%Q` is therefore possible but usually wrong.
- **`%M` is the safe scratch area** — it drives no simulated device.

## Error handling and reconnection

Every failure is classified, so a reconnect loop never has to enumerate Siemens
error codes. In Python the classification is the exception type:

```
PlcSimError
├── InvalidAddressError        never retryable
├── IndexOutOfRangeError       never retryable
├── ApiNotInitializedError     never retryable
└── RetryableError             ← catch this in a reconnect loop
    ├── RuntimeManagerUnavailableError   Control Panel not running
    ├── InstanceNotFoundError            instance not registered (yet)
    ├── ConnectionLostError              interface dropped
    ├── TimeoutError
    ├── InstanceNotRunningError          not in RUN
    └── NotConnectedError                ReconnectingInstance: not attached
                                          yet, no native call was made
```

Every exception carries `.kind`, `.code`, `.code_name` and `.retryable`. The C++
side has the same split via `plcsim::Error::kind()` and `.retryable()`.

Not-found errors are self-diagnosing:

```
no simulated PLC instance named "nope" is registered;
registered instances: "PLC_1" (id 0)
```

Three building blocks, in increasing order of convenience:

```python
# 1. Non-throwing probes
rt.is_runtime_manager_available()   # bool, never raises
rt.try_attach("PLC_1")              # Instance | None
plc.is_connected()                  # bool, never raises

# 2. Block until it shows up
plc = plcsim.wait_for_instance("PLC_1", timeout=None)

# 3. Transparent re-attach after a PLC restart
plc = plcsim.ReconnectingInstance("PLC_1", on_reconnect=reassert_inputs)
plc.poll_connect()                  # bool, never raises; attach if possible
```

`ReconnectingInstance` retries a failed call once after re-attaching;
non-retryable errors propagate untouched so a typo'd address cannot become an
infinite loop. Use `on_reconnect` to re-assert input state, which the PLC loses
across a restart.

`poll_connect()` is the non-blocking building block underneath every I/O call:
it makes at most one `try_attach()` attempt - a single cheap IPC round trip, no
different in cost from a normal read/write - and returns whether a connected
instance is available afterwards, without ever raising. `setAddressBit()` /
`getAddressBit()` / etc. call it implicitly and raise `NotConnectedError`
immediately if it doesn't produce a live instance, rather than waiting for one.
Safe to call from inside a fixed-step simulation loop even before the Control
Panel is up; it will never stall the loop. Call `connect()` instead if you
specifically want to block, e.g. once at startup.

## Performance

Each `setAddressBit`/`getAddressBit` is one IPC round trip to the instance
process. Measured on this machine: **~0.03 ms per access**, so ~128 bits per
Webots step costs ~3.6 ms against a typical 32 ms timestep.

If you ever outgrow that, `ReadBytes`/`WriteBytes` transfer whole areas in one
call. The internals separate address parsing from transfer specifically so a
cached/batched mode can be added behind the same call sites.

## Limits

- **16 instances** may be registered in the Runtime Manager at once.
- **No documented limit on clients per instance.** `CreateInterface` is
  reference-counted; several processes may attach to the same PLC.
- The I/O memory is shared and serialised — concurrent writers contend, and the
  last write wins. Coordinate if more than one process drives the same bits.
- Releasing the _last_ interface to an instance unregisters it. Not a concern
  here: the Control Panel holds its own reference.

## Licence

MIT for this connector. The Siemens SDK, its header and its DLLs are proprietary
and are neither included nor redistributed — they come from your local S7-PLCSIM
Advanced installation.
