# Capture fixtures

Files beginning with `synthetic` are generated test data, not real inverter observations.
Generate them with `python tools/make_fixtures.py`. They contain serial `TEST000001`.

Place private captures in `private/` (git-ignored). Before contributing captures,
redact serial numbers, MAC addresses, public IP addresses and account information.
Do not change packet sizes or offsets when redacting. Recompute IP/TCP checksums if
you intend to replay a packet on a network. This project only replays into parsers.

The PC utility supports classic PCAP Ethernet (including VLAN), raw IPv4 and
Linux cooked v1/v2. Export PCAPNG to PCAP using Wireshark. IP fragments are skipped.
