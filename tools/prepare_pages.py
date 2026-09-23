"""Prepare the static GitHub Pages firmware files from a completed ESP-IDF build."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
version = (root / "VERSION").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[+-][0-9A-Za-z.-]+)?", version):
    raise SystemExit(f"Invalid semantic version in VERSION: {version!r}")

inputs = {
    "bootloader": root / "build/bootloader/bootloader.bin",
    "partition table": root / "build/partition_table/partition-table.bin",
    "OTA metadata": root / "build/ota_data_initial.bin",
    "application": root / "build/SolarProxxie.bin",
}
missing = [f"{name}: {path}" for name, path in inputs.items() if not path.is_file()]
if missing:
    raise SystemExit("Build the firmware before preparing Pages:\n" + "\n".join(missing))

output = root / "site/firmware"
output.mkdir(parents=True, exist_ok=True)
full_image = output / "SolarProxxie-full.bin"
subprocess.run(
    [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32",
        "merge_bin",
        "-o",
        str(full_image),
        "--flash_mode",
        "dio",
        "--flash_freq",
        "40m",
        "--flash_size",
        "4MB",
        "0x1000",
        str(inputs["bootloader"]),
        "0x8000",
        str(inputs["partition table"]),
        "0x19000",
        str(inputs["OTA metadata"]),
        "0x20000",
        str(inputs["application"]),
    ],
    check=True,
)

ota_image = output / "SolarProxxie.bin"
shutil.copyfile(inputs["application"], ota_image)

manifest = {
    "name": "SolarProxxie",
    "version": version,
    "new_install_prompt_erase": False,
    "builds": [
        {
            "chipFamily": "ESP32",
            "improv": False,
            "parts": [{"path": "SolarProxxie-full.bin", "offset": 0}],
        }
    ],
}
(output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

release = {
    "version": version,
    "target": "original ESP32",
    "flash_size": "4MB",
    "idf": "5.4.2",
    "full_install": {
        "file": full_image.name,
        "bytes": full_image.stat().st_size,
        "sha256": hashlib.sha256(full_image.read_bytes()).hexdigest(),
        "erases_configuration": True,
    },
    "ota": {
        "file": ota_image.name,
        "bytes": ota_image.stat().st_size,
        "sha256": hashlib.sha256(ota_image.read_bytes()).hexdigest(),
    },
}
(output / "release.json").write_text(json.dumps(release, indent=2) + "\n", encoding="utf-8")
print(json.dumps(release, indent=2))
