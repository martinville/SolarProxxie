# Live capture field comparison - 2026-09-17

## Follow-up: `solarproxxie-live.pcap`

The later 89,567-byte file has SHA-256
`ef9be261b3b0d075c0398b50f0b7b44bf51a76f22ac7ad8184515d44f3d738b7`.
It contains 776 packets over 655 seconds of device uptime and ten distinct
306-byte telemetry reports (each seen twice), from embedded times 17:30:06
through 17:40:21. All are from the same inverter as the earlier samples. Reports
arrive about every 68-69 seconds in this capture.

No additional **reliably named** data point emerges. Daily grid import at payload
offset 86 rises 8.8 to 9.1 kWh; daily load at 102 and monthly load at 66 rise
13.7 to 14.0 and 321.7 to 322.0 kWh. Their mapped lifetime totals rise by
0.4 kWh, a 0.1 kWh difference consistent with counter update timing or
resolution but not exact proof. Daily/monthly PV, yearly PV, and daily battery
charge remain constant as production and charging fall near zero in this period.
The proposed inverter frequency at offset 270 exactly equals mapped load
frequency at 268 in all ten reports, so it may be a duplicated reading.

The earlier **monthly grid energy** candidate at offset 68 should not be
published with that name: it stays at 42.9 kWh while daily grid import rises
0.3 kWh. Daily battery discharge and daily grid export remain zero; BMS SOC
remains 99%. This capture cannot verify those field names or the BMS offset.
The existing UPS plus calculated Home load equals total load in all ten reports.

One short 11-byte control message directly precedes the 17:35:47 telemetry
report. The generic `tools/packet_decoder/decode.py` reassembler merges these
into an invalid 306-byte slice and prints `decoded: false`. The ten-report
comparison above extracts complete 306-byte TCP payloads directly from the
PCAP; it includes that valid report. This is a replay-tool framing limitation,
not evidence of a new inverter field.

## Earlier live capture: `solarproxxie-live (1).pcap`

Capture: `solarproxxie-live (1).pcap` (21,354 bytes), SHA-256
`8b3931e3546ac2ba2ff197958d129134319a2da325c029f7c5f7df225d286d41`.
Classic raw-IPv4 PCAP with 186 packets over 166 seconds of device uptime. It
contains two unique 306-byte inverter TCP payloads, each observed twice. Their
embedded timestamps are 17:20:59 and 17:22:07 on 2026-09-17. Comparison below
also uses `solarproxxie.pcap` (07:22:03) and `solarproxxie 2.pcap` (07:28:53).
The file `solarproxxie (1).pcap` contains no inverter TCP payloads.

The following values are **capture-derived 306-byte payload fields** absent from
[sunsniff's field table](https://github.com/bmerry/sunsniff/blob/main/fields.csv).
The six daily/monthly counters in this table are now mapped in SolarProxxie for
306-byte frames only. Yearly PV and inverter frequency remain candidate fields
and are not published. Offsets are zero-based from
the TCP application payload, not the IP packet. Values use big-endian 16-bit
words multiplied by 0.1, except frequency (0.01). Register labels are candidate
identities from [kellerza's single-phase definitions](https://github.com/kellerza/sunsynk/blob/main/src/sunsynk/definitions/single_phase.py),
not an assertion that the whole TCP payload is a contiguous register dump.

| Proposed data point | Payload offset | Candidate register | 07:28 | 17:20 | Corroboration |
|---|---:|---:|---:|---:|---|
| Daily battery charge energy | 74 | 70 | 0.1 kWh | 0.4 kWh | +0.3 kWh matches mapped lifetime battery charge |
| Daily grid import energy | 86 | 76 | 5.5 kWh | 8.3 kWh | +2.8 kWh matches mapped lifetime grid import |
| Daily load energy | 102 | 84 | 5.4 kWh | 13.3 kWh | +7.9 kWh matches mapped lifetime load consumption |
| Daily PV energy | 150 | 108 | 0.1 kWh | 5.5 kWh | +5.4 kWh matches mapped lifetime PV production |
| Monthly PV energy | 64 | 65 | 52.1 kWh | 57.5 kWh | +5.4 kWh matches PV total and daily PV |
| Monthly load energy | 66 | 66 | 313.4 kWh | 321.3 kWh | +7.9 kWh matches load total and daily load |
| Yearly PV energy | 70 (word 72 is zero) | 68-69 | 1081.1 kWh | 1086.5 kWh | +5.4 kWh matches PV total; newly identified candidate |
| Inverter frequency | 270 | 193 | 50.01 Hz | 50.01 Hz | 49.95 Hz in the second live frame; tracks mapped load frequency at offset 268 in all four samples |

The cumulative-counter agreement supports these labels for this inverter, but
only an inverter screen or Modbus register comparison can independently confirm
each payload offset. In particular, frequency at 270 may be a copy of load
frequency, and yearly PV at 70 requires a year-counter comparison.

Still tentative: daily battery discharge at 76 and daily grid export at 100
remain zero; monthly grid energy at 68 rises 37.9 to 42.9 kWh, which does not
match the +2.8 kWh lifetime grid-import change. BMS SOC at 296 remains 99% in
all four samples. The BMS voltage/temperature-looking words at 298/302 differ
from sunsniff's 302-byte layout, but BMS voltage and temperature are already
named by sunsniff and these new offsets remain unconfirmed. No named entity
should be published from these tentative words yet.

SolarProxxie already exposes UPS/essential power and derived Home/non-essential
power, both absent from the upstream table. The live readings (564/105 W and
550/4156 W) again satisfy UPS + Home = total load (669/4706 W). The large home
load change adds evidence for that existing split, not a new data point.

Reproduce the mapped fields with:

```
python tools/packet_decoder/decode.py "solarproxxie-live (1).pcap" --layout 306
```
