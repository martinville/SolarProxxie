# Inteless protocol evidence and decoder contract

## Sources and confidence

Reference inspected on 2026-09-14:

- [sunsniff README](https://github.com/bmerry/sunsniff): tested Sunsynk-5K-SG01LP1,
  unbranded Inteless dongle, passive router capture, newer 302-byte layout support.
- [pcap.rs](https://github.com/bmerry/sunsniff/blob/main/src/pcap.rs): payload sizes,
  magic, serial and timestamp locations, sanitized example.
- [fields.csv](https://github.com/bmerry/sunsniff/blob/main/fields.csv): packet offsets,
  register associations and field metadata. A local reference copy is included.
- [build.rs](https://github.com/bmerry/sunsniff/blob/main/build.rs) and
  [fields.rs](https://github.com/bmerry/sunsniff/blob/main/src/fields.rs): scales,
  temperature bias, word order, and limitations of the source's signedness handling.

**Reference-backed** means supported by this source, not verified on every inverter.
No real packet from the user's dongle has been supplied. **Probable** interpretations
are documented here but do not become enabled entities. **Unknown** fields are not mapped.
This is Inteless telemetry, not Solarman V5.

## Known packet layouts

Offsets are zero-based from the start of the **TCP application payload**, not
Ethernet or IP headers.

| Item | Reference-backed interpretation |
|---|---|
| Payload length | 292 bytes, or 302 bytes for newer dongle firmware |
| Byte 0 | `a5` |
| Bytes 11–20 | 10-byte inverter serial |
| Bytes 37–42 | Year since 2000, month, day, hour, minute, second |
| Individual words | 16-bit big-endian within a word |
| Two-word values | First referenced word is low word; second is high word |
| Temperatures | `raw × 0.1 − 100` degrees Celsius |
| Frequencies | `raw × 0.01` Hz |
| Energy totals | `raw × 0.1` kWh |

Timestamp values are local inverter time in the reference. Firmware validates the
calendar (including leap years) but uses **monotonic receipt time** for freshness;
it does not assume a timezone or a synchronized inverter clock.

The newer layout shifts many measurements by eight bytes, but BMS offsets differ.
Each layout has its own explicit offsets. It is incorrect to apply a uniform
shift to every value. Select 292 or 302 for that dongle under Dongles. This explicit selection
also prevents interpreting a 302-byte record split after byte 292 as an older frame.

## Examples of mappings

| Field | Register | 292 offset | 302 offset | Conversion |
|---|---:|---:|---:|---|
| Grid voltage | 150 | 176 | 184 | × 0.1 V |
| Battery temperature | 182 | 240 | 248 | × 0.1 − 100 °C |
| Battery voltage | 183 | 242 | 250 | × 0.01 V |
| Battery SOC | 184 | 244 | 252 | % |
| PV power 1 | 186 | 248 | 256 | W |
| PV power 2 | 187 | 250 | 258 | W |
| Battery power | 190 | 256 | 264 | signed W |
| Battery current | 191 | 258 | 266 | signed × 0.01 A |

A word `00 36` at SOC's offset represents 54%. A word `04 ba` at battery
temperature's offset represents 21 °C. The synthetic fixtures use these values;
they are not presented as captured evidence.

41 direct fields cover PV1–3 voltage/current/power, battery SOC/voltage/current/power,
battery capacity/temperature/charge-discharge totals, grid voltage/current/powers/
frequency/import-export totals/connection flag, load voltage/current/power/frequency/
consumption total, inverter power and temperatures, PV lifetime production, and BMS
voltage/current/limits/temperature.

`pv_power` is the reference's derived sum of observed PV1, PV2 and PV3 power. It is
published only when all source fields are valid, and is explicitly marked derived
rather than assigned an invented Modbus address. The declarative `sum_of` field
can reference earlier fields in the table for future verified sums.

The source treats all words as signed and explicitly notes that many registers
are actually unsigned. This release preserves that reference interpretation rather
than silently asserting new signedness evidence. The conversion engine independently
supports U16, I16, U32, I32, bit masks and numeric enumerations. Large lifetime totals
or high raw unsigned values need model-specific confirmation before Energy Dashboard
use. BMS fields have known payload offsets but unknown direct register addresses.

## Inteless versus Modbus

The passive reference extracts register-derived values at offsets. It does **not**
establish that this payload contains a complete RTU or Modbus TCP ADU. Accordingly:

- Inteless decoding never scans random bytes and labels matches as Modbus.
- The separate RTU response decoder accepts functions 03/04, checks slave ID,
  byte count, complete length and CRC16, and requires a **known starting register**.
- A read response itself does not carry its starting register; only a verified
  request/response correlation can supply it. Firmware does not guess it.
- The CLI `--modbus-start` is for explicitly identified development samples.
- No Modbus requests or writes are transmitted.

## Unverified items

Application envelope length fields, message-type fields, application checksum,
sequence fields, acknowledgements, poll triggers and error responses remain unknown.
The `a5` marker and size/calendar/serial checks are plausibility validation, not
cryptographic authenticity or a verified application CRC. The parser does not claim
checksum verification that the evidence does not support.

The reference's sanitized packet uses remote TCP port 51100, but this is an
**example**, not a universal endpoint. Firmware discovers observed destinations
without filtering to a fixed cloud IP/hostname/port. It does not infer a cloud
hostname from reverse DNS or label all AP clients as dongles. Dongle detection means
a plausible supported telemetry frame has been observed.

Other daily/yearly energy values, warning/fault codes, generator, auxiliary/smart load,
three-phase maps and model-specific status enumerations remain unmapped until
their locations and meanings are supported by evidence.

## TCP and capture limits

Four flows have bounded stream accumulators. In-order splitting, overlapping
retransmissions and bounded coalescing are supported for the selected fixed record
size. A gap, stale stream, unknown prefix or too-large copy discards parser state.
There is no unbounded out-of-order TCP/IP fragment reassembly. Valid observations
can therefore be missed under unusual segmentation, but original packets continue
through lwIP. Snapshots are capped at 768 bytes.

PCAP uses little-endian classic PCAP 2.4, link type 101 (raw IPv4), microsecond
timestamp fields derived from uptime. Downstream copies are taken after destination
NAT, so these are diagnostic IP observations, not exact over-the-air Ethernet frames.
The ring contains at most 32 records and overwrites the oldest.

## Adding real captures

1. First confirm cloud forwarding works with capture off.
2. Enable Debug Mode, start a short capture, wait for a reporting cycle, stop/download.
3. Store it under `tests/captures/private/`. Do not upload it automatically.
4. Decode with `python tools/packet_decoder/decode.py path.pcap --layout 292` (or 302).
5. Compare observed numeric values with an independent inverter reading at the same
   time. Record model, dongle firmware, source, byte order, scale and uncertainty.
6. Redact identifiers without changing length before contributing a fixture.
7. Add a failing host test before extending a profile. Never expand an existing
   profile merely because a plausible value happens to appear at an offset.


## UPS and home load power (2026-09-17)

Appended entities preserve existing indices, names and saved settings:
- `ups_power`: UPS Power (Essential), signed W at payload offset 242 with the
  302 profile. The user's 306-byte capture contains 607 W and the user confirmed
  the split. No Modbus register or 292-profile offset is asserted; the 292 profile
  leaves UPS power unavailable. Other inverter models remain unverified.
- `home_load_power`: Home Load Power (Non-essential), calculated as
  max(0, grid_power_ct - grid_power_l1), with both inputs required from the same
  source frame. The capture yields 350 - 332 = 18 W. This follows the kellerza
  single-phase non-essential formula; CT placement/model affects applicability.

The existing load_power remains unchanged. This does not establish full 306-byte
BMS compatibility. Extra declarations in main/modbus/register_extra.inc survive
reference-table regeneration. Empty appended entity slots in older configurations
are initialised on upgrade; customisations and disabled settings are preserved.


## Captured 306-byte profile and temperature fix (2026-09-17)

Select 306 bytes in that dongle's mapping under Dongles for the two supplied captures. The stream waits
for all 306 bytes, including when TCP splits at byte 302; it handles duplicates
and two coalesced records. Main measurement offsets use the 302 reference, including
UPS offset 242. All bms_* fields remain unavailable because their offsets are not
verified for 306-byte records. This avoids reporting -100 C from the wrong zero word.
Legitimate negative temperatures remain supported. Existing 292/302 decoding is
unchanged; selecting 302 for a 306-byte device can still misinterpret its BMS data.

Temperature defaults and MQTT discovery units are Celsius. Home Assistant may
convert these to Fahrenheit according to user preferences; firmware does not
silently relabel or override custom units. The second capture replays to 51 C DC,
43 C AC, 22.3 C battery, 634 W UPS and 20 W Home, with no BMS fields.
MQTT removes discovery and retained states for fields that become unavailable.
Live broker/hardware validation of cleanup remains pending.

## Capture-supported daily and monthly energy (2026-09-17)

Six fields are appended to the map for **306-byte Inteless frames only**. All
offsets start at byte zero of the TCP application payload; values are unsigned
16-bit big-endian words in 0.1 kWh units.

| Entity ID | Name | Offset |
|---|---|---:|
| `battery_charge_daily` | Battery Daily charge | 74 |
| `grid_import_daily` | Grid Daily import | 86 |
| `load_energy_daily` | Load Daily energy | 102 |
| `pv_energy_daily` | PV Daily energy | 150 |
| `pv_energy_monthly` | PV Monthly energy | 64 |
| `load_energy_monthly` | Load Monthly energy | 66 |

The morning-to-afternoon capture changes track their mapped lifetime counters.
The later ten-report capture preserves the names' plausibility but cannot prove
them independently. These are enabled by default and use Home Assistant energy
classification with `total_increasing` state. They remain unavailable on 292-
and 302-byte frames; Modbus register numbers are not inferred from payload
positions. Saved configurations gain only empty appended slots on upgrade, so
existing per-field customizations remain in place. Yearly PV, inverter
frequency, monthly grid energy, BMS SOC and constant-zero daily counters stay
unmapped pending direct inverter-side confirmation. See
[PCAP_LIVE_ANALYSIS.md](PCAP_LIVE_ANALYSIS.md) for the evidence and limits.
