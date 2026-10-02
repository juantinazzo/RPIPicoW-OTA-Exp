# RPIPicoW-OTA-Exp

OTA (Over the Air) Update Experiment on the Raspberry Pi Pico-W for my YouTube channel [DrJonEA](https://youtube.com/@DrJonEA)

Making use of [Jakub Zimnol](https://github.com/JZimnol) excellent [Firmware Over the Air](https://github.com/JZimnol/pico_fota_bootloader) bootloader to firmware upgrade my Pico-W.

The Firmware loader handles the update of a preloaded firmware held in flash to be the active application. It does that be reworking flash duing a reboot of the processor. The client applications needs to load the firmware from it's source.

## Prerequisites

+ `pico-sdk` `>= 2.2.0` (developed and tested against 2.3.1)
+ `arm-none-eabi-gcc` toolchain and CMake
+ `Python 3` (used by the build to append the SHA256 to each FOTA image)

Point the build at your SDK with `PICO_SDK_PATH`, e.g.

```
export PICO_SDK_PATH=/opt/pico-sdk
```

`WIFI_SSID` and `WIFI_PASSWORD` have to be in the environment when CMake runs,
they are baked into the firmware as compile definitions.

## Bootloader
I included the [pico_fota_bootloader](https://github.com/JZimnol/pico_fota_bootloader) as a submodule and integrated it into the `blu` and `grn` projects. Building either project then creates two binaries the bootloader and the application firmware.  Both must be loaded onto the Pico the first time.

The upstream `master` branch is used. Compared to the old v1 bootloader it adds

+ SHA256 hashing of the FOTA image, validated by the application before the
  update is applied
+ an AES ECB image encryption option (off by default, see below)
+ a rollback mechanism, so a firmware that does not commit itself before the
  next reset is swapped back out
+ separate linker scripts per SoC, selected from `PICO_PLATFORM`
+ bootloader logs, which this project redirects to UART

The application is linked with the bootloader's own linker scripts through
`pfb_compile_with_bootloader()`, which also adds the post build step that
produces the image to be served to the device:

```
build/src/<release>.bin             # flash this to the Pico initially
build/src/<release>_fota_image.bin  # serve this to the Pico to update it
```

`_fota_image.bin` is `<release>.bin` with a 256 byte block appended, the last 32
bytes of which are the SHA256 of the binary. Note that this is *not* the same as
`<release>.bin`, so make sure the test server serves the `_fota_image` variant.

The bootloader allocates flash as follows, and the application linker script
enforces the same layout, so the layout is stable across builds as long as the
bootloader and the application are built with the same SDK flash size:

```
0x10000000  Bootloader (36k)
0x10009000  Flash info partition (4k)
0x1000a000  Application slot        <-- .text starts here
            Download slot
```

Jakub talks about doing this through Bootsel, actually I uploaded through openocd and Debug Probe which worked flawlessly.

## Downloading the binary
For test purposes I download the firmware over HTTP. Due to memory size limits on the Pico I decided to download it in seperate 10KB chunks. I could have made this smaller or larger. I chose to make each chunk a seperate HTTP Get request, because the write of each chunk into firmware is going to halt my IP stack, so holding open a GET request mid transfer seemed like a risky idea.

The bootloader can only write whole 256 byte flash pages, so `segSize` has to be
a multiple of 256. `pfb_write_to_flash_aligned_256_bytes()` pads the last chunk
of the image on the way into the download slot, and the test server pads the
last chunk it serves, so the padding never becomes part of the image.

You will see the application is doing MD5 calculations on each segment and the total file using Caled Stewart [MD5 library](https://github.com/calebstewart/md5). This was purely for visual inspection during development. The integrity of the image itself is now checked properly by the bootloader with `pfb_firmware_sha256_check()` before the download slot is marked valid, and an image that fails the check is discarded rather than booted.

HTTP GET is implimented using [FreeRTOS coreHTTP](https://github.com/FreeRTOS/coreHTTP) library.  This is therefore using LWIP and [FreeRTOS Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) to support Sockets.

### A note on the flash writes and the second core
`flash_range_erase()`/`flash_range_program()` stall every core that is fetching
instructions from flash, and this project runs the FreeRTOS SMP port with
`configRUN_MULTIPLE_PRIORITIES`, so a task can be running from flash on core 1
while core 0 writes the download slot. This has not been a problem in practice,
but if you hit a hard fault during a download, `configRUN_MULTIPLE_PRIORITIES 0`
in `port/FreeRTOS-Kernel/FreeRTOSConfig.h` keeps every task on core 0 and is the
first thing to try.

## Test Server
I have a simple Python Flask server as the delivery mechanism for the firmware. See the py folder. This just send segments and prints out the MD5 of each segment sent.  It is picking up the binaries directly from the build folders.

The server rejects a `segSize` that is not a multiple of 256, as such a request
would desynchronise the write offsets on the device.

## Blue and Green Test Firmware
The test switches between two firmware releases Green (GRN) and Blue (BLU). Both are technically the same code, `grn` builds the sources in `blu/src`. The CMakeLists.txt file within the grn/src and blu/src inserts three definitions to distinguish the releases in behaviour:

```
RELEASE="GRN"
OTA_URL="http://vmu22a.local.jondurrant.com:5000/blu"
LED_GP=14
```

+ RELEASE is a string printed at boot up
+ OTA_URL is the firmware release to load next
+ LED_GP is the LED GPIO pad to flash for 20 seconds before trying to update


## Clone
This project makes use of subprojects. Clone with the --recurse-submodules switch.

## Build and flash
Both the grn and blu firmware need to be build seperately using the standard build process.
```
export PICO_SDK_PATH=/opt/pico-sdk
export WIFI_SSID=<your ssid>
export WIFI_PASSWORD=<your password>

cd blu
mkdir build
cd build
cmake ..
make
```

You need to the flash pico_fota_bootloader/pico_fota_bootloader.elf or pico_fota_bootloader/pico_fota_bootloader.uf2 on the Pico.
Then flash either blu.elf (or blu.uf2) or grn.elf (or grn.uf2) to start the process.

Both the application and the bootloader log over UART on TX GP16 / RX GP17
rather than over USB, so a single UART cable sees the whole boot sequence.

## Enabling image encryption
The bootloader's AES ECB encryption is off by default because it needs the
pycryptodome python package at build time and a key which has to be identical
for the bootloader and for every future firmware build:

```
pip install pycryptodome
cmake -DPFB_WITH_IMAGE_ENCRYPTION=ON -DPFB_AES_KEY=<up to 32 alphanumeric chars> ..
```

With encryption on, the build additionally produces
`<release>_fota_image_encrypted.bin`, which is the file to serve to the device.
