# gba-flashcartio

A Game Boy Advance (GBA) C library to access the SD card of the following flashcarts:
- EverDrive GBA X5 / Mini
- EverDrive GBA Pro
- EZ Flash Omega / OmegaDE

> EZ Flash Air is not supported (pull requests are welcome!).

The flashcart type is autodetected and FAT partitions are supported via [ELM-ChaN's FatFs library](http://elm-chan.org/fsw/ff).

- **FatFs reads work on all supported carts; writes are implemented for EverDrive GBA Pro.** Other carts report write protection.
- It uses either **DMA3** or **DMA1** for data copies, or regular copies when DMA is disabled.
- During SD access, flashcarts make part of the ROM inaccessible (*EverDrive X5 / Mini* disables the last 16 MB, *EverDrive Pro* maps FIFO registers into ROM space, and *EZ Flash* disables all ROM space). To prevent issues, by default, interrupts are briefly disabled (`REG_IME = 0`).
- **EverDrive X5 / Mini** notes:
  * Since the last 16 MB of ROM are unavailable while using the SD card, make sure your linker script places these functions in the first 16 MB of ROM or in RAM.
- **EverDrive Pro** notes:
  * Uses ~1 KB of EWRAM for a transfer buffer, plus the SD access functions.
  * ROM addresses `08000080..0800009F` expose the cartridge interface during SD access. Avoid reading this part of the header until the operation finishes.
  * Writes are synchronous and use one sector per cartridge command, waiting for completion before proceeding.
  * A write failure may leave a partial file or filesystem update. The driver blocks subsequent I/O after a failed C2 transaction; power-cycle and inspect the card rather than retrying automatically.
- **EZ Flash** notes:
  * Since ROM is unavailable while using the SD card, ~1 KB of static EWRAM will be taken by some functions.
  * The _EZ Flash Definitive Edition_ works great out of the box, but in the original one:
    * There's an autosave feature that copies the save file to the microSD card. It's triggered every time you write to SRAM, and it takes ~10 seconds to complete. This can cause conflicts if it tries to write the microSD card while you are reading from it. After writing to SRAM, let some time pass before you read the microSD card, or it will crash!
    * Your ROM wait states should be `3,2` or slower. `3,1` will certainly make it crash!

## Usage

Refer to the [example](example/src/main.cpp) to see how it works. It is written in C++ for demonstration purposes, but `gba-flashcartio` is a C library, fully compatible with both C and C++.

An already compiled .gba ROM is available in the [Releases](https://github.com/afska/gba-flashcartio/releases) section.

You can compile the example using Docker:

```bash
cd example

docker run -it \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd)/..":/opt/gba \
  -v "$(pwd)":/opt/gba/example \
  devkitpro/devkitarm:20241104 \
  bash -c 'cd /opt/gba/example && make rebuild'
```

## Writing files

Use the normal FatFs API after `flashcartio_activate()` and `f_mount()`:

```c
FIL file;
UINT written;
const char data[] = "Hello from GBA!\n";
FRESULT result = f_open(&file, "/hello.txt", FA_WRITE | FA_CREATE_ALWAYS);
if (result == FR_OK) {
  result = f_write(&file, data, sizeof(data) - 1, &written);
  FRESULT closed = f_close(&file);
  if (result == FR_OK) result = closed;
  /* Check result AND written == sizeof(data)-1 (a full card can short-write). */
}
```

`FA_CREATE_ALWAYS` replaces an existing file. Append, seek/overwrite, truncate,
mkdir, rename and unlink use the same FatFs implementation. `f_sync()` and
`f_close()` flush FatFs buffers; sector writes already wait for a zero completion
status, so `CTRL_SYNC` checks driver health without another flush command.
Do not access the filesystem concurrently or from an interrupt. The historical
`flashcartio_is_reading` flag is also set throughout writes.

Applications using sector APIs directly can call `flashcartio_can_write()`,
`flashcartio_write_sector()` and `flashcartio_sync()`. Do not mix raw writes with
an active FatFs mount. Write buffers may be unaligned or ROM-resident; each sector
is copied to RAM before the cartridge interface is enabled.

## Compile-time options

In `lib/sys.h`:
- `FLASHCARTIO_DISABLE_DMA` (default=`0`): Set to `1` to use regular copies instead of DMA. This is slower but can be helpful in some scenarios.
- `FLASHCARTIO_USE_DMA1` (default=`0`): Set to `1` to use DMA1 instead of DMA3. You'll need this if you also use DMA for audio, as DMA1/DMA2 have higher priority and will corrupt the SD reads. With this option, you can use DMA2 for audio and DMA1 for the SD card.
- `FLASHCARTIO_ED_ENABLE` (default=`1`): (*EverDrive*) Set to `0` to disable _EverDrive_ support.
- `FLASHCARTIO_ED_SAVE_TYPE` (default=`ED_SAVE_TYPE_SRM`): (*EverDrive*) Set your game's save type manually here (one of `ED_SAVE_TYPE_EEP`, `ED_SAVE_TYPE_SRM`, `ED_SAVE_TYPE_FLA64`, `ED_SAVE_TYPE_FLA128`). Unfortunately, this is required, since initializing the registers overwrites ROM configuration that is usually autodetected otherwise.
- `FLASHCARTIO_ED_DISABLE_IRQ` (default=`1`): (*EverDrive*) If you are absolutely sure that your interrupt code doesn't access the last 16 MB of ROM and you won't be calling `SoftReset` in the middle of a read (when `flashcartio_is_reading` is `true`), you can avoid disabling interrupts by setting this option to `0`.
- `FLASHCARTIO_EDPRO_ENABLE` (default=`1`): (*EverDrive Pro*) Set to `0` to disable _EverDrive Pro_ support. Include `lib/everdrivegbapro/io_edpro.c` in your build when enabled.
- `FLASHCARTIO_EDPRO_DISABLE_IRQ` (default=`1`): (*EverDrive Pro*) If you are absolutely sure that your interrupt code doesn't access ROM at all and you won't be calling `SoftReset` in the middle of a read (when `flashcartio_is_reading` is `true`), you can avoid disabling interrupts by setting this option to `0`.
- `FLASHCARTIO_EZFO_ENABLE` (default=`1`): (*EZ Flash*) Set to `0` to disable _EZ Flash_ support.
- `FLASHCARTIO_EZFO_DISABLE_IRQ` (default=`1`): (*EZ Flash*) If you are absolutely sure that your interrupt code doesn't access ROM at all and you won't be calling `SoftReset` in the middle of a read (when `flashcartio_is_reading` is `true`), you can avoid disabling interrupts by setting this option to `0`.

In `lib/fatfs/ffconf.h`:
- `FF_FS_READONLY` (default=`0`): Set to `1` to omit FatFs mutation APIs. It can also be supplied as a compiler definition, consistently for all translation units.
- `FF_FS_MINIMIZE` (default=`0`): Enables stat, free-space queries and mutation APIs such as mkdir, unlink, rename and truncate.
- `FF_FS_NORTC` (default=`1`): Modified files use the configured fixed date (2022-01-01). Set it to `0` and supply `get_fattime()` for real timestamps.
- `FF_FS_EXFAT`: exFAT support can be disabled, as this can cause patent issues especially for commercial homebrew.

## Thanks to

- **asie** for the FatFs library recommendation.
- **Xilefian** for the [ezfo-disk_io](https://github.com/felixjones/ezfo-disc_io) development.
- **TotalJustice** for EZfo code improvements in [this gist](https://gist.github.com/ITotalJustice/b6c2f630c6ac5fff1e8b117681e27abd).
- **krikzz** for open sourcing [gba-ed-pub](https://github.com/krikzz/gba-ed-pub).
