# Dongle identity check: `solarproxxie-live.pcap`

Checked the latest file (SHA-256
`ef9be261b3b0d075c0398b50f0b7b44bf51a76f22ac7ad8184515d44f3d738b7`)
for traffic from `192.168.50.2`.

- The file has 776 raw-IPv4 records (link type 101). None is truncated. Raw IPv4
  contains no Ethernet/Wi-Fi MAC header.
- `192.168.50.2` sends 96 TCP packets: 62 toward `13.42.117.179:51100` and
  34 toward `101.37.34.21:51100`. It sends no UDP packets in this capture.
- Of its 30 nonempty TCP payloads, 20 are 306-byte inverter telemetry packets
  (ten distinct reports, each captured twice) and ten are 11-byte control
  messages (five distinct messages, each captured twice). Other outgoing TCP
  packets have no application payload.
- Wireshark frame 1 is the first telemetry report. Payload bytes 11-20 contain
  the ASCII **inverter serial** `2302246241`. That same string is the only
  printable run of four or more bytes in any outgoing application payload.
  Payload bytes 21-36 are zero in every telemetry report.
- Outgoing short control examples are frames 401-404: `a5 06 01 05 00 b6 00
  00 2e 00 00` and `a5 06 01 05 00 b6 00 00 2f 00 00`. The corresponding
  inbound 17-byte replies are frames 405-406. These packets contain no
  printable serial or key.

The captured bytes do **not establish a dongle serial or a dongle key**. The
10-digit ASCII value in telemetry should not be relabelled as the dongle serial:
the known layout identifies it as the inverter serial. A key might be encoded
in an unrecognised binary field or appear only in a startup, local setup, or
registration exchange not present in this capture. The PCAP alone provides no
evidence to identify one. If Wireshark shows a specific value, compare its
frame number and byte offset to this file before assigning it a meaning.

Subsequent direct inspection of the dongle's local web interface found both
values in its `GET /config?command=devinfo` response. Neither the dongle serial
nor its Register Key appears as ASCII in this PCAP. The web finding is recorded
in [DONGLE_WEB_SECURITY.md](DONGLE_WEB_SECURITY.md), without copying the key
into the source tree.
