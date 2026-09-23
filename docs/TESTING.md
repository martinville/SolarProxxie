# Validation and release acceptance

## Background login acceptance

Web tests hold the login response pending to check progress text, duplicate-submit
prevention, retry after failure, password-field clearing and successful navigation.
Hardware checks still required:

- [ ] Login with the existing saved password after OTA; no setup/reset required.
- [ ] Record Diagnostics → Sign-in performance and compare with the previous delay.
- [ ] While one client signs in, another authenticated client can refresh status.
- [ ] Concurrent sign-ins receive 429; wrong credentials retain the existing backoff.
- [ ] Repeated login/logout and disconnect-during-login cycles do not leak sockets
      or heap. Allocation/task/queue failure paths release the async request.
- [ ] Check normal operation and temperature/power at the new 240 MHz CPU setting.

## Dongle GUI access acceptance

For the new per-dongle controls, hardware checks are still required:

- [ ] Configure two dongles with different packet layouts; verify each source
      decodes and publishes only its own values, including after reboot and OTA.
- [ ] Confirm old version 2 NVS settings migrate with all names, credentials and
      the former shared layout preserved for existing dongles.
- [ ] Read `devinfo` for each mapped, connected dongle and verify the serial,
      Register Key and versions against its own web page; no key appears in MQTT,
      logs or configuration export.
- [ ] Confirm Reboot affects only the selected dongle, the other keeps reporting,
      and the selected one reconnects. A disconnected or unmapped target must be
      rejected. Do not test reboot during a dongle firmware update.

- [ ] Empty and unknown-IP lists show the appropriate instructions and MACs.
- [ ] After an ESP32 restart, a still-associated dongle that retains its IP is
      discovered from AP ARP traffic, and can be selected for passthrough.
- [ ] A static-IP AP client can be resolved after it communicates; unrelated
      ARP entries and disconnected clients never appear as selectable devices.
- [ ] The full STA port-8080 URL and access steps are visible, and Refresh devices
      updates the list without switching the browser's Wi-Fi network.

`python tools/test_dongle_gui.py` compiles the actual mapping module against SDK
substitutes and checks target restrictions, replacement, expiry, disconnect and
IP/MAC changes, setup/cloud gating, missing NAPT tables and failures. It does not
simulate packet forwarding. Web DOM tests cover the enable/close controls and
the home-network URL. Complete these checks on hardware:

- [ ] From home Wi-Fi, sign in and enable access for the dongle. Open the link
      on port 8080; verify its login, assets, navigation and form submission.
- [ ] Verify actual dongle firmware has no incompatible absolute URLs/redirects.
- [ ] Verify access closes after 15 minutes, Close access, logout, reboot,
      cloud disable, upstream disconnect and dongle disconnect/IP/MAC change.
- [ ] Confirm a rejected target cannot replace the current mapping and another
      connected client can be selected intentionally.
- [ ] Confirm the normal dashboard, MQTT and Sunsynk cloud updates continue.
- [ ] Reuse a test session cookie from a different client's IP; management API
      must reject it. Re-authenticate after moving the browser to another network.

Visual browser and end-to-end dongle checks remain pending when no browser or
ESP32/dongle is available. A passing build is not hardware compatibility evidence.

## Automated checks implemented

Activate ESP-IDF or set `IDF_PATH`. On Windows a local Zig compiler avoids requiring
Visual Studio; on Linux set `CC=cc` to use GCC/Clang, or install Zig in the environment.

```sh
python -m pip install ziglang
python tools/test_host.py
python tools/make_fixtures.py
python tools/packet_decoder/decode.py tests/captures/synthetic292.hex
python tools/packet_decoder/decode.py tests/captures/synthetic302.pcap --layout 302
npm ci --ignore-scripts
npm run test:web
idf.py build
idf.py size
```

`tools/test_host.py` builds actual project C code with a small host NVS/FreeRTOS/random
test shim and the ESP-IDF-bundled mbedTLS/cJSON source. No firmware code uses those
test shims. The host random shim is deterministic and **must never enter firmware**.

Tests cover exact Inteless lengths, all shorter truncations, invalid dates/leap years,
SOC/temperature/grid-voltage fixtures, signed/unsigned 16/32-bit values, scaling,
bit masks, RTU CRC errors, TCP segmentation/coalescing/retransmissions, 50,000
deterministic malformed inputs, IPv4/mask validation, network overlap, normal-mode
critical-field rejection, failed config saves, round-trip NVS reads, unknown versions,
secret-free export, password replacement, an independently calculated PBKDF2 result,
password/salt mismatch, MQTT topic validity, discovery identity stability and PCAP headers.

Web tests run the actual application JS in a DOM environment with a mock data API.
They exercise all pages, setup steps, XSS escaping and absence of critical normal-mode
inputs. They **do not** prove rendered appearance, real browser behavior or HTTP
authorization on the ESP32. The C configuration tests independently cover its boundary.

The native packet CLI is built from the same Inteless and Modbus C files as firmware.
Fixtures are synthetic and clearly labelled. Contributing real sanitized captures
is the next step for protocol compatibility verification.

On managed Windows machines, Application Control can block a newly built host EXE
even though compilation succeeds. Do not disable that protection as part of this
workflow. Record the blocked check and run it in an approved development environment
or the provided Linux CI job. A previous passing executable does not validate a
newer build. The firmware cross-compile is independent of host EXE execution.

## Hardware milestone 1 — mandatory before telemetry qualification

Record board/module, flash size, supply, IDF build, home router, dongle model/firmware,
inverter model and actual timestamps. All checks below remain **pending** until run
on hardware; no attached ESP32 was available during initial implementation.

- [ ] First flash boots and setup appears without stored config.
- [ ] Setup scan shows SSIDs, RSSI and security; Wi-Fi test succeeds.
- [ ] Static/DHCP settings persist across a normal reset and unplug/replug.
- [ ] Laptop on protected AP receives `.10`–`.50`, correct mask, gateway and DNS.
- [ ] Laptop can resolve DNS and reach the Internet through AP → STA → NAPT.
- [ ] Dongle associates with this AP and receives a lease.
- [ ] Sunsynk app continues updating for at least three normal reporting cycles.
- [ ] Reconnect upstream router, change radio channel and restore WAN; dongle recovers.
- [ ] Reboot/disable MQTT broker and leave HA offline; cloud updates continue.
- [ ] Enable/stop/download capture during traffic; cloud updates continue.
- [ ] Start a streaming PCAP from Packets; verify the browser saves a
  readable file after Finish download, and that navigation
  remains responsive during the download. Check missed-packet counts and test a
  browser disconnect, a second download attempt, and low-memory failure.
- [ ] Disable forwarding; existing and new dongle cloud flows stop in both directions.
- [ ] Re-enable forwarding; dongle and cloud recover without factory reset.
- [ ] A DHCP subnet overlap fails safely and gives a useful error.

## Hardware milestone 2 — observation and data

- [ ] Record matching real 292/302-byte Inteless frames; select the matching layout.
- [ ] Compare each enabled field against an independent inverter observation.
- [ ] Unknown/truncated frames do not stop cloud traffic.
- [ ] Capture PCAP opens in Wireshark; uptime and post-DNAT limitations are understood.
- [ ] MQTT discovery forms one device per mapped slot; renaming a slot keeps its entity identity stable.
- [ ] Disabled fields are removed; no fabricated/unobserved fields are advertised.
- [ ] Unplug dongle; inverter availability expires while gateway remains online.
- [ ] Broker outage and restart recover; publication remains paced and bounded.
- [ ] Measured publish successes reflect broker acknowledgements, not HA timing.

## Security, OTA and endurance acceptance

- [ ] Unauthenticated data/config/OTA API requests are rejected in normal mode.
- [ ] Wrong/missing CSRF is rejected; logout and expiry invalidate the session.
- [ ] Direct critical-setting JSON requests are rejected outside physical setup.
- [ ] Setup API is inaccessible via the STA interface during Wi-Fi tests.
- [ ] First-boot and BOOT-forced setup expire after 15 minutes.
- [ ] Long BOOT hold resets; short press does not erase; wrong-button behavior documented.
- [ ] Valid OTA succeeds and preserves config; malformed and oversize images fail.
- [ ] Interrupt OTA upload/power before selection; old image still boots.
- [ ] Boot a deliberately failing candidate on a spare board; rollback works.
- [ ] Interrupt config writes repeatedly; NVS retains a valid record or enters recovery.
- [ ] Run at least 48 hours with continuous dongle use, periodic GUI, and broker outages.
- [ ] Log minimum heap/largest block and task stack margins; no progressive loss occurs.
- [ ] Confirm original ESP32 routing throughput and latency are sufficient for this dongle.

## Development replay

`menuconfig → SolarProxxie → TEST_PACKET_REPLAY` injects synthetic telemetry every
ten seconds. `synthetic_replay` is true in status and the dashboard labels TEST REPLAY.
It is not enabled by sdkconfig.defaults. Build this only for offline UI/MQTT development;
never confuse replay metrics with real inverter observations.

## Release rule

A successful compile and host tests are necessary but do not certify a hardware
gateway. Mark a release hardware-qualified only after recording the above results.
Do not turn unknown protocol details into “supported” merely to make a checklist green.

## Multiple dongle acceptance

- [ ] Upgrade a v1 board without erasing; confirm AP/STA credentials and admin login survive.
- [ ] Map two actual dongles to INVERTER1 and INVERTER2; verify correct serials and independent values in GUI and MQTT.
- [ ] Confirm separate HA devices and effective topics; review automation migration from old single-device entities.
- [ ] Stop one dongle and verify only its freshness expires; the other keeps publishing.
- [ ] Exercise four dongles with the same configured protocol layout, MQTT QoS 1 delays and heap/stack monitoring.
- [ ] Rename a slot while connected: old mapped topics are cleared and the device retains its per-slot unique IDs.
- [ ] Remove/disable one mapping; its entities disappear without affecting other mappings.
- [ ] Repeat mapping edits with broker offline and document any retained topics needing manual cleanup.
- [ ] Reboot after a configuration save and confirm all mappings persist.
- [ ] Confirm serial-conflict indication prevents one source IP silently mixing different inverter serials.
