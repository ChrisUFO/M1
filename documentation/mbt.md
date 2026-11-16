<!-- See COPYING.txt for license details. -->

# M1 Build Tool

The M1 firmware can be built with STM32CubeIDE or Visual Studio Code.

## Installation  

Clone the repository. Install the required software and extensions:

* For STM32CubeIDE users:

- The code has been built with STM32CubeIDE 1.17.0.
Download: https://www.st.com/en/development-tools/stm32cubeide.html (STM32CubeIDE 1.17.0, GNU Tools for STM32 12.3.rel1),
and https://www.st.com/en/development-tools/stm32cubeprog.html.

* For Visual Studio Code users:

- The code has been built with arm-none-eabi-gcc 14.2.Rel1.
Download: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads (14.2.Rel1)

* For Linux users:

- Install ARM GCC toolchain (e.g., `arm-none-eabi-gcc`), Ninja, and srecord via yay
- On Debian/Ubuntu: `sudo apt install gcc-arm-none-eabi ninja-build srecord`

- VSCode download: https://code.visualstudio.com/download

Extensions installation:
+ [CMake Tools](https://github.com/microsoft/vscode-cmake-tools)
+ [Cortex-Debug](https://github.com/Marus/cortex-debug)
+ [Ninja](https://ninja-build.org/) (follow steps below)

- Chocolatey (package manager) installation:
Download: https://sourceforge.net/projects/chocolatey.mirror/

Prerequisites:
Before running the installation command, ensure that you have Chocolatey itself installed. You can find official installation instructions on the Chocolatey website. 

Installation Command:
Open an elevated Command Prompt or PowerShell session (right-click and select "Run as administrator") and execute the following command:

```powershell
choco install ninja
```

Verification:
After the installation is complete, you can verify that Ninja is installed correctly and added to your system's PATH environment variable by opening a new terminal window and running:

```powershell
ninja --version
```

## Build

* For Linux users:
```bash
make
```
Output: `./artifacts/` (`M1_v0.9.0.0-UFO.elf`, `.bin`, `.hex`, `_wCRC.bin`)

**`_wCRC.bin` is the only flashable artifact.** The boot failsafe reads the CRC
word at `0x080FFC14` unconditionally, and only the CRC-appended image carries
it; the `.bin`, `.hex`, and `.elf` stop at `0x080FFC14`. A device provisioned
from one of those boots and runs but shows `FW CORRUPT!` on every boot, with
`boot_attempts` counting up in the backup registers. See
[Firmware image and update](../docs/protocols/firmware-update.md#provisioning-which-artifact-to-flash).

* For Visual Studio Code users:
- Select build type from PROJECT STATUS - Configure (gcc-14_2_build-release, gcc-14_2_build-debug).
- Click the Build icon.

* For STM32CubeIDE users:
Build in the IDE.

## Build directories

* For Linux users:
Firmware is built in the folder `./build/` with output copied to `./artifacts/`.

* For Visual Studio Code users:
Firmware is built in the folder ./out/build/gcc-14_2_build-release for release configuration and ./out/build/gcc-14_2_build-debug for debug configuration.

* For STM32CubeIDE users:
Firmware is built in the folder ./Release for release configuration and ./Debug for debug configuration.

## SRecord (srec_cat) – download, install, and use for CRC

The **srec_cat** tool from the [SRecord](https://srecord.sourceforge.net/) package is used to append CRC-32 bytes to the firmware binary.

> **Windows without SRecord:** `srec_cat` and
> `tools/emulate_srec_cat_crc.py` are byte-equivalent emitters of the same
> layout (crop at `0x000FFC14`, CRC-32/MPEG-2 appended little-endian, output
> exactly `0xFFC18` bytes) for images of exactly `0xFFC14` bytes — the only
> shape the canonical build produces. For shorter images `srec_cat` pads with
> `0x00` and exits 0 while the Python emitter refuses, so the equivalence is
> stated for the image size the build actually emits. The canonical build
> resolves whichever one is available — `srec_cat` when it is on `PATH`, the
> Python emitter otherwise — and `sh scripts/verify_crc_parity.sh` proves they
> agree against `scripts/crc_parity_golden.txt`. Installing SRecord is
> therefore optional on Windows; see [Build and debug](../docs/build-and-debug.md).

### Download and install

- **Tool:** `srec_cat` (part of SRecord)
- **Download:** https://srecord.sourceforge.net/download.html  
  Get the Windows distribution (e.g. ZIP), extract it, and ensure the directory containing `srec_cat.exe` is on your system **PATH** (e.g. add that directory to the PATH environment variable in Windows so you can run `srec_cat` from any terminal).

### Command-line usage

From a terminal (Command Prompt or PowerShell), in the directory that contains your built binary:

```text
srec_cat M1_v0.9.0.0-UFO.bin -binary -crop 0x0 0x000FFC14 -STM32_Little_Endian 0x000FFC14 -o M1_v0.9.0.0-UFO_wcrc.bin -binary
```

Where:

- **M1_v0.9.0.0-UFO.bin** – Input binary file (e.g. produced by STM32CubeIDE or the VS Code build after a successful build).
- **M1_v0.9.0.0-UFO_wcrc.bin** – Output binary file with CRC-32 bytes appended.
- **0x000FFC14** – Image size of the binary (fixed value for this firmware).

After running the command, use **M1_v0.9.0.0-UFO_wcrc.bin** (or the name you gave with `-o`) as the image with CRC for programming.

---

## Post-build command for CRC

**Required for provisioning: only the CRC-appended image validates at boot.** The
canonical CMake build (`./build.sh`, VS Code presets) emits it automatically, so
this section is only for a copy of the project that does not — an old checkout, or
the STM32CubeIDE project, where the step has to be added by hand (see below).
Ensure SRecord is installed and `srec_cat` is on your PATH (see [SRecord (srec_cat) – download, install, and use for CRC](#srecord-srec_cat--download-install-and-use-for-crc) above).
A build that does not check for the step — the CMake build does, and refuses to
emit an image without it — still succeeds when it is missing, and ships an image
the boot failsafe rejects: `FW CORRUPT!` on every boot.

* STM32CubeIDE users:

  - Make sure you have installed SRecord and added `srec_cat` to your PATH (see above).
  - Go to menu **Project → Properties → C/C++ Build → Settings → Build Steps → Post-build steps – Command**.
  - In the Command box, enter:

    ```text
    srec_cat $(OBJCOPY_BIN) -binary -crop 0x0 0x000FFC14 -STM32_Little_Endian 0x000FFC14 -o $(BUILD_ARTIFACT_NAME)_wCRC.bin -binary
    ```

  - After building the project, an extra binary file `M1_v0.9.0.0-UFO_wCRC.bin` with CRC bytes appended will be generated.
  - The post-build step is a per-configuration setting, and this repository's
    `.cproject` sets it in the **Release** configuration only. A **Debug** build —
    and therefore an IDE debug download — produces no `_wCRC.bin`, so a device
    flashed that way shows `FW CORRUPT!` on every boot even though the image is
    the one just built. To avoid that, either repeat the command above in the
    Debug configuration's Build Steps and download the resulting `_wCRC.bin`
    instead of the ELF, or debug against a Release `_wCRC.bin` and re-provision
    before the device leaves your desk (see
    [Provisioning](../docs/protocols/firmware-update.md#provisioning-which-artifact-to-flash)).

* VSCode users:

  - Make sure you have installed SRecord and added `srec_cat` to your PATH (see [SRecord (srec_cat) – download, install, and use for CRC](#srecord-srec_cat--download-install-and-use-for-crc) above).
  - Download and update the file `CMakeLists.txt` in the source folder (`<source folder>/CMakeLists.txt`).
  - After building the project, an extra binary file `M1_v0.9.0.0-UFO_wCRC.bin` with CRC bytes appended will be generated.

## Debug

* For Visual Studio Code users:
- Select debugger from RUN AND DEBUG (Debug with ST-Link, Debug with J-Link).
- Debugger setting can be changed in ./.vscode/launch.json.
- J-Link software download: https://www.segger.com/downloads/jlink/

* For STM32CubeIDE users:
Debug in the IDE.