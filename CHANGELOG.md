# Changelog

SolarProxxie follows [Semantic Versioning](https://semver.org/).

## 1.2.0 - 2026-09-23

- Add a GitHub Pages project site matching the embedded web interface.
- Add a Web Serial full-firmware installer for the original 4 MB ESP32 target.
- Add authenticated checks and confirmed HTTPS OTA installation from published
  `martinville/SolarProxxie` GitHub Releases.
- Add automated Pages deployment and tagged firmware-release workflows.
- Embed the project logo in the ESP32 header and login screen and expose the running
  firmware version before login.
- Document the temporary dongle web-interface mapping and its unauthenticated
  configuration and firmware-upload risk.

## 1.1.2 - 2026-09-23

- Correct the captured cloud-login timestamp from BCD to ordinary binary fields.
- Synchronize the ESP32 clock with SNTP and withhold invalid 1970-era login clocks.
- Preserve the spoofed DNS hostname role when accepting its local TCP connection.
- Preserve one telemetry and one redirect session per dongle when a reconnect reuses a
  cached DNS result, preventing both sockets from being misclassified as telemetry.
- Clear stale local-cloud sockets and DNS-role hints when a dongle leaves the ESP32 AP.
- Return the captured 34-byte `ukiot.sunsynk.net:51100` redirect for
  `iot.e-linter.com` while keeping the telemetry endpoint's 11-byte acknowledgement.
- Acknowledge the dongle's optional type `0x04` cloud request instead of closing its
  telemetry session and triggering a Wi-Fi reconnect.
- Expose clock readiness in offline diagnostics.

## 1.1.1 - 2026-09-23

- Prevent rejected offline-cloud sessions from exhausting the shared lwIP socket pool.
- Limit each dongle to its two observed cloud roles and replace stale retry sessions.
- Add TCP keepalive and idle cleanup to the local responder.
- Report whether TCP/51100 is actually listening instead of whether its task exists.
- Increase the bounded socket pool to preserve capacity for HTTP, MQTT, DNS and probes.

## 1.1.0 - 2026-09-23

- Add the bounded local cloud fallback for configured dongles.
- Add reachability-controlled transitions between normal forwarding and local fallback.
- Add per-dongle cloud-forwarding controls and offline status in the web interface.
- Centralize the release version in the root `VERSION` file.
- Show the running firmware version and build identity in the web interface.

## 1.0.0 - 2026-09-21

- Initial software-validated ESP32 release with local management, passive telemetry
  decoding, MQTT/Home Assistant discovery, multi-dongle mappings, OTA and PCAP capture.
