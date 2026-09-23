# Build and validation report

## Role-aware offline handshake - 2026-09-23

Current version: **1.1.2**. Current artifact: **997,504 bytes**, SHA-256
`e1f3034714ab0ade113247c04b08d538a57e7fed0d2aa4af68f020f7f6c957c6`.

Corrects two capture-proven handshake mismatches. Login clock fields are ordinary
binary values rather than BCD, and SNTP supplies a valid clock instead of the ESP32
boot epoch. DNS interception now records whether a connection was initiated for the
telemetry or redirect hostname. The redirect role receives the captured 34-byte
`ukiot.sunsynk.net:51100` response; the telemetry role keeps its captured 11-byte
acknowledgement. Offline diagnostics expose clock readiness.

Exact host assertions cover the login trailer and redirect payload. The complete
shared-C suite, dongle GUI C suite, web DOM tests, ESP-IDF compile/link and partition
check pass. The application leaves 968,576 bytes (49%) free in each OTA slot. A real
dongle must still confirm that DNS/TCP ordering and the UTC+2 telemetry clock match
its firmware and configured inverter timezone.

## Offline socket-exhaustion fix - 2026-09-23

Current version: **1.1.1**. Current artifact: **996,464 bytes**, SHA-256
`b580f0ef3df173cf9ed2c1f0e63dc4433f2cc972893b13c262d6e2b2d136fc76`.

The rejected offline handshake caused repeated dongle TCP sessions to occupy the
12-entry lwIP socket table, after which the HTTP server could not accept clients
and the reachability task could not create its probe socket (`errno 23`). The local
responder now permits the two observed cloud roles per dongle IP, replaces the
oldest same-IP session on a retry, enables TCP keepalive and receive timeouts, and
closes sessions idle for two minutes. The bounded socket pool is 24 so HTTP, MQTT,
DNS and reachability retain capacity. Diagnostics now report actual TCP/51100
listener state rather than merely reporting that its task exists.

ESP-IDF compile/link and partition checks, the complete shared-C host suite,
dongle GUI C suite and web DOM tests pass. The application leaves 969,616 bytes
(49%) free in each OTA slot. This fixes management-plane starvation; it does not
claim that the still-incomplete offline cloud handshake is hardware-qualified.

## Bounded offline cloud fallback - 2026-09-23

Current version: **1.1.0**. Current artifact: **996,128 bytes**, SHA-256
`b5070ca1dee93aa4b023f860bf7420c564c1cc74812a9c89492df1069ddd1da7`.

The gateway now provides a bounded, read-only local fallback for configured
dongles when upstream reachability fails twice or when cloud forwarding is
disabled for that dongle. It intercepts only A/IN queries for the three observed
vendor hostnames, accepts TCP/51100 only from configured AP clients, and returns
the capture-derived login clock response, polling map and message-number-matched
acknowledgements. One successful reachability probe restores normal NAPT/cloud
forwarding. The responder does not initiate traffic or implement inverter writes.

The transition and offline-startup captures confirm that DNS replies alone do not
produce a telemetry session and that the dongle repeats its login/control exchange
after forwarding returns. The shared-C host suite (including 50,000 malformed
inputs and exact emulator reply tests), dongle GUI C suite, four Python replay
tests, web DOM integration, ESP-IDF compile/link and partition-size check pass.
The application leaves 969,952 bytes (49%) free in each OTA slot. The semantic
release version is sourced from `VERSION`, embedded by ESP-IDF, used by the release
packager, shown subtly in the persistent web footer, and detailed on the About page.

The firmware remains software-validated, not hardware-qualified. A real dongle
must still verify the emulated startup sequence, telemetry cadence, automatic
online/offline transitions, cloud recovery and behavior across firmware revisions.

## Consolidated Data Points interface - 2026-09-21

Current artifact: **986,816 bytes**, SHA-256
`01d8e174be0fa4824301dfb6cfe57a75c7a2c14719c414ce60d2f4febe206318`.

Removed the duplicate Inverters navigation page, its live-refresh/rendering path,
the second per-dongle packet-layout selector and the obsolete
`/api/inverter[/slot]` HTTP alias. Packet layout is now configured only as part of
each dongle mapping under Dongles. The shared data-freshness timeout is
now placed below the Data Points explorer. Settings guidance, API documentation,
protocol instructions and regression tests were updated to match.

A named-function reference audit found no unreferenced JavaScript functions after
the removal, and the obsolete page, form and endpoint have no remaining source or
documentation references. Compared with the preceding 987,264-byte image, the
firmware is **448 bytes smaller**. Flash code fell by 76 bytes, flash data by 368
bytes, and the embedded JavaScript gzip fell by 342 bytes. Static DRAM and IRAM
usage are unchanged. The application leaves 979,264 bytes (50%) free in each OTA
slot.

Web DOM tests, the complete shared-C host suite, dongle GUI C tests, four replay
tests, the ESP-IDF build and partition-size check pass. Hardware/browser acceptance
remains pending.

## Per-dongle layouts, identity and controls - 2026-09-21

Historical artifact: **987,264 bytes**, SHA-256
`ed1483d424b81b5a412a338a4be48605ab5a5304063466b7eee3a58b80802931`.

Each configured dongle now has its own 292-, 302- or 306-byte packet layout.
Configuration schema 3 migrates version 2's shared layout into every dongle slot
while preserving the frozen version 1/2 prefixes and saved credentials. Gateway
controls can display a mapped, connected dongle's serial and Register Key from a
strictly parsed cloud-login packet and can submit the dongle's confirmed reboot
request. Identity values remain in RAM and are not exported, logged or published
to MQTT. Reboot and identity access require an authenticated administrator session,
CSRF validation, a configured mapping and a currently connected AP client.

The release-wrap-up rerun found and restored the omitted dongle web-passthrough
renderer in the source asset before rebuilding. Web DOM tests now cover the combined
mapping, identity, reboot, passthrough and per-layout interface. The shared C host
suite, dongle GUI C suite, four replay-tool tests, ESP-IDF build and partition-size
check all pass. The application leaves 978,816 bytes (50%) free in each OTA slot.
The embedded gzip asset contains one copy of the restored renderer.

The firmware and source are software-validated but not hardware-qualified. Real
ESP32/dongle checks for per-source decoding, identity comparison, targeted reboot,
reconnection, MQTT/Home Assistant and browser behavior remain mandatory.

## Six 306-byte energy mappings - 2026-09-17

Current artifact: **970,064 bytes**, SHA-256
`96abe6a7dd08d01f7cb166bdee2e7f4d0e9bfb6f0d8766872ee01529fc986e46`.

Added daily battery charge, grid import, load and PV energy, plus monthly PV and
load energy, as appended per-inverter entities. The offsets apply only to
306-byte frames; older layouts leave them unavailable. Existing entity indices
and saved settings remain stable. Host tests cover scaling, layout isolation,
MQTT topics, discovery and migration. ESP-IDF build and web DOM tests passed,
with 51% of the OTA slot free. The separate native PCAP replay executable was
blocked by local application-control policy; raw PCAP words were compared in
the capture analysis. Hardware and broker validation remain pending.

## Packet details modal - 2026-09-17

Historical artifact: **968,880 bytes**, SHA-256
`de811cdbf240ebff0fd4cce9df1aca80e41206a9d7b13d7407d44cd51c506318`.

Captured packets now uses a compact table. Its Details button opens a modal
identifying the selected packet, and the full captured IPv4 hex/ASCII dump is
visible immediately. The dialog belongs to the Captured packets section and
closes when the selection is filtered out or capture is cleared. Web DOM tests
and ESP-IDF build passed (51% OTA slot free); browser and hardware review are
pending.

## Default collapsed sections - 2026-09-17

Historical artifact: **968,384 bytes**, SHA-256
`6e6f430d72fe6f4f3b72fa5dddf9584b7162691d7a39f0086a17845ac74b6c6e`.

The first eligible section on each navigation page starts expanded; every later
collapsible section starts collapsed. User choices continue to survive navigation
and live refresh. Individual readings and the Data Points explorer remain open.
Web DOM tests cover the initial state and refresh preservation. ESP-IDF build and
partition checks passed (51% OTA slot free); browser visual review is pending.

## Diagnostics full-width cards - 2026-09-17

Historical artifact: **968,368 bytes**, SHA-256
`0694c44457597208a4e3e764a32797344109926dd3112c76089e52a0f7198276`.

System, Packet pipeline and DNS proxy now stack vertically across the Diagnostics
content width, in that order. The former two-column grid could leave a large gap
above DNS when the pipeline card grew. Web DOM tests and ESP-IDF build passed;
visual device/browser review remains pending.

## Collapsible navigation sections - 2026-09-17

Historical artifact: **968,448 bytes**, SHA-256
`e6848797aa6022cdf48eb12c9167e3fb3d2087e408f2a7663f7120ef51928870`.

Full section cards now have accessible top-right arrow controls. Their independent
open/closed state survives periodic page refreshes and navigation. Individual
inverter reading tiles, AP device cards, and the Data Points explorer stay open.
Web DOM tests cover eligibility, aria state, refresh persistence and retained
forms/actions; ESP-IDF build and partition checks passed (51% OTA slot free).
Browser visual review and live hardware acceptance remain pending.

## User-controlled live PCAP download - 2026-09-17

Historical artifact: **967,600 bytes**, SHA-256
`9acf8beb9a81a4d43a484f0404a3be04b98bb8febe4a1a9c121cd8c4e330eb0c`.

The long PCAP download no longer has a 20-minute timer. Start live PCAP download
streams until Finish download is clicked or the browser connection ends. The file
is named solarproxxie-live.pcap. A nonblocking socket check detects disconnects
when packets are idle. Only one stream can run at a time; its bounded queue and
missed-packet counter remain in place. Web DOM tests and the ESP-IDF build passed.
A live browser/device run is still needed to confirm completion and cancellation.

## Browser streamed PCAP download - 2026-09-17

Historical artifact: **967,584 bytes**, SHA-256
`88c13a4670dd5845eb107d34c4806dc0a1873ca219182c069495debd065f0f45`.

Packets page now offers a single-click 20-minute PCAP download. An asynchronous
HTTP worker writes live PCAP records to the browser from a bounded 12-packet RAM
queue; the 32-packet inspector remains separate. Finish long download ends the
file early. Status exposes streamed and missed packet counts. Only one long
capture runs at a time, requires sign-in and Debug Mode, and stops on disconnect
or after 20 minutes. Slow clients can drop records; PCAP remains best-effort.

Web DOM tests and ESP-IDF build/partition checks passed (51% OTA slot free).
Async HTTP and a full 20-minute transfer still require live device/browser testing.
The packaged firmware contains this revision.

## Captured 306-byte profile and temperature fix - 2026-09-17

Historical artifact: **966,128 bytes**, SHA-256
`03883a46e24143b5187be5400152167a8a8d466d0298465ad562a8e8ada64262`.

Adds explicit 306-byte selection, full-record stream handling and suppression of
unverified BMS fields for this profile. Main temperatures and UPS/Home remain valid.
MQTT withdraws unavailable fields and clears their retained states. No Fahrenheit
conversion was added: native Celsius metadata was confirmed correct.

Host tests, web DOM tests and ESP-IDF build passed. Native replay of the second
capture succeeded: DC 51 C, AC 43 C, battery 22.3 C, UPS 634 W, Home 20 W, no BMS
values. Tests cover negative temperatures, Celsius discovery, TCP split at 302,
duplicates and coalesced 306-byte frames. Hardware/broker checks remain pending.
After OTA, select Configuration > Inteless reference layout > 306 bytes.

## UPS and Home Load Power - 2026-09-17

Historical artifact: **965,872 bytes**, SHA-256
`b4fff52288754e142257f54ee36110a6ab464a170d1f5e1ec05af8877a504071`.

Adds per-source ups_power (302-profile offset 242, user-confirmed capture) and
home_load_power (non-negative CT minus L1). Existing entity indices are preserved;
empty new settings slots migrate on boot. The 292 profile reports UPS unavailable.
Host tests, web DOM tests and ESP-IDF build passed. Raw capture inspection matches
607 W UPS and 18 W Home. Native capture replay was blocked by Windows Application
Control even after escalation; hardware verification remains pending. Existing
306-byte BMS interpretation is not fixed by this change.

## Compact dashboard, gateway and diagnostics - 2026-09-17

Historical artifact: **965,152 bytes**, SHA-256
`f18ae2ee53dcc53321aa975f33ebf2e3f1f050994f374660f59c7447310cedaf`.

Dashboard ends with a compact per-dongle table. Ages count every second between
status polls, with independent waiting/fresh/stale/conflict states. Gateway shows
AP clients as labelled cards and TCP/IP observations as compact table rows.
Diagnostics uses structured System, DNS and Packet pipeline cards, preserving
nested source details under each named inverter.

Web DOM tests and ESP-IDF build/partition checks passed (51% OTA slot free).
Tests cover table placement, ticking age, waiting sources, nested inverter records,
structured device fields, endpoints and existing escaping/navigation behaviour.
Release archives and packaged assets verified. Browser visual and hardware checks
remain pending.

## SolarProxxie product rename - 2026-09-17

Historical artifact: **962,704 bytes**, SHA-256
`696c0161a839f8354ad612fc57a1b9bc0870829ad3cba1fe539aaac519dd238c`.

Product/UI branding, boot banner, CMake project, firmware filename, release archives,
HA manufacturer/model, default AP/MQTT names, downloads, package metadata, CI and
documentation now use SolarProxxie (lowercase solarproxxie for MQTT namespaces).
Only legacy-name migration and its regression tests retain the old product name
in maintained source. Saved factory-default names migrate on boot; custom names
including BOB4, credentials and stable HA device IDs are preserved. Users with the
old factory AP name must reconnect dongles to SolarProxxie. Old default MQTT base
subscriptions must use solarproxxie; HA discovery updates its topics automatically.
The workspace directory and user-supplied capture filenames are not renamed.

Web DOM tests, shared C host tests including legacy-default/custom-name migration,
and ESP-IDF build/partition checks passed (51% OTA slot free). Release archives and
manifest hashes were verified. Live hardware and browser visual checks remain pending.

## Multiple named dongles - 2026-09-17

Historical artifact: **962,560 bytes**, SHA-256
`d72ccbbabaab9a5994289fd4067006b9b46917b4e3c3293a92755f7f84c6687c`.

Four IP/name mapping slots isolate source telemetry, freshness, MQTT change tracking
and state topics. Topics are `<base>/<name>/<field-suffix>`, with legacy default
prefixes removed during topic construction. Home Assistant uses one device per
mapping slot and stable slot/field unique IDs. Unmapped sources are visible but
not published. Conflicting inverter serials at one IP block its publication.
Gateway includes the mapping form; Inverter and Data Points have slot selectors;
Dashboard and Diagnostics show independent sources. Shared field customisations
and the global protocol layout remain explicit in the interface and documentation.

Configuration schema 2 has a frozen v1 reader and a checked unchanged prefix.
Host tests verify v1 NVS loading without credential loss and v2 mapping persistence.
Existing users must assign IPs after upgrade; new per-slot HA entities replace
single-device discovery, so automations may need updating. DHCP reservations are
not created by the mapping. Older firmware cannot read a saved v2 record.

Validation passed:
- Shared C host suite: four isolated sources, freshness, serial conflicts, name/IP
  validation, distinct topics/discovery IDs, rename identity and NVS migration.
- Web DOM suite: mapping submission/draft preservation, inverter selection,
  effective topics, older-response rejection and previous UI regressions.
- ESP-IDF build, link and partition checks (51% of the OTA slot free).
- Release archives: integrity and packaged image hashes verified.

Live dual/four-dongle MQTT/HA acceptance, DHCP stability, power-cut migration and
browser visual checks remain pending. These tests do not establish live broker
or radio behaviour. The source and firmware packages contain this revision.

## Debug dongle packet inspector ? 2026-09-16

Historical artifact: **958,656 bytes**, SHA-256
`04e8e493185876ca9dab3d3a81b1dac8fe5e43209a492616972e8b5be8945d87`.
Debug Packets lists all 32 retained snapshots and expands one packet at a time,
with full captured hex, payload hex/ASCII, unverified readable strings, direction,
addresses/ports, truncation, recording state and dropped-copy count. An IP filter
helps isolate the dongle. IDs remain stable until overwritten/cleared; expired
requests return 404. Capture remains opt-in and bounded at 768 bytes per packet.
No identification fields are guessed, no new protocol commands are transmitted,
and encrypted or split application messages are not decoded by this inspector.

Shared C host tests passed, including TCP/UDP boundary parsing, truncated snapshots,
fragments, malformed headers and IP padding. Web DOM tests passed for inspection,
IP filtering, clearing and inert rendering of hostile packet strings. ESP-IDF build
and partition checks passed. Real-device memory/traffic soak and browser visual
checks remain pending. Local release archives refreshed.

## SolarProxxie branding ? 2026-09-16

Historical artifact: **956,592 bytes**, SHA-256
`1bad00257853422d1366c60f545745c38cca6d613876ae5303fe83ef537471c2`.
Removed third-party branding from the web header, About/setup text, boot banner,
Home Assistant device model and default measurement names. New configurations
use `inverter/` MQTT suffixes. Saved names/topics are preserved for compatibility;
Data Points allows editing them. Technical compatibility and attribution remain.
Web DOM tests, the complete shared C host suite and ESP-IDF build/partition checks
passed. The host suite now executes successfully, superseding the historical
Windows-policy-blocked shared host result below. Native decoder compilation passed;
its separate integration runtime was not rerun. Visual and hardware checks remain
pending. Local source and firmware packages refreshed.

## Inverter freshness and dongle diagnostics ? 2026-09-16

Historical artifact: **956,912 bytes**, SHA-256
`3cc6c734b6ef19bad61a484a0de11c7248c280027f85b53490ce2407cf82f5c5`.
Dashboard and Diagnostics show seconds since the last decoder-accepted inverter
snapshot, refreshed every four seconds. Diagnostics also shows inverter serial,
dongle IP, matched current AP MAC, configured layout, valid-frame count and
fresh/stale/waiting or synthetic state. Dongle serial/model/firmware are explicitly
unavailable, rather than inferred from the inverter serial.
The Diagnostics login-duration panel is removed. Login retains the elapsed timer
and shows a short password-checking status while authentication completes.
Web tests passed for ages including zero, waiting/stale states, serial escaping,
MAC display, removal of the login panel and login guidance. ESP-IDF build and
partition checks passed. Hardware and browser visual validation remain pending.

## Charcoal GUI theme ? 2026-09-16

Historical artifact: **956,384 bytes**, SHA-256
`6c0f192242b562e7c5544252eeb37d004d780fadb83bfed81275b885b76647ca`.
Replaces the pixel theme with neutral charcoal and grey surfaces, warm champagne
controls, sage success indicators and muted coral errors. Rounded panels, cleaner
typography, restrained borders and responsive navigation replace pixel decoration.
All styling remains embedded and works offline without external fonts or images.
Web DOM tests and ESP-IDF build/partition checks passed. Browser visual verification
remains unavailable; hardware acceptance remains pending. Release packages refreshed.

## Pixel GUI theme — 2026-09-16

Historical artifact: **956,144 bytes**, SHA-256
`853aad45be0977ae146e465e3ba11faf4ae309ad32d67b66ec9cf444ad39f741`.
ESP-IDF build and partition checks passed (51% OTA slot free); web DOM tests passed.
The stylesheet uses the requested navy, pink, turquoise, yellow and white palette,
with a CSS pixel ghost, square controls, offset borders and monospace headings.
Assets remain embedded and require no external fonts or images. Responsive rules,
keyboard focus indicators and reduced-motion handling are included.
Browser discovery returned no available browser, so visual checks remain pending.
Local source and firmware packages were refreshed. Hardware acceptance remains pending.

## Gateway refresh completion — 2026-09-16

Historical artifact: **955,488 bytes**, SHA-256
`e0cf9f30c34008e1afcc8268f666e122fae3f02ec8de313ff6c9125858f8511c`.
ESP-IDF build and partition checks passed (51% OTA slot free).
The Gateway page uses one shared gateway snapshot per refresh and preserves
the selected connected device across refreshes.

Web DOM tests passed, including snapshot request count and selection retention.
The actual C dongle GUI/discovery tests passed, including DHCP/ARP resolution,
unknown IPs, interface/MAC/subnet restrictions and mapping lifecycle failures.
These checks complete validation of the edits left in the workspace on September 15.
Local source and firmware packages were refreshed to match this artifact.
Real-browser rendering and ESP32/dongle hardware acceptance remain pending;
the historical shared parser suite limitations below are unchanged.

## Device discovery and passthrough instructions — 2026-09-15

Historical artifact: **955,504 bytes**, SHA-256
`ddd95489b47a201d56ba67bb51024dd2511a8ab5504af107d678505c3b54cb6a`.
ESP-IDF compile/link/partition checks passed (51% OTA slot free). The Gateway
list preserves connected devices without known IPs and explains empty lists,
lookup errors and reconnection steps. The page shows the configured AP SSID,
exact STA port-8080 URL, access steps and a Refresh devices button.

DHCP resolution is shared by the UI and passthrough validator, with an AP-only
MAC/subnet-checked ARP fallback. C tests passed for empty/unknown-IP clients,
DHCP precedence, ARP fallback, wrong interface/MAC/subnet and stale disconnected
entries, plus the existing mapping lifecycle tests. Web DOM tests passed for
empty/unknown-IP/error states and instructions. The reported live empty list
has not been inspected on hardware, and real-dongle/visual checks remain pending.

## Background login update — 2026-09-15

Historical artifact: **954,352 bytes**, SHA-256
`cc005399ff907d1b27abf8e39e71893be94583c5f39fcfcbef3d7db23476c06e`.
ESP-IDF compile/link and partition check passed (51% OTA slot free).
CPU configuration is 240 MHz. Existing password hashes and NVS format are unchanged.

The HTTP request is retained asynchronously while a single priority-1 worker
verifies the password on core 1. Session creation and backoff updates are queued
back to the HTTP-server task. Credentials are wiped before completion is queued.
Concurrent sign-ins receive 429. Diagnostics and serial logs record verification
duration without credentials.

Web tests passed for pending progress, duplicate prevention, failure/retry,
password-field clearing and successful navigation. Real-board speed, concurrent
HTTP responsiveness, resource-failure paths and socket/heap endurance remain
hardware checks; this build is not a measured latency result. The higher CPU
frequency can increase power use. Earlier test results below are historical.

## Dongle GUI update — 2026-09-15

This historical artifact superseded the initial artifact recorded below:

- `build/SolarProxxie.bin`: **952,704 bytes**, SHA-256
  `1aa2060cbcd6931de4b6c4c54bee23bd941fc66cea99f8fdb114855457aec769`.
- ESP-IDF compile/link and partition size check passed; 52% of the OTA slot is free.
- Web DOM tests passed, including dongle GUI enable/close, URL and disabled states.
- Actual C mapping module tests passed with SDK substitutes: expiry, target
  validation/replacement, disconnect and IP/MAC changes, setup/cloud gating,
  freed NAPT table and failure paths. Assertions were enabled.
- About now separates protocol attribution, project implementation and security.
- Browser discovery returned no available browser; visual validation remains pending.
- No real ESP32/dongle forwarding, login or redirect compatibility test was run.
- Existing parser/MQTT host results below are historical and were not rerun for
  this network/UI change. New management session IP binding needs hardware HTTP
  acceptance, as listed in `TESTING.md`.

Target: original dual-core ESP32, 4 MB flash. SDK: ESP-IDF 5.4.2. Application: 1.0.0.

## Initial firmware artifact (historical)

- `build/SolarProxxie.bin`: **950,064 bytes** (about 928 KiB).
- OTA slot capacity: 1,966,080 bytes; **52% remains free** according to IDF's size check.
- SHA-256: `41b1544a2c325969d1b95860d5b77e279551038ed7081caa7812ca08e1289c49`.
- Embedded gzip assets: HTML 521 bytes, CSS 1,644 bytes, JS 8,513 bytes.
- Production defaults: replay disabled, LED GPIO disabled, IPv4 NAPT enabled,
  IPv6 disabled, cloud forwarding enabled after normal configuration.

The packaged `dist/manifest.json` independently records image sizes, hashes and
flash offsets. GUI OTA accepts only the application image.

## Evidence and remaining checks

| Check | Result |
|---|---|
| Final ESP32 C/C++ compile, link and image-size check | Passed |
| Conditional replay / active-low GPIO2 branches | Compile-only check passed |
| Shared C host suite including 50,000 malformed inputs | Passed on the current revision |
| Final shared C host executable compilation and execution | Passed |
| Final native packet-decoder compilation | Passed |
| Final native decoder integration execution | Passed for synthetic 292/302/306 fixtures |
| Python replay-tool suite | Passed, 4 tests |
| PCAP link-type/endian, truncation and fragment Python tests | Passed |
| Web DOM integration and syntax | Passed |
| Real-browser visual/responsive check | Not run: no browser session available |
| ESP32 flash/boot, DHCP/NAPT, dongle/cloud continuity | Not run: no identified ESP32 attached |
| OTA/power-cut/rollback/endurance | Pending hardware acceptance |
| Hosted CI workflow | Added, not run on a remote CI service in this session |

An earlier run reported `WinError 4551` for newly built host executables. The
current release-wrap-up run succeeded without disabling or changing system security;
Zig's caches were kept inside the project workspace. The passing shared-C and replay
results therefore cover the current per-dongle identity/layout revision as well as
the declarative PV1+PV2+PV3 sum.

No physical hardware or visual-browser pass is implied by a successful compile.
Use [TESTING.md](TESTING.md) as the release qualification gate before relying on
the gateway unattended.
