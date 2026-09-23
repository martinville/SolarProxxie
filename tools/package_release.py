"""Create local source/firmware archives after successful validation; no publishing."""
from pathlib import Path
import hashlib,json,re,zipfile,shutil
root=Path(__file__).resolve().parents[1]
dest=root/'dist';dest.mkdir(exist_ok=True)
version=(root/'VERSION').read_text(encoding='utf-8').strip()
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:[+-][0-9A-Za-z.-]+)?',version):
    raise SystemExit(f'Invalid semantic version in VERSION: {version!r}')
roots=['main','web','docs','tools','tests','.github']
files=[]
for directory in roots:
    files.extend(p for p in (root/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and 'private' not in p.parts)
files.extend(root/n for n in ['VERSION','CHANGELOG.md','CMakeLists.txt','sdkconfig.defaults','partitions.csv','README.md','LICENSE','NOTICE.md','.gitignore','.clang-format','package.json','package-lock.json'])
with zipfile.ZipFile(dest/f'SolarProxxie-{version}-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(files):z.write(p,Path(f'SolarProxxie-{version}')/p.relative_to(root))
images={'SolarProxxie.bin':('build/SolarProxxie.bin','0x20000'),'bootloader.bin':('build/bootloader/bootloader.bin','0x1000'),'partition-table.bin':('build/partition_table/partition-table.bin','0x8000'),'ota_data_initial.bin':('build/ota_data_initial.bin','0x19000')}
manifest={'version':version,'target':'esp32','flash_size':'4MB','idf':'5.4.2','hardware_validated':False,'images':{}}
for name,(path,offset) in images.items():
    p=root/path;shutil.copyfile(p,dest/name);manifest['images'][name]={'offset':offset,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(dest/'FLASH.txt').write_text(f'SolarProxxie {version} / original ESP32 / 4 MB\nHardware acceptance testing remains pending.\n\nIn an activated ESP-IDF terminal, from this directory:\npython -m esptool --chip esp32 -p COM5 -b 460800 write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 0x1000 bootloader.bin 0x8000 partition-table.bin 0x19000 ota_data_initial.bin 0x20000 SolarProxxie.bin\n\nReplace COM5 with your actual port. GUI OTA accepts only SolarProxxie.bin.\nSee docs/INSTALL.md in the source archive for full instructions.\n')
with zipfile.ZipFile(dest/f'SolarProxxie-{version}-esp32-4MB.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in [*images,'manifest.json','FLASH.txt']:z.write(dest/name,name)
print(json.dumps(manifest,indent=2))
