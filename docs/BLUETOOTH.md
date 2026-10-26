# Bluetooth and supported models

[Documentation](README.md)

The model registry recognizes 17 advertised names from the upstream ALLPOWERS
project and separates Old10, Legacy16 and R2500V2 packet families. A model hint
does not prove all devices of that name have been tested. See the registry in
core/src/AllpowersModels.h and its retained MIT notice/provenance commits.

## Compatibility and evidence

| Protocol family | Known telemetry | Evidence |
| --- | --- | --- |
| R2500 V2 | Charge, input/output, runtime, AC/DC/USB outputs, explicit mains/battery source | R2500 V2 + Waveshare BLE/GATT and HID UPS on Windows, 2026-10-25; see [validation](validation.md) for scope and revision limits |
| Legacy16 | Charge, power, runtime, AC/DC/light outputs; USB output and mains source unknown | Offline protocol tests; individual catalog models unverified |
| Old10 | Charge, power, runtime, cycles, AC/DC/USB/light outputs; mains source unknown | Offline protocol tests; individual catalog models unverified |

USB UPS reporting is implemented for R2500 V2. Other profiles expose only their
known fields; missing or stale readings never become invented battery values.
Pocket is compiled and protocol-tested; its hardware operation in this scenario
is unverified. The native Windows service/driver is experimental for every model:
board HID evidence does not validate that separate path or Windows power actions.

## Discovery and connection

Both firmware and desktop are BLE Central/GATT clients. They use service FFF0,
notify FFF1 and write FFF2 with the standard base UUID. Polls are read-only;
there are no station-control commands in the application.

Firmware's device picker lists nearby connectable BLE advertisers, including
unknown names. Non-connectable advertisers and Bluetooth Classic are excluded.
Matching a name or advertisement does not prove authorization/GATT compatibility.
The last successful target is saved in NVS, not compiled into the binary.

The desktop defaults to a station-name filter; it can also show unknown devices.
A fresh selection/connection resets the session. Old notification callbacks cannot
update a new peer. Actual connection requires the expected GATT characteristics.

Some stations accept a single client: leave the desktop disconnected while a USB
adapter owns the connection. No hardware connection was switched during cleanup.
