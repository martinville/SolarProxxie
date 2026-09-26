# Architecture

## Network path

ESP-IDF 5.4.2 supplies `WIFI_MODE_APSTA`, SoftAP DHCP and lwIP NAPT through
`esp_netif_napt_enable()`. The AP-side netif is the NAPT interface. The STA is the
default outbound interface. Build defaults enable IP forwarding, NAPT and L2-to-L3
copying, and disable IPv6. The official
[lwIP guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-guides/lwip.html)
describes NAPT and supported compile-time hooks.

`ghost_hooks.h` is supplied to the lwIP component through the documented
`ESP_IDF_LWIP_HOOK_FILENAME` mechanism. SDK source files are not patched.

- `IP4_INPUT` observes AP-originated IPv4 before full stack validation. It only
  uses `pbuf_copy_partial`; it does not trust or cast header lengths.
- `IP4_CANFORWARD` enforces cloud blocking after destination NAT. It observes
  downstream routed copies and returns the normal lwIP decision when enabled.
- The hook never waits for a mutex, MQTT, disk, HTTP or a free queue slot. If the
  12-slot copy queue is full, only the copy is discarded.
- Counters distinguish **forward attempts** from radio-delivery success. An ESP32
  cannot infer application/cloud receipt from an attempted packet forward.
- During an Internet outage, the AP input hook answers only allowlisted vendor
  DNS A queries from configured dongles and directs them to the local TCP/51100
  responder. The responder acknowledges the captured read-only startup/report
  protocol. Two failed ICMP probes activate fallback; one successful probe restores
  forwarding promptly.

The AP DNS proxy binds to the AP address. Setup replies to A questions with the
portal IP; normal mode forwards bounded UDP DNS requests to STA-provided DNS
servers, with a random upstream ID and one-second timeout per server. DNS answers
larger than 512 bytes and TCP DNS fallback are not implemented by the local proxy.
Most dongle lookups fit this limit; confirm with the target hardware capture.

The home network can be DHCP or static. Static overlap is rejected before save;
DHCP overlap is detected at runtime and forwarding is disabled until corrected.
The normal AP supports eight associated clients. Telemetry is stored separately
for up to eight source IPs, with explicit IP-to-name and IP-to-layout mappings for publication.
Each IP is expected to report one inverter serial; conflicting serials block its
publication. The oldest source record is evicted if a ninth distinct IP reports. Clients on a shared AP are not mutually isolated.

## Tasks and memory

| Work | Scheduling |
|---|---|
| Wi-Fi | IDF task on core 0 |
| TCP/IP and forwarding hooks | IDF tcpip task on core 0 |
| Decoder | core 1, priority 4, 4 KiB stack |
| Telemetry/MQTT orchestration | core 1, priority 3, 6 KiB stack |
| HTTP | core 1, priority 2, 8 KiB stack |
| DNS/reconnect/health | unpinned application tasks |
| MQTT transport | ESP-MQTT-owned task |
| Optional ICMP probe | low-priority task, one check per ~30 seconds |

Permanent bounds: 8 decoder-queue packet copies of up to 768 bytes, four TCP flow
accumulators, 64 available field slots, four telemetry records and four MQTT
change-tracking records, four sessions, four HTTP sockets and an 8 KiB MQTT
outbox. Serial logging remains available, but logs and packet snapshots are not
retained in RAM. Live PCAP allocates its bounded queue only while downloading.

Telemetry/capture data uses a short mutex outside the network hook. Configurations
are copied under a separate mutex; HTTP validates a candidate before saving it.
Large config copies are heap-allocated in low-frequency tasks, not on task stacks.
Gzip assets are served directly from mapped flash. Data points, packet-offset files and configuration
responses stream one JSON entry at a time to avoid building a large JSON tree.
JSON parser nesting is limited to 12. No filesystem is mounted.

## Recovery

Wi-Fi reconnects with increasing delays and jitter, capped around one minute.
A 45-second connection/DHCP deadline requests reconnection. Configuration is never
erased because the Internet or broker is unavailable. MQTT reconnects independently;
state publication is paced and bounded. The health task feeds the task watchdog,
monitors physical reset, and stops capture under severe memory pressure.

Decoder heartbeat, queue depth, heap minimum/largest block, and task stack margins
are exposed. Unknown frames return errors; no exceptions or inverter commands are
generated. A C panic anywhere still resets the ESP32. This hardware/RTOS offers no
process isolation that could promise otherwise.

## Configuration and migrations

NVS namespace `ghost`, key `config`, stores one versioned fixed-layout record.
NVS's blob update mechanism commits a replacement before the old value becomes
obsolete; the 64 KiB partition leaves room for replacement and garbage collection.
Application RAM is updated only after a successful NVS operation.

`config/migration.c` owns version dispatch. Version 2 appended four IP/name slots;
`config/config_v1.h` freezes the real v1 layout. A compile-time assertion checks the
unchanged prefix size. Version 1 records migrate in RAM on boot, preserving network,
credentials and field settings. Version 3 expands this to eight mappings and adds
per-mapping packet layouts. Version 4 appends per-dongle cloud-forwarding flags;
v3's global choice is copied to every slot during migration. Older versions migrate
in RAM on boot. Version 5 appends stable per-dongle decoder profile IDs and derives
the packet length from each profile; v4 numeric layouts migrate automatically. V5
is persisted on the next successful save. Version 6 appends the global
Metric/Imperial display and publishing preference; v5 records migrate to Metric.
Version 7 appended per-profile packet-offset overrides. Version 8 removes the
dependency on C-defined field definitions and appends the complete uploaded field
definitions. Version 9 adds eight independently managed packet-offset setup slots.
Fresh installations, factory resets, and older records receive the three shipped,
self-contained JSON files, which are embedded as firmware data and parsed rather
than translated into hardcoded C offset tables. Uploaded version-1 setup files are
validated and saved with the main NVS record, then copied into runtime decoder
storage. Each upload replaces only its numbered slot. Unknown versions/sizes are
rejected without erasure. Old firmware
cannot read v9 records after a save.
Never change a deployed record layout without a new reader.
The host tests use an NVS stub; actual power-cut atomicity remains a hardware test.

JSON backup is intentionally **operational-only** and excludes critical values and
secrets. It can be restored from normal mode without bypassing physical setup.

## Extension boundaries

- New protocol profile: `protocol/`, with new real/sanitized fixtures and evidence.
- New field: central `modbus/register_map.c`, including model, type and scaling.
- TLS/HTTPS: transport initialization in MQTT and HTTP; keep authorization checks.
- New network transport: keep observation and policy hooks at the IP boundary.
- Remote OTA: reuse inactive-slot validation; add explicit authentication/signatures.
- Prometheus or REST exports: use telemetry snapshots, not the forwarding hook.

Do not add Modbus writes or inverter-control commands without a separate design.
