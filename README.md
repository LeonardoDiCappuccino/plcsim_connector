# plcsim_connector

A C++ connector with Python bindings for **SIMATIC S7-PLCSIM Advanced**, aimed at
driving digital I/O from a simulation environment such as Webots.

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

This is a **client** of a PLC instance that already exists. The
PLCSIM Advanced Control Panel owns the instance lifecycle: creating it, powering
it on, downloading a project. The connector attaches to it by name, reads and
writes single bits, and can put it into RUN or STOP.

Supported today: single-bit access to `%I`, `%Q` and `%M`.
Not supported: byte/word/dword access, data blocks, tag-name access, events,
cycle control. All are reachable through the SDK and are natural v2 additions.

## Requirements

| | |
|---|---|
| OS | Windows (x64) |
| Toolchain | MSVC (Visual Studio 2022). The Siemens API exposes C++ classes with virtual functions, so the ABI is MSVC's. |
| CMake | ≥ 3.24 |
| S7-PLCSIM Advanced | Installed. The build reads its path from the registry. |
| Python | ≥ 3.9, 64-bit (optional) |

The Siemens SDK is **not vendored**. `cmake/FindPlcSimAdvancedApi.cmake` locates
`SimulationRuntimeApi.h` from the local installation:

1. `-DPLCSIM_API_DIR=<dir>` if you set it,
2. else `HKLM\SOFTWARE\Wow6432Node\Siemens\Shared Tools\PLCSIMADV_SimRT` → `Path`, plus `API/<version>`,
3. else `C:/Program Files (x86)/Common Files/Siemens/PLCSIMADV/API/<version>`.

Select a different API version with `-DPLCSIM_API_VERSION=7.0` (default `8.0`).

## Build

```bash
cmake --preset msvc-x64-debug && cmake --build --preset msvc-x64-debug
```

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

The live suite is disabled unless you name an instance:

```bash
cmake --preset msvc-x64-debug -DPLCSIM_LIVE_INSTANCE=PLC_1
```

Catch2 is fetched automatically (or reused from a `find_package`-able install)
and registered with CTest via `catch_discover_tests`, so each `TEST_CASE` shows
up individually in CTest and in VS Code's Test Explorer.

## Addresses

Accepted, case-insensitively, with optional surrounding whitespace:

| Form | Meaning |
|---|---|
| `%I10.2`, `I10.2` | input, byte 10, bit 2 — leading `%` optional |
| `%IX10.2` | IEC bit prefix `X` optional |
| `%E10.2`, `%A4.1` | German mnemonics (Eingang / Ausgang) |
| `%M0.0` | marker |

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
  writes to `%I` are *not* visible to the API. Reading `%I` returns what the API
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
    └── InstanceNotRunningError          not in RUN
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
```

`ReconnectingInstance` retries a failed call once after re-attaching;
non-retryable errors propagate untouched so a typo'd address cannot become an
infinite loop. Use `on_reconnect` to re-assert input state, which the PLC loses
across a restart.

## Performance

Each `setAddressBit`/`getAddressBit` is one IPC round trip to the instance
process. Measured on this machine: **~0.03 ms per access**, so ~128 bits per
Webots step costs ~3.6 ms against a typical 32 ms timestep.

If you ever outgrow that, `ReadBytes`/`WriteBytes` transfer whole areas in one
call. The internals separate address parsing from transfer specifically so a
cached/batched mode can be added behind the same call sites.

## Layout

```
include/plcsim/     public C++ API — no Windows.h, no Siemens headers
src/
  Address.cpp       address parser      ┐ SDK-free, unit-tested
  Error.cpp         error classification│ without PLCSIM installed
  Types.cpp         enum names          ┘
  SdkBridge.cpp     the ONLY file that includes SimulationRuntimeApi.h
bindings/python/    pybind11 module + the plcsim_connector package
cmake/              SDK discovery, package config
tests/              Catch2 unit tests + live suite
examples/           C++ demo, Webots controller
docs/               the Siemens API manual
```

### Why one SDK translation unit

`SimulationRuntimeApi.h` declares its DLL handle and `DestroyInterface` function
pointer as **file-scope statics**, so every including TU gets its own copy.
`InitializeApi()` in one file leaves `DestroyInterface()` in another with a null
pointer, failing at runtime with no useful diagnostic. Confining the header to
`SdkBridge.cpp` makes that class of bug impossible, and keeps `Windows.h` out of
the public headers as a side benefit.

Relatedly, `ShutdownAndFreeApi()` unconditionally `FreeLibrary`s the DLL, so two
independently destroyed `RuntimeManager` objects would tear the API out from
under each other. They share one mutex-guarded, refcounted initialisation
instead.

## Limits

- **16 instances** may be registered in the Runtime Manager at once.
- **No documented limit on clients per instance.** `CreateInterface` is
  reference-counted; several processes may attach to the same PLC.
- The I/O memory is shared and serialised — concurrent writers contend, and the
  last write wins. Coordinate if more than one process drives the same bits.
- Releasing the *last* interface to an instance unregisters it. Not a concern
  here: the Control Panel holds its own reference.

## Licence

MIT for this connector. The Siemens SDK, its header and its DLLs are proprietary
and are neither included nor redistributed — they come from your local
S7-PLCSIM Advanced installation.
