# Capture analysis: solarproxxie.pcap

Examined 2026-09-17. Capture SHA-256: c0682e5465aa837c7de6ac977ffc71536750d3d3dc8dd6b059d72c04d89a52ae.

## Observations

- 3,365 bytes; classic little-endian PCAP, raw IPv4 link type 101.
- 32 packets covering 21.236 seconds of device uptime; no truncated records.
- One unique 306-byte inverter TCP payload, present twice with identical TCP
  sequence number and payload. These are duplicate observations, not two readings
  or two inverters. Capture alone cannot establish retransmission versus hook duplication.
- Source 192.168.50.2:52234, destination 13.42.117.179:51100.
- Inverter serial at payload bytes 11-20: 2302246241.
- Payload timestamp at bytes 37-42: 2026-09-17 07:22:03 (timezone not encoded).
- An 11-byte server response follows; its meaning is not established.
- Other packets contain local multicast discovery/control traffic.
- No identifiable dongle serial/key or configuration message in this capture.

## Existing decoder comparison

The C parser accepts lengths 292 and 302. The stream reader slices the TCP stream
into the configured length; replay with --layout 302 accepts the first 302 bytes
of this 306-byte payload and leaves four trailing bytes. Thus apparent successful
telemetry decoding does not establish full layout compatibility.

Main measurements match the reference 302 offsets plausibly: grid 231.8 V,
battery 54.30 V / 99%, battery power -25 W, PV1 305 W, load 625 W,
grid frequency 49.95 Hz. These are existing fields, not new discoveries.

BMS values using that layout are implausible: charge voltage 0.02 V, voltage
0.99 V, current 5,399 A, temperature -100 C. They must not be considered validated
measurements for this captured layout.

## Candidate fields requiring validation

All offsets below are zero-based TCP payload offsets, words big-endian.
These are hypotheses from numerical consistency, not confirmed names or scales.

| Offset | Raw word | Possible interpretation | Evidence / limitation |
|---|---|---|---|
| 270 | 4995 | Additional frequency, 49.95 Hz | Matches known frequency at 268; exact channel unknown |
| 278 | 99 | Additional SOC/status | Matches known SOC; could be another status value |
| 288 | 5440 | BMS charge voltage, 54.40 V | Plausible voltage; identity unconfirmed |
| 294 | 104 | Possible BMS current limit, 104 A | Could be a limit or unrelated value |
| 296 | 99 | Possible BMS SOC, 99% | Matches inverter SOC |
| 298 | 5399 | Possible BMS voltage, 53.99 V | Close to inverter battery voltage |
| 300 | 0 | Possible BMS current | Zero alone cannot identify meaning or scale |
| 302 | 1223 | Possible BMS temperature, 22.3 C | Matches battery temperature word at 248 |
| 304 | 0 | Unknown trailing word | No semantic evidence |

No new entities or firmware mappings were added. The captured 306-byte layout
needs explicit validation, especially its BMS section; applying a universal
+2-byte offset shift is not justified. Additional captures at different SOC,
charge/discharge conditions and matching inverter/BMS screen readings would
allow candidate fields to be correlated. A capture around dongle startup could
also reveal identification messages absent here. Program schedules and daily
energy totals cannot be identified reliably from this single sample.

Reference checked against upstream on 2026-09-17:
- https://raw.githubusercontent.com/bmerry/sunsniff/main/fields.csv
- https://raw.githubusercontent.com/bmerry/sunsniff/main/src/pcap.rs

Reproduction: python tools/packet_decoder/decode.py solarproxxie.pcap --layout 302
This command demonstrates the current slicing behaviour, not valid 306-byte support.

## Essential / UPS and non-essential / home power

Additional investigation requested by user. The existing generic load_power is
625 W in this capture; this is not sufficient evidence to label it UPS power.

The kellerza single-phase definitions provide alternative essential formulas:
register 175 + 169 - 166, or register 175 + 167 - 166, and non-essential
max(0, register 172 - 167). Our captured reference-mapped values are:
175 = 275 W; 169 = 350 W; 167 = 332 W; 172 = 350 W.
Register 166 (AUX/GEN) is not currently mapped, so essential calculations require
an explicit assumption of zero AUX contribution.

Under that assumption, the L1 formula suggests UPS/essential = 607 W and
non-essential = 18 W; their sum is 625 W, equal to existing load_power.
The alternative essential formula gives 625 W, illustrating why firmware/model
validation matters. Payload offset 242 independently contains 607, a candidate
direct essential/UPS field, but no verified register mapping establishes its meaning.

These values are candidates, not newly enabled entities. Compare against the
inverter's UPS and home readings from the same sample and capture changes on
each circuit to distinguish the formula variants. CT placement and AUX port usage
must also be accounted for. Do not clamp an unknown/missing input into a valid zero.

Source:
https://raw.githubusercontent.com/kellerza/sunsynk/main/src/sunsynk/definitions/single_phase.py


### Implementation following user confirmation

User confirmed the 607 W / 18 W split and authorised both fields. UPS now reads
payload offset 242 under the 302 profile; Home is CT minus L1, clamped to zero.
Python inspection verifies these values in the actual capture. C host tests
verify the offset and calculation using controlled samples. Standalone native
capture replay was blocked by Windows Application Control, including on retry
with elevated tool permissions. Earlier candidate discussion records the evidence
before user confirmation. BMS mappings remain unverified and unchanged.
