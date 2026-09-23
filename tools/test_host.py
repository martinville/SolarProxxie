"""Compile and execute the actual firmware pure logic on the development PC.
Uses CC when specified, otherwise a local Zig C compiler (pip install ziglang).
"""
from pathlib import Path
import os,shlex,subprocess,sys
root=Path(__file__).resolve().parents[1]
sdk=Path(os.environ.get('IDF_PATH',root/'.tools/esp-idf-v5.4.2'))
mb=sdk/'components/mbedtls/mbedtls'
out=root/'build/host';out.mkdir(parents=True,exist_ok=True)
cc=shlex.split(os.environ['CC']) if 'CC' in os.environ else [sys.executable,'-m','ziglang','cc']
includes=[root/'main',root/'tests/stubs',sdk/'components/json/cJSON',mb/'include',mb/'library']
sources=['tests/host_tests.c','tests/stubs/platform.c','main/protocol/inteless_parser.c','main/protocol/telemetry_store.c','main/protocol/stream.c','main/protocol/dongle_identity.c','main/protocol/cloud_emulator.c','main/protocol/pcap.c','main/modbus/modbus_decoder.c','main/modbus/register_map.c','main/config/config.c','main/config/validation.c','main/security/security.c','main/mqtt/discovery.c']
sources=[str(root/p) for p in sources]+[str(sdk/'components/json/cJSON/cJSON.c')]
sources.append(str(root/'main/config/migration.c'))
sources += [str(mb/'library'/f'{n}.c') for n in ['pkcs5','md','sha256','platform','platform_util','constant_time','cipher','cipher_wrap','aes']]
target=out/('host_tests.exe' if os.name=='nt' else 'host_tests')
cmd=cc+['-std=c17','-O1','-g','-DCJSON_NESTING_LIMIT=12','-DMBEDTLS_CONFIG_FILE="mbedtls_test_config.h"']+[f'-I{p}' for p in includes]+sources+['-o',str(target)]
if os.name!='nt':cmd.append('-lm')
subprocess.run(cmd,check=True,cwd=root)
native=out/('packet_decoder.exe' if os.name=='nt' else 'packet_decoder')
subprocess.run(cc+['-std=c17','-O2',f'-I{root / "main"}',str(root/'tools/packet_decoder/native.c')]+[str(root/p) for p in ['main/protocol/inteless_parser.c','main/protocol/telemetry_store.c','main/modbus/modbus_decoder.c','main/modbus/register_map.c']]+['-o',str(native)],check=True,cwd=root)
print('Native packet decoder ready:',native)
subprocess.run([str(target)],check=True,cwd=root)
