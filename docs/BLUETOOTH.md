# Bluetooth and supported models

The model registry recognizes 17 advertised names from the upstream ALLPOWERS
project and separates Old10, Legacy16 and R2500V2 packet families. A model hint
does not prove all devices of that name have been tested. See the registry in
core/src/AllpowersModels.h and its retained MIT notice/provenance commits.

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
