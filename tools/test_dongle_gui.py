"""Compile actual dongle GUI mapping logic with SDK substitutes; no radio/NAPT emulation."""
from pathlib import Path
import os
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'build' / 'gui-test'
out.mkdir(parents=True, exist_ok=True)
for header in ['app.h', 'esp_wifi.h', 'esp_netif.h', 'esp_netif_net_stack.h', 'lwip/etharp.h', 'lwip/inet.h', 'lwip/lwip_napt.h', 'lwip/sockets.h', 'lwip/tcpip.h']:
    path = out / header
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "gui_sdk.h"\n')
cc = shlex.split(os.environ['CC']) if 'CC' in os.environ else [sys.executable, '-m', 'ziglang', 'cc']
binary = out / ('dongle_gui_test.exe' if os.name == 'nt' else 'dongle_gui_test')
subprocess.run(cc + ['-std=c17', '-O1', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    f'-I{out}', f'-I{root / "tests"}', f'-I{root / "main"}', str(root / 'tests/dongle_gui_test.c'),
                    '-o', str(binary)], check=True, cwd=root)
subprocess.run([str(binary)], check=True, cwd=root)
