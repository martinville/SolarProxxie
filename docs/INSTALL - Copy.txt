# Installation and flashing

## Hardware

Use an ESP32-WROOM / ESP32 Dev Module with the **original dual-core ESP32** and
at least 4 MB flash. ESP32-C3/C6/S2/S3 are not this initial target. Use a reliable
USB data cable and power supply. No connection to inverter electrical terminals
is required: this project uses Wi-Fi only. Do not open the inverter.

## Windows, first installation

1. Follow Espressif's [ESP-IDF 5.4.2 Windows installation guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/get-started/windows-setup.html).
   Install the tools installer / ESP-IDF environment with version **5.4.2** and
   ESP32 target tools. Use a short local installation path without spaces if possible.
   The installer supplies Python dependencies, CMake, Ninja and the Xtensa compiler.
2. Open the installed **ESP-IDF 5.4 terminal**. An ordinary PowerShell window does
   not automatically contain the correct tool paths. Confirm `idf.py --version`.
3. Git is included in typical installer environments. Obtain this project by
   extracting its source archive or cloning the repository URL you publish it to:

   ```powershell
   git clone <your-SolarProxxie-repository-URL> SolarProxxie
   cd SolarProxxie
   ```

   This project does not invent a public repository URL. If you already have this
   workspace, simply `cd C:\Users\martinv\Projects\SolarProxxie`.
4. Connect the ESP32. In **Device Manager → Ports (COM & LPT)**, note the port that
   appears when you plug it in, e.g. `COM5`. Do not assume that COM1 is the ESP32.
5. If no port appears, first try another data cable. Boards may use CP210x, CH340 or
   FTDI USB bridges; install the driver matching the board manufacturer's chip.
   Obtain it from that vendor, not a third-party driver bundle.
6. Build and flash:

   ```powershell
   idf.py set-target esp32
   idf.py menuconfig
   idf.py build
   idf.py -p COM5 flash monitor
   ```

   `menuconfig` is optional for defaults. Under SolarProxxie, leave replay off and
   LED GPIO at `-1` unless you know your board's LED pin. BOOT is fixed at GPIO0.
7. Exit the serial monitor with **Ctrl+]**. Close it before another program uses
   the same COM port. Subsequent changes: `idf.py -p COM5 build flash monitor`.

VS Code's Espressif ESP-IDF extension is optional. Select the same ESP-IDF 5.4.2
installation and use its terminal/build/flash commands. Avoid mixing Arduino or
another ESP-IDF version's environment with this project.

## Linux

Follow the [ESP-IDF 5.4.2 Linux guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/get-started/linux-macos-setup.html)
for distribution-specific prerequisites. For a Debian/Ubuntu development machine:

```sh
sudo apt-get update
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
mkdir -p ~/esp
cd ~/esp
git clone --branch v5.4.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
. ./export.sh
cd /path/to/SolarProxxie
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The board might use `/dev/ttyACM0` instead. Check `ls /dev/serial/by-id/` for stable
names. Your account may need membership in the serial-device group (often `dialout`):
`sudo usermod -aG dialout "$USER"`, followed by a fresh login. Do not run normal
builds as root. Run `. ~/esp/esp-idf/export.sh` in each new terminal.

## Flash layout and image selection

| Partition | Offset | Size |
|---|---:|---:|
| NVS | 0x9000 | 64 KiB |
| OTA metadata | 0x19000 | 8 KiB |
| PHY | 0x1b000 | 4 KiB |
| OTA A | 0x20000 | 1,920 KiB |
| OTA B | 0x200000 | 1,920 KiB |
| NVS keys, reserved | 0x3e0000 | 4 KiB |

There is no separate factory application; OTA A is used on first flash.
`idf.py flash` writes all required initial images at their correct offsets.
The GUI accepts only the **application** `build/SolarProxxie.bin`.
Do not change partition offsets on already-deployed devices through an application-only OTA.

## First hardware milestone

Before enabling MQTT or Debug Mode, provision the gateway, connect a test client
to its protected AP, and check DHCP/DNS and Internet access. Then connect the
Inteless dongle and confirm its cloud application continues to update for at
least several reporting cycles. Record results in [TESTING.md](TESTING.md).

## If flashing cannot connect

- Close the serial monitor and any other program using the port.
- Try `idf.py -p COM5 -b 115200 flash` with the correct port substituted.
- If automatic bootloader entry fails: hold BOOT, tap EN/reset, start flashing,
  then release BOOT after the tool connects. Reset normally after flashing.
- For **Setup Mode**, use the opposite timing: reset normally, then press BOOT
  during the application’s five-second detection window.
- A board held in the ROM bootloader cannot run this project's factory-reset logic.

## Project-local tooling used during development

`.tools/` and `.venv/` are local, ignored build tools and are not part of the firmware.
The optional `tools/bootstrap.py` downloads the full pinned SDK release archive;
it is large. Standard users should prefer the official ESP-IDF installer.
`tools/idf_local.py` invokes the project-local Windows installation when present.
