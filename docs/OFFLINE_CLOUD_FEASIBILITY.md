# Offline dongle telemetry feasibility - updated 2026-09-18

## Product requirement

The dongle must continue sending inverter telemetry to SolarProxxie during an
Internet outage. The owner also wants a user choice between online operation
(real cloud forwarding) and offline operation (local-only telemetry with the
required dongle-facing cloud behavior emulated). Online mode should not be a
prerequisite for local MQTT/Home Assistant readings. An Internet outage in
online mode should not silently end local readings if offline fallback can
keep the dongle reporting. Any emulator must stay local and must not send
commands that change inverter settings.

This note separates observed behavior from the bounded local emulator now implemented.

## Gateway behavior with cloud forwarding disabled

`ghost_network_cloud(false)` disables AP-side NAPT. The IPv4 forwarding hook
blocks routed traffic in both directions, and the AP DNS proxy returns SERVFAIL
instead of forwarding normal DNS queries. The capture/decoder path can still
observe AP-originated packets, but the dongle loses its cloud TCP conversation.
The Gateway UI already warns that passive inverter updates may stop.

This alone can explain why telemetry stops after forwarding is disabled. The
new `solarproxxie-live.pcap` shows the dongle sending DNS queries to `8.8.8.8`
for `ukiot.sunsynk.net` and `iot.e-linter.com`. The dongle does not rely solely
on the AP-advertised DNS proxy, so changing replies from the existing proxy
would not intercept those hard-coded upstream queries. DNS interception or a
verified dongle DNS-setting change is needed for an offline endpoint.

## Observed application replies

In the updated `solarproxxie-live.pcap`, a full TCP connection from
`192.168.50.2:63910` to `18.170.245.78:51100` contains a 43-byte serial/key
message (frame 236), a 19-byte server reply (241), an 11-byte request/reply,
a 164-byte device-information message/reply, other control exchanges, then
306-byte inverter reports (365 and 495), each followed by an 11-byte server
reply (367 and 497). The report reply carries the same one-byte message number
as the report (payload byte 8). Another complete connection follows the same
pattern at frames 1339 through 1399. For example:

```
report prefix: a5 06 01 09 02 b8 00 00 09 ...
reply:         a5 06 a1 09 01 00 00 00 09 00 00
```

The 43-byte message carries the same dongle serial and Register Key returned by
the dongle's `devinfo` endpoint. The 306-byte reports contain the inverter
serial, but neither the dongle serial nor Register Key. This supports a
connection-level identification exchange, but cannot establish what the cloud
server actually validates or whether the key is required for continued reports.
The matching report replies are consistent with application acknowledgements;
they do not prove the dongle suppresses future reports when one is missing.

The dongle's read-only `GET /config?command=status` returned `serverstatus: 0`
and `wifistatus: 5` while connected; its page labels these as OK. Its
configured server setting reports `pv.e-linter.com:6666`, which does not match
the destination IPs and port in this PCAP. The relationship between this
setting, DNS answers and the observed connections is unknown.

## What is needed before an offline mode

An effective local mode would need to intercept the dongle's DNS queries even
when sent to `8.8.8.8`, resolve only the observed cloud hostnames to a local
address, accept TCP on port 51100, handle the initial serial/key and control
exchanges, and return appropriate application replies for reports. It must be
restricted to the dongle AP and active only in an explicit offline mode. Simply
forging DNS success would leave the TCP conversation and application replies
absent. Replaying the 11-byte telemetry reply without the initial handshake is
not a verified solution. The server's validation rules and the dongle's behavior
without replies still need hardware tests.

## Transition captures received 2026-09-23

`forwarding-on-off-on.pcap` contains the missing transition evidence. Before
forwarding was disabled, the existing cloud connection carried 306-byte type
`0x09` reports and matching 11-byte acknowledgements. Once forwarding was
disabled, the dongle retried the existing connection, alternated DNS queries for
`iot.e-linter.com` and `ukiot.sunsynk.net` between `8.8.8.8` and `223.5.5.5`, and
did not produce a usable telemetry session. At about 560 seconds, forwarding was
restored; the dongle immediately opened TCP/51100 sessions and repeated the
deterministic login, device-information and polling-map exchange. Its first
306-byte report followed about 11 seconds later.

`dongle-offline-startup.pcap` covers roughly 310 seconds. It contains the same
repeated DNS cycles and resolver replies but no TCP/51100 application exchange.
Together the captures show that DNS success alone does not keep telemetry alive
and that a local TCP responder must implement the observed startup exchange.

## Implemented bounded fallback

The gateway now intercepts only A/IN queries from configured dongles for the
three observed vendor names (`ukiot.sunsynk.net`, `iot.e-linter.com`, and the
configured `pv.e-linter.com`). In local mode it answers with the gateway AP
address while preserving the queried resolver as the apparent DNS source. A
local TCP/51100 service accepts only configured dongle IPs and returns the
captured login clock reply, read-only polling map, and message-number-matched
acknowledgements. It never initiates a dongle message and does not emit inverter
write commands.

Two consecutive failed reachability probes activate fallback and disable NAPT;
one successful probe restores forwarding. A dongle whose individual
cloud-forwarding option is disabled always uses the local responder. Existing
local sessions are closed when online forwarding resumes so the dongle reconnects
to the real service.

Firmware and host parser tests validate bounds, hostname allowlisting, and exact
reply construction. Real-dongle testing is still required before treating the
fallback as proven across every dongle firmware revision. Do not commit a
Register Key or extracted identity payload to test fixtures.
