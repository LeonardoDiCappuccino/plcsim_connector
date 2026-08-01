"""``python -m plcsim_connector`` - list the registered PLC instances.

The quickest way to check that S7-PLCSIM Advanced is installed, the Runtime
Manager is up, and an instance is available to attach to.
"""

from __future__ import annotations

import sys

from . import Area, PlcSimError, RuntimeManager, describe_api_dll_search, exchanges_io


def main() -> int:
    print(describe_api_dll_search())

    try:
        runtime = RuntimeManager()
    except PlcSimError as exc:
        print(f"Cannot reach the Runtime Manager: {exc}", file=sys.stderr)
        print(f"  kind={exc.kind} code={exc.code_name} retryable={exc.retryable}",
              file=sys.stderr)
        return 1

    print(f"Runtime Manager version {runtime.version}")

    instances = runtime.registered_instances()
    if not instances:
        print("No instances registered. Start one from the PLCSIM Advanced Control Panel.")
        return 1

    print(f"{len(instances)} instance(s) registered:")
    for info in instances:
        try:
            with runtime.attach(info.name) as plc:
                state = plc.operating_state
                detail = f"{state.name}"
                if exchanges_io(state):
                    detail += (
                        f", %I {plc.area_size(Area.Input)} B"
                        f", %Q {plc.area_size(Area.Output)} B"
                        f", %M {plc.area_size(Area.Marker)} B"
                    )
                else:
                    detail += ", I/O inactive (not in RUN)"
                print(f"  [{info.id}] {info.name}: {detail}")
        except PlcSimError as exc:
            print(f"  [{info.id}] {info.name}: unavailable - {exc}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
