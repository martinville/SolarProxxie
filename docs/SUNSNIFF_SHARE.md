# 306-byte Inteless capture: fields to share with sunsniff

## Suggested upstream contribution

The smallest useful pull request is **306-byte report support**, with new
measurements in a follow-up. In current sunsniff `build.rs`, extend
`pcap_sizes` from `&[292, 302]` to `&[292, 302, 306]`. Add
`v306_offset,v306_offset2` columns to `fields.csv`. For already verified main
measurements, copy the 302 offsets; the captured 306-byte reports use those
offsets. Leave the six `bms_*` offsets blank because their 306-byte locations
have not been established. Existing derived sums may be included where all
their inputs are present.

The current `src/pcap.rs` selects a field table by `transport.payload().len()`.
That is insufficient for the supplied captures: a single 306-byte report may
arrive as two TCP segments (including a 302+4 split), and a segment may contain
more than one report or a retransmission. Reassemble per TCP flow and sequence
number, then decode each **complete 306-byte application record** exactly once.
Do not identify 306 reports by reading the first 302 bytes as a complete report.
Keep existing 292/302 behavior and add tests for complete, split, coalesced and
retransmitted records. The new full cloud capture contains the dongle Register
Key, so derive a sanitized payload-only fixture instead of committing that PCAP.

The `extra_load_power_306` function below and the six supported energy fields
can be proposed after the framing change. `home_load_power` needs subtraction
and clamping; sunsniff's current `sum_of` metadata only adds fields, so it
cannot be represented accurately as a CSV-only derived sum.

Compared `solarproxxie.pcap` and `solarproxxie 2.pcap` with the upstream
[`fields.csv`](https://github.com/bmerry/sunsniff/blob/main/fields.csv) on
2026-09-17. Each capture contains one unique 306-byte telemetry record, observed
twice at the same TCP sequence number. All offsets below start at byte zero of
the **TCP application payload**, not the IP or TCP header. Both captures have the
same inverter serial; two observations are too few to establish every candidate.

## Confirmed by both captures and owner

| Field absent from sunsniff field table | Encoding | First | Second |
|---|---|---:|---:|
| UPS / essential power | signed BE16 at offset 242, watts | 607 W | 634 W |
| Home / non-essential power | `max(0, CT - grid L1)`, signed watts at offsets 228 and 218 | 18 W | 20 W |

In both records, UPS plus Home equals the existing load-power word at offset
240 (625 and 654 W). UPS also equals the existing inverter-power word at offset
234 plus grid L1 at offset 218. The word at offset 254 repeats the UPS values;
it may be a duplicate representation, not another independent sensor. The owner
confirmed the load split. Whether the calculated Home field applies to other CT
placements or inverter models remains to be tested.

This self-contained Rust function is intended as a starting point for the
developer's 306-byte decoder. It does not depend on sunsniff's generated field
types. The developer will need to wire it into that project's field metadata and
add 306-byte payload support: its current decoder looks up fields by *exact*
payload length, and its table only has 292 and 302 layouts.

```rust
/// Offsets start at the first byte of a 306-byte TCP application payload.
/// Return (UPS essential W, Home non-essential W).
fn extra_load_power_306(payload: &[u8]) -> Option<(i32, i32)> {
    if payload.len() != 306 || payload[0] != 0xa5 {
        return None;
    }
    let signed_word = |offset: usize| -> i32 {
        i16::from_be_bytes([payload[offset], payload[offset + 1]]) as i32
    };
    let ups = signed_word(242);
    let grid_ct = signed_word(228);
    let grid_l1 = signed_word(218);
    let home = (grid_ct - grid_l1).max(0);
    Some((ups, home))
}
```

Expected results: `(607, 18)` and `(634, 20)` for the first and second
captures. The current SolarProxxie implementation is in
[`main/modbus/register_extra.inc`](../main/modbus/register_extra.inc) and the
subtraction/clamping logic is in
[`main/modbus/modbus_decoder.c`](../main/modbus/modbus_decoder.c).

## Additional candidates absent from the sunsniff field table

These names and scales follow the independent
[`kellerza/sunsynk` single-phase definitions](https://github.com/kellerza/sunsynk/blob/main/src/sunsynk/definitions/single_phase.py),
but the *payload offsets* below are inferred by alignment with known sunsniff
offsets. They have **not** been confirmed against the inverter's own readings.
They should be inspected as candidate raw values before publishing named sensors.

| Candidate | Offset | Register candidate | First | Second | Assessment |
|---|---:|---:|---:|---:|---|
| Daily battery charge | 74 | 70 | 0.1 kWh | 0.1 kWh | constant, weak |
| Daily battery discharge | 76 | 71 | 0.0 kWh | 0.0 kWh | zero, weak |
| Daily grid import | 86 | 76 | 5.4 kWh | 5.5 kWh | plausible change |
| Daily grid export | 100 | 77 | 0.0 kWh | 0.0 kWh | zero, weak |
| Daily load energy | 102 | 84 | 5.2 kWh | 5.4 kWh | plausible; differs from lifetime-counter delta |
| Daily PV energy | 150 | 108 | 0.0 kWh | 0.1 kWh | plausible change |
| Monthly PV energy | 64 | 65 | 52.0 kWh | 52.1 kWh | plausible change |
| Monthly load energy | 66 | 66 | 313.2 kWh | 313.4 kWh | plausible change |
| Monthly grid energy | 68 | 67 | 37.9 kWh | 37.9 kWh | ambiguous name/direction |
| Inverter frequency | 270 | 193 | 49.95 Hz | 50.01 Hz | agrees with nearby load frequency |
| BMS SOC | 296 | unknown | 99% | 99% | plausible, constant |

For a quick raw-value check in Rust, use the same BE16 helper and apply `0.1`
to the candidate energy words, `0.01` to frequency, and `1.0` to SOC. Do not
assign the proposed register numbers as packet facts: the wire format is not a
contiguous Modbus register dump. The daily load candidate rises by 0.2 kWh
while the known lifetime load counter rises by 0.1 kWh; that mismatch needs
explanation from a longer recording or matching inverter display readings.

The BMS fields already named by sunsniff (voltage, current, temperature, limits)
appear to have different offsets in these 306-byte packets. For example, its
302-byte temperature offset produces -100 °C when applied to these captures,
while offset 302 contains 1223, numerically consistent with 22.3 °C. Two
constant readings do not verify the BMS layout. SolarProxxie leaves BMS fields
unavailable under its 306-byte profile until they are confirmed.

The full per-word comparison is in
[`PCAP_WORD_COMPARISON.csv`](PCAP_WORD_COMPARISON.csv). Do not share raw PCAPs
publicly without reviewing their serial number and network addresses first.
