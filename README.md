<p align="center">
  <img src="logo.svg" alt="SolarProxxie logo" width="80">
</p>

# SolarProxxie

SolarProxxie is ESP-IDF firmware for an original dual-core ESP32 with at least 4 MB of flash. It places the ESP32 between an Inteless/PV Inteless Wi-Fi dongle and the home network, forwards the dongle's IPv4 traffic, observes supported telemetry packets, and publishes decoded values to MQTT and Home Assistant.

The project is intended for local monitoring. It does not send inverter commands or Modbus writes. The protocol support is based on packet captures and reference material rather than a vendor-supported API, so values must be checked against the actual inverter model before they are used for billing, safety decisions, or control automations.

The current version is recorded in [`VERSION`](VERSION). Release changes are listed in [`CHANGELOG.md`](CHANGELOG.md).

## Status

- Target: original ESP32 / ESP32-WROOM, dual-core Xtensa, 4 MB flash.
- SDK: ESP-IDF 5.4.2.
- Supported telemetry profiles: Inteless/Sunsynk single-phase 292-byte, 302-byte, and captured 306-byte variants.
- Automated host, parser, configuration, security, MQTT, PCAP, and browser DOM tests are included.
- The firmware builds into both application slots in the supplied 4 MB partition table.
- Hardware behavior still depends on the ESP32 board, dongle firmware, inverter model, Wi-Fi environment, and broker. A successful software test is not a substitute for bench testing.

## How it works

```text
Inteless dongle
      |
      | Wi-Fi connection to the SolarProxxie access point
      v
+------------------------- ESP32 --------------------------+
| SoftAP + DHCP + DNS                                      |
|          |                                                |
|          +--> IPv4 NAPT --> home Wi-Fi --> vendor cloud  |
|          |                                                |
|          +--> bounded packet copies --> telemetry parser |
|                                           |               |
|                                           +--> web UI     |
|                                           +--> MQTT       |
|                                           +--> HA discovery|
+----------------------------------------------------------+
```

The ESP32 runs in AP+STA mode:

1. Its station interface connects to the home Wi-Fi network.
2. Its access-point interface provides Wi-Fi, DHCP, DNS, and a gateway to the dongle.
3. lwIP IPv4 NAPT forwards normal dongle traffic through the station interface.
4. A bounded copy of relevant packets is sent to the decoder. Packet inspection does not modify the forwarded application payload.
5. Valid decoded readings are stored per dongle and made available to the local web interface and MQTT publisher.

If the decoder queue is full or a packet is unsupported, the observation can be dropped without deliberately stopping normal forwarding. IPv6 is disabled so that per-dongle forwarding policy cannot be bypassed over IPv6.

## Functionality

### Local web interface

The web interface is embedded in firmware and does not require a CDN or Internet connection. It provides:

- A dashboard for Wi-Fi, Internet reachability, decoder, MQTT, heap, uptime, and firmware status.
- Dongle configuration and status.
- Searchable and sortable decoded data points.
- MQTT and Home Assistant settings.
- Network, access-point, administrator, and operational settings.
- Diagnostics for packet processing, DNS, offline fallback, memory, tasks, and connected clients.
- Firmware upload to the inactive OTA partition.
- Optional live PCAP download when debug mode is enabled.
- Configuration import and export for operational settings. Passwords and other secrets are excluded from exports.

The web assets, including the project logo, are gzip-compressed and served directly from flash.

### Dongles and inverter mappings

Up to eight dongle slots can be configured. Each slot has:

- A dongle IPv4 address.
- A name used in the web interface, MQTT topics, and Home Assistant device name.
- A decoder profile.
- A separate cloud-forwarding setting.

Telemetry values, freshness, change detection, serial number, and publish timing are tracked per source IP. Field names, units, JSON keys, and enabled/disabled state are shared field definitions.

Unmapped dongles can appear in diagnostics but are not published to MQTT. A mapping does not reserve a DHCP address, so dongle addresses should remain stable. If different inverter serial numbers appear from the same source IP, publication for that source is blocked to avoid combining data from different inverters.

### Telemetry decoding

The parser supports the configured 292-byte, 302-byte, and 306-byte packet profiles. It performs bounded TCP stream handling, length checks, field scaling, signed-value handling, and plausibility checks before accepting a dataset.

Data points and their byte offsets are defined only by self-contained JSON files; no measurement byte offsets are hardcoded in the C firmware. A factory-fresh installation loads the embedded `packetoffset001.json`, `packetoffset002.json`, and `packetoffset003.json`. Under **Data Points → Packet-offset setup files**, each setup can be downloaded, replaced, or deleted, and additional numbered setups can be added up to slot 008. Each file contains its packet length, field definitions, scaling, formulas, registers, and byte offsets. Uploaded files are persisted in NVS and take effect immediately.

The authoritative shipped files are [`mappings/packetoffset001.json`](mappings/packetoffset001.json), [`packetoffset002.json`](mappings/packetoffset002.json), and [`packetoffset003.json`](mappings/packetoffset003.json). These exact files are embedded in the firmware image as data and parsed during default initialization. The 306-byte setup includes the evidence-backed daily/yearly counters, inverter frequency, PV4 and warning/fault words, plus the UPS/home-load split. Further candidates and limitations are recorded in [`docs/LONG1CAP_REGISTER_ANALYSIS.md`](docs/LONG1CAP_REGISTER_ANALYSIS.md) and [`mappings/sunsynk-evidence-map.json`](mappings/sunsynk-evidence-map.json).

Settings provides a Metric/Imperial preference. Temperature values remain decoded internally in Celsius, then are converted consistently for Data Points, MQTT state payloads, and Home Assistant discovery (`°C` or `°F`). Custom unit labels for other fields do not convert their numeric values. Register definitions and supporting notes are in [`main/modbus/register_map.c`](main/modbus/register_map.c) and [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

### MQTT and Home Assistant

MQTT is disabled until configured. The current transport is plain MQTT over TCP, normally port 1883; TLS is not implemented.

For a mapping named `INVERTER1`, a typical state topic is:

```text
solarproxxie/INVERTER1/state
```

The state payload is one JSON object containing the complete enabled dataset, so Home Assistant
receives every value from the same inverter snapshot together.

The firmware supports:

- Optional MQTT username and password.
- Configurable broker, port, client ID, base topic, and discovery prefix.
- Publish-on-change with minimum and maximum intervals.
- Retained state and availability messages.
- Per-inverter availability based on the configured freshness timeout.
- Home Assistant MQTT discovery with stable field and slot identifiers.
- Manual publish, discovery refresh, and connection tests.
- Testing entered broker settings without saving them.

No Home Assistant API key is needed. Home Assistant communicates through the configured MQTT broker. Topic behavior and migration details are documented in [`docs/MQTT.md`](docs/MQTT.md).

### Per-dongle cloud forwarding

Cloud forwarding can be disabled for one configured dongle without disabling the ESP32's management interface or MQTT client. The forwarding filter applies to routed IPv4 traffic in both directions, including established sessions. Other configured dongles can continue forwarding normally.

The decoder is passive in normal forwarding mode. Unknown packets are not treated as a reason to block forwarding.

### Local fallback during an Internet outage

An optional reachability check sends one ICMP probe roughly every 30 seconds. After two consecutive failures, the gateway can disable normal NAPT and answer only the observed vendor DNS names and supported TCP/51100 requests locally. One successful probe restores normal forwarding.

This fallback implements only the captured read-only polling, redirect, acknowledgement, and clock behavior needed for supported telemetry flows. It is not a general replacement for the vendor cloud. ICMP failure is also not proof that the vendor service is unavailable; the selected probe host may simply block or lose ICMP.

### Live packet capture

Debug mode adds a Live PCAP page. Packet records are streamed to the browser through a bounded queue and are not written to flash. A slow browser can miss packets. The capture does not decrypt encrypted traffic and may contain serial numbers, addresses, or other private identifiers.

## Dongle web interface isolation

The tested Inteless dongle exposes a plain-HTTP management interface. The observed interface did not request a username or password and exposed device information, Wi-Fi/network configuration, reboot, and firmware-upload controls. Its frontend showed no authentication or CSRF mechanism for those actions. Destructive upload tests were not performed, so unknown server-side checks cannot be ruled out; operationally, the interface should be treated as unprotected.

SolarProxxie therefore does not expose the dongle web interface permanently to the home network. By default, the AP-side dongle is behind NAPT and there is no inbound port mapping to its HTTP server.

An authenticated SolarProxxie administrator can temporarily enable a mapping from:

```text
http://<ESP32-home-IP>:8080/  ->  http://<selected-dongle>:80/
```

The mapping:

- Can target only a configured dongle that is currently connected to the ESP32 access point.
- Is disabled by default.
- Expires after 15 minutes.
- Closes when the administrator disables it or logs out.
- Closes when the dongle disconnects or changes MAC address.
- Closes when the ESP32 loses its upstream connection, NAPT is disabled, or the ESP32 restarts.

This is exposure control, not authentication added to the dongle. While port 8080 is enabled, any device that can reach that ESP32 address may be able to open the dongle UI and use its unauthenticated configuration or firmware-upload functions. SolarProxxie does not inspect, authorize, or rewrite requests sent through this temporary port mapping. Enable it only when needed, use it from a trusted LAN, close it afterward, and never forward port 8080 from the Internet.

More observations are recorded in [`docs/DONGLE_WEB_SECURITY.md`](docs/DONGLE_WEB_SECURITY.md).

## Security model and limitations

SolarProxxie is designed as a local network appliance. It should not be directly exposed to the Internet.

### Implemented controls

- The normal ESP32 access point uses WPA2-PSK and supports PMF where compatible.
- Administrator passwords are 12-128 bytes and stored as salted PBKDF2-HMAC-SHA256 hashes, not plaintext.
- New hashes currently use 2,000 iterations for acceptable response time on the ESP32. This is a limited embedded cost and is not strong protection against offline guessing; use a long, unique password.
- Legacy 100,000-iteration hashes can be verified and are migrated after a successful login.
- Login attempts have increasing global backoff.
- Sessions use random 32-byte tokens, are tied to the peer address, and expire after 30 minutes of inactivity.
- Session cookies use `HttpOnly` and `SameSite=Strict`.
- State-changing API calls require a separate CSRF token.
- The browser interface escapes editable and network-provided text before inserting it into HTML.
- Content Security Policy, frame blocking, MIME sniffing protection, request-size limits, socket limits, and bounded queues reduce common web and resource-exhaustion risks.
- Normal SolarProxxie OTA upload requires an authenticated administrator session and a valid CSRF token.
- The setup API is accepted only on the physical setup AP interface.
- A corrupt or unsupported saved configuration does not silently open an unattended setup portal; physical recovery is required.

### Important limitations

- The management UI uses HTTP, not HTTPS. Credentials and session data are not protected from a device capable of observing the local network.
- MQTT uses unencrypted TCP. Broker credentials and telemetry can be observed on the network.
- Wi-Fi and MQTT credentials must be recoverable by the firmware and are stored in NVS.
- Development defaults do not enable flash encryption, NVS encryption, or Secure Boot. A person with physical flash access can recover secrets or replace firmware.
- The supplied OTA handler validates ESP image structure/checksum and partition size. It does not establish who produced the image unless ESP-IDF signed-image/Secure Boot provisioning is separately configured.
- Clients connected to the ESP32 access point are not mutually isolated.
- The temporary dongle UI port mapping has the dongle security limitation described above.
- Telemetry authenticity is not cryptographically verified and must not be treated as a safety signal.
- Packet captures and diagnostic data can contain sensitive identifiers.

The partition table reserves an NVS keys partition, but that reservation alone does not enable encryption. Enabling Secure Boot or flash encryption burns eFuses and changes recovery procedures; follow Espressif's provisioning documentation and test on a spare board first. See [`docs/SECURITY.md`](docs/SECURITY.md).

## Initial setup

1. Flash the complete project to the ESP32 over USB.
2. On first boot, connect to the temporary open `SolarProxxie-Setup` access point.
3. Open `http://192.168.4.1` if the captive portal does not open automatically.
4. Configure home Wi-Fi, DHCP or static addressing, the dongle access-point subnet and credentials, and the administrator account.
5. Test the home Wi-Fi settings and save. The ESP32 restarts.
6. Reconnect the phone or computer to the home network and open the ESP32's home-network address.
7. Use the dongle's normal Wi-Fi setup procedure to connect the dongle to the SolarProxxie access point.
8. Add the dongle's assigned IP address under **Dongles**, select the correct decoder profile, and confirm that the vendor application still receives data before enabling other functions.
9. Configure MQTT only after local telemetry has been checked against known inverter values.

The setup access point is intentionally open and expires after 15 minutes. After configuration, Setup Mode requires physical access: restart the ESP32 and press BOOT during the first five seconds.

The default normal AP subnet is `192.168.50.1/24`, with DHCP addresses from `.10` through `.50`. The home and dongle subnets must not overlap.

## Firmware update and recovery

The **Firmware** page accepts only the application image:

```text
build/SolarProxxie.bin
```

Do not upload a bootloader, partition table, or merged flash image through the web UI. The image is written to the inactive OTA slot and selected for the next boot. ESP-IDF rollback is enabled; a running image is marked valid after 60 seconds.

To factory-reset the stored configuration, hold BOOT for ten seconds while the application is running. This erases the SolarProxxie NVS configuration. Do not hold BOOT while applying power unless ROM download mode is intended.

## Build and flash

Install ESP-IDF 5.4.2 and open its configured terminal:

```sh
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the actual serial port. Linux devices commonly appear as `/dev/ttyUSB0` or `/dev/ttyACM0`.

The supplied partition table contains 64 KiB NVS, OTA metadata, two 1,920 KiB application slots, and a reserved NVS keys partition. Detailed Windows and Linux instructions are in [`docs/INSTALL.md`](docs/INSTALL.md).

## Tests

```sh
python tools/test_host.py
npm ci --ignore-scripts
npm run test:web
```

Additional packet-decoder and fixture commands are documented in [`docs/TESTING.md`](docs/TESTING.md). Browser DOM tests verify application behavior but do not replace a visual browser check or ESP32 HTTP integration test.

## Known scope and limitations

- Only the listed Inteless/Sunsynk single-phase profiles are implemented. The research map documents a PDF-derived string-inverter three-phase candidate, but native three-phase hybrid packet offsets and other packet sizes must not be assumed compatible.
- Packet and field mappings come from reverse engineering and reference comparison, not a vendor contract.
- TCP reassembly is intentionally bounded. Out-of-order gaps, fragmentation, oversized frames, or queue pressure can cause missed observations.
- AP and STA share one 2.4 GHz radio and channel.
- DNS proxy replies are bounded to 512-byte UDP DNS behavior; TCP DNS fallback is not implemented.
- The device is a monitor and gateway, not an inverter controller.
- OTA, NVS power-loss behavior, long-duration heap stability, multi-dongle behavior, and every supported dongle firmware should be tested on the intended hardware before unattended use.

## Repository layout

```text
main/config/       configuration, validation, NVS migration
main/network/      Wi-Fi, DHCP/DNS, reachability, fallback, dongle UI mapping
main/protocol/     Inteless packet parsing and stream handling
main/modbus/       field definitions and Modbus decoding support
main/router/       packet observation and forwarding policy
main/mqtt/         MQTT publication and Home Assistant discovery
main/security/     password hashing and constant-time comparison
main/web/          HTTP API and embedded asset server
main/ota/          authenticated application OTA handling
web/               browser UI assets
tests/             host and browser tests
tools/             build, fixture, capture, and decoder utilities
docs/              protocol, API, architecture, security, and test notes
```

## Documentation

- [Installation](docs/INSTALL.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Protocol](docs/PROTOCOL.md)
- [MQTT and Home Assistant](docs/MQTT.md)
- [HTTP API](docs/API.md)
- [Security](docs/SECURITY.md)
- [Testing](docs/TESTING.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)

## License

SolarProxxie is licensed under GPL-3.0-or-later. See [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md) for license and third-party attribution details.
