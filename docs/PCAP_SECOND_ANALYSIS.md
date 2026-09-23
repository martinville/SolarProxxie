# Second capture comparison - 2026-09-17

Files: solarproxxie.pcap and solarproxxie 2.pcap.
New file SHA-256: 5ae36579aa01058bc1d3cad5e9062db72135bf2465e9bc2bd7d5d27871dae133.

The second capture has 32 untruncated packets spanning 21.509 seconds. It contains
one unique 306-byte telemetry payload, observed twice at the same TCP sequence,
and an 11-byte server reply. Same inverter serial and TCP endpoints as the first
capture. Payload timestamps are 07:22:03 and 07:28:53 on 2026-09-17, 6m50s apart;
PCAP times are device uptime, not wall-clock time. No new identification message
or identifiable dongle key was found.

## Existing load split independently corroborated

| Measurement | First capture | Second capture |
|---|---:|---:|
| UPS at offset 242 | 607 W | 634 W |
| Non-essential, CT minus L1 | 18 W | 20 W |
| Total load at offset 240 | 625 W | 654 W |
| Inverter output at offset 234 | 275 W | 332 W |
| Grid L1 at offset 218 | 332 W | 302 W |
| Grid CT at offset 228 | 350 W | 322 W |

UPS equals inverter output plus grid L1 in both samples, and UPS plus Home equals
total load. Offset 254 also equals UPS in both samples, but is not exposed as a
second independent measurement.

## Additional candidate fields absent from sunsniff fields.csv

These identities/scales are supported by kellerza's single-phase register table,
but the TCP payload positions below are inferred by interpolation between known
sunsniff register/offset anchors. This is NOT independent confirmation of offsets.
Two readings support plausibility; zero/constant values provide weak evidence.
All offsets refer to the captured 306-byte payload, not an assumed 292 layout.

| Candidate | Register | Offset | First | Second |
|---|---:|---:|---:|---:|
| Daily battery charge | 70 | 74 | 0.1 kWh | 0.1 kWh |
| Daily battery discharge | 71 | 76 | 0.0 kWh | 0.0 kWh |
| Daily grid import | 76 | 86 | 5.4 kWh | 5.5 kWh |
| Daily grid export | 77 | 100 | 0.0 kWh | 0.0 kWh |
| Daily load energy | 84 | 102 | 5.2 kWh | 5.4 kWh |
| Daily PV energy | 108 | 150 | 0.0 kWh | 0.1 kWh |
| Monthly PV energy | 65 | 64 | 52.0 kWh | 52.1 kWh |
| Monthly load energy | 66 | 66 | 313.2 kWh | 313.4 kWh |
| Monthly grid energy | 67 | 68 | 37.9 kWh | 37.9 kWh |
| Inverter frequency | 193 | 270 | 49.95 Hz | 50.01 Hz |

Important caveat: register order is not universally contiguous in this protocol.
For example, mapped grid-import words straddle grid frequency, and the proposed
export/day-load positions are around another discontinuity. Daily load's proposed
0.2 kWh increase also exceeds the observed total-load lifetime counter increase
of 0.1 kWh; their scope or update timing may differ, or the interpretation may be
wrong. These candidates should be matched to daily/monthly inverter screen values
before publishing them as named sensors. No additional firmware fields were added
in this analysis.

## BMS: probable values, still not a verified layout

BMS SOC at offset 296 remains 99%; this candidate is absent from sunsniff's table.
Possible voltage at 298 changes 53.99 -> 53.98 V. Possible charge voltage at 288
stays 54.40 V, limit at 294 stays 104, temperature at 302 stays 22.3 C. Sunsniff
already has the latter field types, but its 302-layout offsets do not fit this
capture. Those are potential corrections, not wholly new measurements. Current
at 300 stays zero and cannot establish sign or scaling. The BMS data did not vary
enough to resolve these uncertainties. No BMS fixes or new BMS entities were made.

## Comparison scope and reproducibility

Compared against upstream main fields.csv on 2026-09-17. UPS and calculated Home
are SolarProxxie extensions absent from that table. The new daily/monthly/frequency
candidates above and BMS SOC are absent as well. Program schedule fields are listed
upstream as registers but have no packet offsets; these captures do not establish
those offsets. This comparison concerns the field table, not every issue/fork.

PCAP_WORD_COMPARISON.csv lists every even-aligned word from offset 44 through 304,
both raw values, known upstream mappings, and candidate labels. Existing BMS labels
in that CSV mean current reference offsets only, not verified 306-byte semantics.

Sources:
- https://raw.githubusercontent.com/bmerry/sunsniff/main/fields.csv
- https://raw.githubusercontent.com/kellerza/sunsynk/main/src/sunsynk/definitions/single_phase.py
