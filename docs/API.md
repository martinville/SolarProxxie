# Local HTTP API (configuration schema 3)

HTTP port 80. Paths are exact and do not currently accept query-string filtering.
JSON responses use `application/json`. An error contains `{"error":"message"}`
with an appropriate 400/401/403/404/409/429/500/503 status where applicable.
Successful actions return `{"ok":true}`.

Normal mode requires a session for all data endpoints. Static HTML/CSS/JS and
`GET /api/session` are public so login can load. All mutations except login require
`X-CSRF-Token`, including OTA upload. Setup APIs require the AP interface; testing
upstream Wi-Fi does not expose an open setup API on the home network.

## Session

1. `POST /api/login`, JSON `{"username":"...","password":"..."}`.
2. Save the returned cookie (`HttpOnly`, `SameSite=Strict`, 30-minute expiry).
   The response contains a `csrf` token. Cookies are not marked Secure because
   this initial implementation intentionally uses HTTP.
3. `GET /api/session` reports `setup`, `authenticated`, firmware `version`, and the current CSRF token
   only to an authenticated session or on the physical setup AP.
4. Send `X-CSRF-Token: <token>` with mutations. `POST /api/logout`, `{}`, ends session.

Four sessions are retained; a new login replaces the least-recently-expiring slot.
Login verifies the password in one background worker. The same POST stays open
until verification finishes and returns the existing cookie/CSRF response. A
concurrent login receives HTTP 429 rather than starting another worker. Failure
backoff remains in effect. Async allocation/worker/queue failures return HTTP 503.
Authenticated status/diagnostics include `authentication.busy` and
`authentication.last_verification_ms` (zero until a completed verification).
Sessions are bound to the client's IPv4 address; sign in again after changing
networks. This prevents a dongle from reusing a management cookie it receives
through the temporary port forward (browser cookies are shared across ports).
Failed logins have increasing delay. Responses never return password hashes/salts.

## Reads

| Endpoint | Result |
|---|---|
| `/api/status` | system, network, router, MQTT, DNS, Internet-probe and offline-fallback objects |
| `/api/diagnostics` | same complete status snapshot |
| `/api/gateway` | network/AP clients, observed connections, and `dongle_gui` (`active`, `ip`, `port`, `remaining_seconds`) |
| `/api/datapoints/1` through `/api/datapoints/8` | selected inverter metadata, effective `mqtt_topic`, IP/name/serial, value and freshness; bare path aliases position 1 |
| `/api/mqtt` | connection, test result, sent/failure and dataset timing |
| `/api/config` | operational settings; additional public critical values in setup |
| `/api/export` | operational-only JSON with no password fields |
| `/api/scan` | setup-only array of SSID, RSSI, ESP-IDF auth-mode number |
| `/api/packets` | Debug-only observed connections and live-stream status |
| `/api/capture-stream` | Debug-only streaming PCAP download; records until stopped or disconnected, one active download |

Every `*_ms` timestamp is milliseconds since boot. `forward_attempts` does not
mean successful radio delivery. `router.dongles` contains independently observed sources with valid telemetry,
not MAC-vendor guesses. DHCP clients list their MAC/IP.
The ICMP result describes only the configured probe, not cloud application health.

Client discovery now includes every currently associated AP device, including
devices whose IP is unknown (`0.0.0.0`). Each client reports `ip_source` as
`dhcp`, `arp` or `unknown`. An ARP fallback requires a matching MAC, the AP
interface and the AP subnet; old entries for disconnected devices are excluded.
The same resolver validates passthrough targets. Network status also includes
`ap_ssid` and `clients_error`; clients with unknown IPs cannot be selected yet.

## Writes

- `/api/dongle-gui`: authenticated normal mode plus CSRF token required.
  `{"enabled":true,"ip":"192.168.50.10"}` forwards the STA TCP port 8080 to
  port 80 of a configured, currently connected AP client with a resolved IP for 15 minutes.
  Requires active NAPT and upstream Wi-Fi, but may bypass that dongle's cloud block for
  TCP port 80 only. `{"enabled":false}` closes it.
  Returns the `dongle_gui` status object; invalid or unavailable targets return
  409. Opening another target replaces the old mapping. State is not persisted.
  The raw forwarded service uses the dongle's own authentication, not the
  SolarProxxie login. Expiry and client IP/MAC changes are checked every second.
- `/api/dongle-info`: `{"ip":"192.168.50.2"}` returns the selected, currently
  connected and mapped dongle's serial and Register Key captured from its
  cloud-login TCP packet since gateway boot, plus `observed_seconds_ago`.
  If no matching packet has been observed, it returns 409. The key is held only
  in RAM and is not saved to configuration or published to MQTT. Normal
  administrator session and CSRF token are required.
- `/api/dongle-reboot`: `{"ip":"192.168.50.2"}` sends the dongle's own empty
  `POST /config?command=reboot` only to a mapped, currently connected AP client.
  A 2xx response confirms acceptance; it does not prove the dongle later
  reconnected. Normal administrator session and CSRF token are required.

- `/api/config` and `/api/import`: partial operational JSON. All input is validated
  before the candidate replaces NVS/RAM state. `version` if present must be 3.
- `/api/setup`: physical setup only; accepts network/AP/admin settings plus
  operational settings. Requires an admin password 12–128 bytes and a valid AP
  passphrase. Requires successful testing of the same STA configuration, or
  explicit `"save_without_wifi":true`. Saves and restarts after about two seconds.
- `/api/wifi-test`: same setup input, tests without storing. Poll status afterward.
- `/api/mqtt-test`: operational form input, connects with candidate MQTT credentials
  without storing. Poll `/api/mqtt` for `test_result`.
- `/api/action`: `{"command":"..."}` with `gateway-restart`, `discovery`, `publish`,
  `test`, or Debug-only `start`, `stop`, `clear` capture commands.
  `stop-stream` finishes an active long PCAP download early.
- `/api/ota`: raw application `.bin`, `Content-Type: application/octet-stream`.
  No multipart wrapper. Authenticated normal mode only. Size capped at inactive slot.
- `GET /api/update`: current cached GitHub release/update status.
- `POST /api/update`: `{"action":"check"}` checks the latest published release at
  `martinville/SolarProxxie`; `{"action":"install","version":"1.2.0"}` starts the
  previously checked release. Both require an authenticated normal-mode session and
  CSRF token. The accepted release must contain a `SolarProxxie.bin` asset.

Operational keys: `debug`, `mqtt_enabled`, `broker`, `port`, `mqtt_user`,
`mqtt_password`, `base`, `client`, `keepalive`, `retain`, `discovery`,
`discovery_prefix`, `on_change`, `min_interval`, `max_interval`, `stale_seconds`,
`layout` (legacy fallback: 292, 302 or 306), `log_level` (1 ERROR through 5 TRACE), `probe_enabled`,
`probe_ip`, `entities`, `dongles`.

Entity update example:

```json
{"entities":[{"id":"battery_soc","enabled":true,"name":"Garage battery",
"ha_name":"Garage Inverter Battery","suffix":"inverter/battery_soc","unit":"%"}]}
```

MQTT password omitted or `"********"` preserves the stored value; an empty string
clears it. The API only reports `mqtt_password_saved`. Export does not contain it.
Authenticated `/api/config` updates may change `sta_ssid`, `sta_password`, `dhcp`,
`ip`, `mask`, `gateway`, `dns1`, `dns2`, `ap_ssid`, `ap_password`, `ap_ip`,
`ap_mask`, `admin`, and `admin_password`. A password value of `********` preserves
the stored secret. Operational imports still reject these fields, and `salt`/`hash`
are never accepted.

JSON request bodies are capped at 24,000 bytes, headers at 1,024 bytes, URI at
128 bytes, and HTTP sockets at four. Responses are polled by the UI every four
seconds where live updates are useful. No unbounded WebSocket/SSE queues exist.

### Valid inverter data diagnostics

The `router.dongles` array in `/api/status` and `/api/diagnostics` contains up to
eight observed source IPs, each with `dongle_ip`, `inverter_serial`, `configured_layout`,
`last_inverter_ms`, `last_valid_age_seconds`, `packets_decoded`, and
`serial_conflict`. No singleton inverter record is returned. A missing source
means no accepted data observed at that IP. The UI joins these records to configured
mappings and displays waiting states for mapped but unseen sources.
`router.configured_layout` is the fallback for unmapped sources; mapped IPs use
their own `configured_layout`. `router.synthetic_replay` applies to all sources.
Ages use ESP32 monotonic time, not inverter time. Inverter serial is not dongle serial.

### Dongle mappings

`POST /api/config` accepts up to eight mapped inverters. The GUI keeps their
internal positions stable when adding or removing one:

```json
{"dongles":[
  {"ip":"192.168.50.2","name":"INVERTER1","profile":"inteless_sp_captured_306","cloud_forward":true},
  {"ip":"192.168.50.3","name":"INVERTER2","profile":"inteless_sp_legacy","cloud_forward":false}
]}
```

Names start with a letter and use 1-24 ASCII letters, digits, underscores or
hyphens. Active IPs and names must be unique. IPs must be canonical IPv4 client
addresses on the configured AP subnet; blank disables a slot. Invalid arrays are
rejected before saving. `profile` is a stable decoder identity; packet length is
only one of its properties. The legacy numeric `layout` input remains accepted for
backward compatibility. `cloud_forward` controls routed traffic independently for
each dongle. Mappings are included in config export/import. NVS versions 1 and 2
migrate to version 5 while preserving network and credential fields and assigning
their former shared layout. Version 3 records migrate their former global cloud
choice to every slot. Version 4 layouts migrate to named decoder profiles. Old
versioned JSON exports must be reviewed and updated to version 5.

### Debug packet capture

Authenticated Debug Mode exposes `/api/packets` with observed `connections` and
live-stream `status`. `/api/capture-stream` starts an asynchronous PCAP response;
`stop-stream` finishes it. Packets use a bounded queue only while a download is
active and are not retained in RAM or flash. No new active protocol requests are
sent.
