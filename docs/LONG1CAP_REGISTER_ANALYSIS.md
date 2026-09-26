# Long1cap packet/register comparison

## Scope

Compared `Long1cap.pcap` with `sunsynk_modbus.pdf` and the existing SolarProxxie
field table. The capture contains six unique 306-byte telemetry reports from
inverter serial `2302246241`. Each report is duplicated in the PCAP because both
sides of the forwarded path were captured. It also contains device/login records,
11-byte acknowledgements and no native three-phase telemetry record.

## Packet structure established by the comparison

The 306-byte report contains these Modbus-shaped blocks:

| Register range | Packet byte formula | Evidence |
|---|---:|---|
| 63-124 | `offset = 2 * register - 66` | Every existing energy, temperature and PV mapping from registers 70-114 lands on the expected word. |
| 150-195 | `offset = 2 * register - 116` | Grid, inverter, load, battery and PV readings land on the expected words and track across all six frames. |
| 196 onward | Same arithmetic is possible but semantics diverge between models | Do not publish without a model-specific source or simultaneous display readings. |

This is stronger evidence than matching isolated plausible numbers: two long,
contiguous sequences agree with the PDF register ordering and with all currently
validated live measurements.

## High-confidence missing data points

These fields are present in the packet and named by the PDF, but are not currently
published by SolarProxxie:

| Field | Register | Packet offset | Type/scale | Long1cap observation |
|---|---:|---:|---|---|
| Battery daily discharge | 71 | 76 | U16, 0.1 kWh | 0.0 kWh in all frames |
| Grid daily export | 77 | 88 | U16, 0.1 kWh | 0.0 kWh in all frames |
| Load yearly energy | 87/88 | 108/110 | U32, 0.1 kWh | 3,772.1 -> 3,772.4 kWh |
| Inverter output frequency | 193 | 270 | U16, 0.01 Hz | 49.93-50.05 Hz; agrees with load frequency |
| PV4 voltage/current | 115/116 | 164/166 | U16, 0.1 V/A | Both zero; channel absent on this inverter |
| Warning words 1/2 | 101/102 | 136/138 | U16 bitfields | Both zero |
| Fault words 1-4 | 103-106 | 140-146 | U16 bitfields | All zero |

Useful but still model-sensitive fields are included in the evidence mapping file:
monthly grid energy, yearly PV energy, environment temperature, per-line L2 values,
per-line inverter/load power and current, and relay/status words. Constant zero is
not enough to validate sign, scale or availability, so these remain candidates.

## Corrections and conflicts

- The 302-layout BMS offsets must not be applied to this 306-byte profile. They
  produce impossible values. Words at offsets 288, 294, 296, 298 and 302 resemble
  BMS charge voltage, limit, SOC, voltage and temperature, but this capture does
  not vary enough to prove their identities.
- PDF register 179 is labelled load current L1 (0.01 A), which would place it at
  byte 242. Earlier capture work and user confirmation identify byte 242 as
  essential/UPS power and it reconciles exactly with total load and home load.
  This is a genuine model-specific conflict; the existing UPS mapping should not
  be silently renamed from this PDF alone.
- Registers 65-69 have different meanings for Hybrid, MI and String products in
  the same PDF. The model column matters; one universal map is unsafe.

## Three-phase conclusion

The PDF provides a useful **three-phase string-inverter candidate** at registers
70-79: phase-to-phase/phase-to-neutral voltages, phase currents and frequency.
Those registers overlap the Hybrid battery/grid-energy meanings. The candidate is
recorded in `mappings/sunsynk-evidence-map.json` with packet offsets derived from
the proven first register block, but it is not validated by `Long1cap.pcap` and is
not an uploadable SolarProxxie profile yet.

For a native Sunsynk three-phase hybrid such as `SYNK-8K-SG04LP3`, the supplied PDF
only identifies a three-phase grid-mode setting; it does not publish an L1/L2/L3
live register table. A correct native profile requires a capture from that exact
model plus its model/firmware identifier and simultaneous screen readings.

For a three-phase installation made from three single-phase inverters, configure
three dongles and reuse the appropriate existing profile independently for Phase A,
Phase B and Phase C. No new packet map is required for that topology.

## Evidence needed next

Capture at least five minutes while deliberately changing one quantity at a time:
battery charge/discharge, grid import/export, PV production and a known phase load.
Record screenshots of the inverter's diagnostic page at the same timestamps. For
a native three-phase unit, include model, firmware, dongle model and all per-phase
voltage/current/power readings.
