"""Summarize dongle DNS and TCP/51100 traffic from a classic PCAP."""
import argparse
import ipaddress
import struct
from pathlib import Path


def packets(path):
    with path.open("rb") as stream:
        header = stream.read(24)
        if header[:4] in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1"):
            endian = "<"
        elif header[:4] in (b"\xa1\xb2\xc3\xd4", b"\xa1\xb2\x3c\x4d"):
            endian = ">"
        else:
            raise ValueError("not a classic PCAP")
        link = struct.unpack_from(endian + "I", header, 20)[0]
        scale = 1e-9 if header[:4] in (b"\x4d\x3c\xb2\xa1", b"\xa1\xb2\x3c\x4d") else 1e-6
        while record := stream.read(16):
            sec, fraction, captured, _ = struct.unpack(endian + "IIII", record)
            frame = stream.read(captured)
            offset = 0
            if link == 1:
                offset = 14
                if len(frame) < offset or frame[12:14] != b"\x08\x00":
                    continue
            elif link not in (101,):
                continue
            yield sec + fraction * scale, frame[offset:]


def dns_name(data, offset):
    labels = []
    while offset < len(data) and data[offset]:
        size = data[offset]
        if size & 0xC0:
            return ".".join(labels), offset + 2
        offset += 1
        labels.append(data[offset : offset + size].decode("ascii", "replace"))
        offset += size
    return ".".join(labels), offset + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.captures:
        print(f"\n{path}")
        first = None
        for timestamp, packet in packets(path):
            if len(packet) < 20 or packet[0] >> 4 != 4:
                continue
            first = timestamp if first is None else first
            elapsed = timestamp - first
            ihl = (packet[0] & 15) * 4
            source = str(ipaddress.ip_address(packet[12:16]))
            target = str(ipaddress.ip_address(packet[16:20]))
            if packet[9] == 17 and len(packet) >= ihl + 20:
                udp = packet[ihl:]
                sport, dport = struct.unpack_from("!HH", udp)
                data = udp[8:]
                if (sport == 53 or dport == 53) and len(data) >= 17:
                    name, _ = dns_name(data, 12)
                    kind = "answer" if data[2] & 0x80 else "query"
                    print(f"{elapsed:8.3f} DNS {kind:6} {source}:{sport} -> {target}:{dport} {name}")
            elif packet[9] == 6 and len(packet) >= ihl + 20:
                tcp = packet[ihl:]
                sport, dport = struct.unpack_from("!HH", tcp)
                if sport != 51100 and dport != 51100:
                    continue
                thl = (tcp[12] >> 4) * 4
                payload = tcp[thl:]
                flags = tcp[13]
                flag_text = "".join(c for bit, c in ((2, "S"), (16, "A"), (1, "F"), (4, "R")) if flags & bit)
                if payload:
                    prefix = payload[:16].hex(" ")
                    print(f"{elapsed:8.3f} TCP {source}:{sport} -> {target}:{dport} {flag_text:4} len={len(payload):3} {prefix}")
                elif flags & 7:
                    print(f"{elapsed:8.3f} TCP {source}:{sport} -> {target}:{dport} {flag_text}")


if __name__ == "__main__":
    main()
