"""Webots controller wiring robot sensors/actuators to a simulated S7 PLC.

Drop this in a Webots controller directory. It maps:

    distance sensor  -> %I10.2   (digital input to the PLC)
    %Q4.1            -> motor    (digital output from the PLC)

Prerequisites
-------------
1. S7-PLCSIM Advanced Control Panel running, with an instance registered under
   the name in ``INSTANCE_NAME`` and a project downloaded to it.
2. ``plcsim_connector`` importable by Webots' Python. Either ``pip install .``
   into the interpreter Webots uses, or set PYTHONPATH to the build output:
       set PYTHONPATH=<repo>/build/msvc-x64-debug/python

Uses ReconnectingInstance so restarting the PLC - or it not being up yet at
all - does not kill or freeze the controller: every call is non-blocking.
"""

from controller import Robot  # provided by Webots

import plcsim_connector as plcsim

INSTANCE_NAME = "Webots"

# Sensor -> PLC input, PLC output -> actuator.
SENSOR_INPUT_BIT = "%I10.2"
MOTOR_OUTPUT_BIT = "%Q4.1"

SENSOR_THRESHOLD = 500.0
MOTOR_VELOCITY = 5.0


def on_reconnect(instance: plcsim.Instance) -> None:
    """Re-assert known state after a reconnect.

    The PLC may have restarted, in which case everything the API previously
    wrote to the input area is gone. Clearing the bit here keeps the controller
    and the PLC from disagreeing about it.
    """
    print(f"[plcsim] connected to {instance.name!r} "
          f"(state {instance.operating_state.name})")
    instance.setAddressBit(SENSOR_INPUT_BIT, False)


def main() -> None:
    robot = Robot()
    timestep = int(robot.getBasicTimeStep())

    sensor = robot.getDevice("distance sensor")
    sensor.enable(timestep)

    motor = robot.getDevice("wheel motor")
    motor.setPosition(float("inf"))
    motor.setVelocity(0.0)

    plc = plcsim.ReconnectingInstance(
        INSTANCE_NAME,
        on_reconnect=on_reconnect,
    )

    was_connected = False

    while robot.step(timestep) != -1:
        try:
            # DI: tell the PLC whether something is in front of the sensor.
            plc.setAddressBit(SENSOR_INPUT_BIT, sensor.getValue() > SENSOR_THRESHOLD)

            # DO: let the PLC's user program decide whether the wheel turns.
            motor.setVelocity(
                MOTOR_VELOCITY if plc.getAddressBit(MOTOR_OUTPUT_BIT) else 0.0
            )
            was_connected = True

        except plcsim.RetryableError as exc:
            # Covers both "not connected yet" (nothing registered) and "was
            # connected but a call plus its one retry both failed". Neither
            # call blocks, so this step just coasts and the next one tries
            # again. Only log the transition so a PLC restart, or the
            # Control Panel simply not being up yet, doesn't spam the
            # console every step.
            if was_connected:
                print(f"[plcsim] lost connection, coasting: {exc}")
            was_connected = False
            motor.setVelocity(0.0)

        except plcsim.PlcSimError as exc:
            # Non-retryable: a bad address or an out-of-range offset. That is a
            # bug in this controller, so fail loudly rather than spinning.
            print(f"[plcsim] fatal: {exc} (kind={exc.kind}, code={exc.code_name})")
            raise

    plc.close()


if __name__ == "__main__":
    main()
