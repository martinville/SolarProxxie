# Notices

Copyright © 2026 Martin Viljoen.

SolarProxxie's C firmware, UI and development tools are original project code,
distributed under GPL-3.0-or-later (see LICENSE).

Protocol/register reference material: Bruce Merry,
[sunsniff](https://github.com/bmerry/sunsniff), GPL-3.0-or-later, inspected 2026-09-14.
`docs/reference-fields.csv` contains the reference data; `main/modbus/register_map.c`
encodes those documented offsets and conversions. No Linux capture/backend code
was transplanted into the ESP32 firmware. Limitations of the reference, including
signedness and model scope, are retained in docs/PROTOCOL.md.

Additional candidate field names and scales were compared with Johann Kellerman's
[kellerza/sunsynk](https://github.com/kellerza/sunsynk), Apache-2.0. No Python or
Home Assistant add-on code from that project is included in the firmware.

ESP-IDF 5.4.2 and its bundled components retain their own licenses, including
Espressif, lwIP, mbedTLS, cJSON and ESP-MQTT notices in the SDK. They are not vendored
into this source tree. Local downloaded SDK/tool directories are ignored by Git.

Development-only npm dependencies retain their package licenses. They are not
included in embedded web resources. No third-party JS/CSS CDN runs on the device.
