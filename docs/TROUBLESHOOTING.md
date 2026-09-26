# Troubleshooting

## Local-only dongle has no data after a cold start

Current firmware starts its DNS interceptor and local TCP/51100 cloud service before
activating the access point. It also answers the initial dongle clock handshake from
the firmware build clock until SNTP synchronizes. This prevents a local-only dongle
from caching an early DNS failure or abandoning its cloud session until manually
rebooted. Diagnostics reports `clock_synchronized` and
`fallback_clock_responses`; using the fallback during startup is expected when the
Internet or SNTP is not yet available.

## No setup AP

- Confirm this is the original ESP32 and that the application was flashed at the
  supplied offsets. Watch serial at 115200 baud.
- Release BOOT before normal power-on. Holding it at reset can enter the ROM loader.
- For a configured board, press BOOT **after reset**, during the first five seconds.
- LED indication is unavailable unless a correct board-specific GPIO was configured.
- A corrupt/future NVS record requires holding BOOT for ten seconds to reset it.

## Captive portal does not appear

Connect to `SolarProxxie-Setup` and manually open **http://192.168.4.1**. Disable
the phone's automatic switch to mobile data or another Wi-Fi temporarily. Captive
portal behavior varies by operating system; HTTPS sites cannot be transparently
redirected without certificate errors, so use the explicit HTTP address.

## Wi-Fi test fails

The original ESP32 uses 2.4 GHz, not 5-GHz-only networks. Check SSID, passphrase,
signal, gateway/mask and DNS. A static address must be unused. Home and AP subnets
must not overlap. Scanning/testing can change the shared APSTA radio channel and
briefly interrupt the phone's connection; reconnect to the setup AP and retry.
An explicit save override is available, but does not repair incorrect credentials.

## Dongle gets no IP

First test the protected AP with a laptop/phone. Verify the configured AP password
and look for its MAC/IP in Gateway. Default leases are `.10`–`.50`. Disconnect old
test clients if the four association slots are occupied. Re-run the dongle's own
Wi-Fi provisioning to select SolarProxxie instead of the home SSID.

## Cloud stops updating

Check that cloud forwarding is enabled for the affected dongle, **NAT active**, STA connected, and non-overlapping
subnets. Confirm DNS responses and TCP sent/reply counters. Test AP Internet access
with a laptop, then reconnect the dongle. A failed ICMP probe alone does not prove
a cloud outage: some networks filter ping. Re-enable forwarding if disabled and
allow the dongle time to reconnect. Never erase config merely to fix an Internet outage.

Packet counters show attempts/observations, not proof the server accepted a report.
If NAT/DNS compatibility fails with this dongle, preserve a short capture and record
the exact ESP-IDF version and hardware. Do not declare the first milestone passed
based solely on counters.

## Cloud works but no inverter values

This is a legitimate unknown-protocol outcome, not a reason to block traffic.
Wait several reporting intervals. Select the correct 292/302 layout. Inspect
queue drops and decoded/rejected counts. Enable a short RAM capture and decode it
on the PC. Larger/coalesced packets, out-of-order TCP, IP fragments, another model
or an unknown dongle firmware can prevent decoding. Do not guess addresses.

When forwarding is off, reports may stop because cloud polling is required. The
initial release does not emulate a cloud server or inject requests.

## MQTT connects but Home Assistant has no entities

Entities are advertised only after a supported dataset is observed. Confirm broker
publish ACLs, discovery prefix, enabled fields and failed-message counters. Use
Test entered settings before saving, then Republish discovery after connection.
The MQTT integration and ESP32 must use the same broker. Give multiple gateways
different client IDs/base topics. If a previous broker was offline during a topic
change, manually remove obsolete retained discovery topics there.

## Values are stale or implausible

Freshness defaults to 900 seconds. Increase only if the actual reporting interval
requires it. Check the model-specific mapping and reference signedness limitation.
Changing a unit label does not apply a conversion. Never use an unverified register
or derived reading as an electrical safety signal.

## OTA fails

Use `build/SolarProxxie.bin`, not bootloader or merged flash images. Stay within the
1,920 KiB slot limit and keep power stable. Login again if the session expired.
Interrupted/invalid uploads do not select the new boot partition. If a new image
reboots before confirmation, rollback should select the previous valid application.
Use serial flashing if both OTA images are unavailable. Preserve the partition layout.

## Heap or packet queue pressure

Stop capture, close extra browser tabs and inspect the largest-free-block and task
stack metrics. MQTT failure should not stall forwarding. Capture storage and the
copy queue are bounded; copy drops are preferable to blocking a dongle packet.
Run the hardware soak checklist before leaving the gateway unattended.

## PC decoder rejects a capture

Export PCAPNG as classic PCAP. Supported link types are Ethernet, raw IPv4 and Linux
cooked v1/v2. Choose the correct layout. The parser skips incomplete IP fragments
and truncated packet snapshots. A HEX input should contain byte pairs with optional
whitespace/comments; Wireshark offset columns are accepted when separated by two spaces.
Raw binary input is an application payload, not an arbitrary Ethernet frame.
