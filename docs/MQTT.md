# MQTT and Home Assistant

Use a local broker, for example Home Assistant's Mosquitto app, and add the
[MQTT integration](https://www.home-assistant.io/integrations/mqtt/). Configure its
broker host, port and credentials. Then configure SolarProxxie's MQTT page with
credentials permitted to publish the base and discovery topics.

Default MQTT is disabled until configured. Transport is TCP on port 1883,
keepalive 60 seconds, discovery prefix `homeassistant`, base `solarproxxie`.
TLS is not part of the initial transport configuration. Do not put secrets into
the broker hostname; it accepts a hostname or IPv4, not a URI.

## Topics

Map dongle IPs under **Dongles** before publishing.
Up to eight internal positions have distinct names, such as `INVERTER1` and `INVERTER2`.
Names are case-sensitive, start with a letter, and contain 1-24 ASCII letters,
digits, underscores or hyphens. Blank IPs disable a slot. Unmapped sources are
visible in Diagnostics but do not publish. This setting does not reserve DHCP
addresses: keep each dongle's address stable and update mappings when it changes.

Example for gateway identifier `ghost_aabbccddeeff`, slots 1 and 2:

```text
solarproxxie/INVERTER1/state                             JSON state snapshot
solarproxxie/INVERTER2/state                             JSON state snapshot
solarproxxie/ghost_aabbccddeeff/availability             gateway online/offline
solarproxxie/ghost_aabbccddeeff_1/inverter_availability   slot 1 online/offline
solarproxxie/ghost_aabbccddeeff_2/inverter_availability   slot 2 online/offline
homeassistant/sensor/ghost_aabbccddeeff_1/battery_soc/config
homeassistant/sensor/ghost_aabbccddeeff_2/battery_soc/config
homeassistant/status                                   HA birth subscription
```

Each mapped slot becomes a separate Home Assistant device named after its mapping.
Its entity unique ID is `ghost_aabbccddeeff_1_battery_soc` (slot 1 example), independent
of the alias or IP. Renaming a slot changes its device name and state topics while
retaining its unique IDs. Moving a dongle to another slot changes its unique IDs.

Field names, units, JSON keys, enable flags, packet layout and publishing intervals
are shared settings. Their **values, last successful dataset, change detection,
publishing interval tracking and freshness are separate for each source IP**.
The GUI shows the shared snapshot topic and effective JSON key for the selected inverter.
A field suffix of `battery_soc`, `inverter/battery_soc` or legacy
`sunsynk/battery_soc` produces the JSON key `battery_soc`. Conflicting effective
keys are rejected. Home Assistant discovery uses a value template for each entity
to select its key from the shared message.

Discovery includes device information, unit, supported device class and state
class, and two availability topics in `all` mode. Every enabled field for a
configured dongle is advertised, including before its first reading and while the
inverter is offline. Freshness is represented by the inverter availability topic;
temporarily missing values are published as JSON `null` instead of removing the
entity. Metadata is suppressed when a custom non-temperature unit no longer matches
the verified unit. Temperature values and discovery units follow the global
Metric/Imperial preference, with Celsius retained as the internal decoded value.

Device availability uses retained `online` and an MQTT LWT of retained `offline`.
Inverter availability becomes offline when no valid dataset arrives within the
configured timeout (default 900 seconds), independently for each mapped source.
A source reporting multiple different inverter serials at one IP is marked as a
serial conflict and its publishing is blocked until reboot after correcting the
address assignment. One dongle reporting several inverter serials is not supported. The timeout uses receipt uptime, not
the possibly inaccurate inverter clock. A broker outage does not stop decoding.

Discovery is retained and refreshed on connection, explicit request and HA's
`online` birth message. An empty retained discovery payload removes a disabled
entity. Discovery is interruptible background work: state snapshots always take
priority. A broker/base/prefix change can leave retained topics under the old
namespace; clear those on the old broker if necessary. Avoid sharing the same MQTT
client ID across gateways.

## Efficiency and timing

Every accepted dongle dataset wakes the MQTT task and is published immediately,
including when its numeric values are unchanged. The default 5-second interval is
used only to retry a failed dataset. The default 60-second maximum interval refreshes
the most recent fresh snapshot when no new dataset arrives. Manual publish bypasses
both intervals and reports an explicit error when no fresh mapped data exists. Stale
values are not republished as fresh data. Local broker disconnects use an independent
reconnect loop; the outbox is capped at 8 KiB with expiration.

Each inverter state publication is one JSON object containing every enabled field
from a single captured dataset. Valid fields contain finite numbers; missing or
invalid fields contain JSON `null` and are never published as the strings `unknown`
or `unavailable`. Home Assistant may still display those
states when it has not yet received a reading or when the inverter availability
topic reports `offline`. This preserves the distinction between a real reading
and a disconnected inverter.

Each complete JSON snapshot requests one QoS 1 acknowledgement. The MQTT status
shows the completion uptime and how many entities were carried by the most recent
successful or failed snapshot. This measures the **broker PUBACK**, not Home
Assistant database/UI processing. Discovery traffic is excluded from the dataset
result, but included in the overall sent-message counter.

**Test entered settings** makes a separate short-lived broker connection without
saving the form or publishing any inverter command. Check `test_result` in the
status panel. **Test saved MQTT connection** requests a reconnect of the active
configured client. A successful broker login does not prove publish ACL permissions;
verify the dataset succeeded/failed counts as well.

## Energy and unsupported models

Verify register interpretation for the exact inverter before using cumulative
energy entities for billing, automation or Energy Dashboard calculations. The
initial reference has a signedness limitation documented in [PROTOCOL.md](PROTOCOL.md).
Do not substitute invented daily totals or three-phase registers.

## Upgrade from single-inverter firmware

Version 1 through 4 NVS records are read and migrated without changing Wi-Fi credentials,
admin hashes, broker credentials or custom field settings. Version 3 introduced
eight add/remove inverter mappings and a separate packet layout for each mapped IP.
Existing version 2 mappings retain their positions and inherit the former shared
layout. Version 4 adds a cloud-forwarding choice to each mapping and preserves the
v3 global choice during migration. Version 5 replaces size-only selection with a
stable named decoder profile and migrates 292/302/306 layouts automatically. The
next configuration save writes schema version 5; older firmware cannot read that record. Keep a configuration backup
before reverting firmware.

Old single-device discovery entries are removed on connection to the configured
broker. The new per-slot unique IDs create separate entities, so existing HA
automations/dashboards may need to select the new entities. Legacy retained per-field state
topics are cleared during a reachable settings reload and are no longer published. Renaming mappings
cleans old mapped state/discovery topics when the old connection is reachable;
changes while offline may require manual removal of old retained state topics.
No live multi-dongle/broker hardware qualification is implied by host tests.

## Product rename

Factory-default MQTT base/client names migrate to `solarproxxie` on boot; custom
values are preserved. Device and entity unique IDs retain their existing internal
identifiers, so a brand rename alone does not create new HA devices. Discovery
updates the manufacturer/model and state/availability topics. Old retained broker
state topics may still exist but are no longer used by the new default namespace.
