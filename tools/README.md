# Tools

| Script | Purpose |
| --- | --- |
| `pull-sweetlog.sh` | Run inside TWRP: dump the 8193-sector `SANTONILOG1` area from SYSTEM to `/tmp/sweetlog.bin` |
| `sweet-log.py` | Decode `sweetlog.bin` (header + 4 MiB ring) to text; `--salvage` for a blank header |
| `fix-datamigration.sh` | macOS/root: add `com.apple.DataMigration.plist` (`LastSystemVersion=20H392`) to the raw HFSX image and regenerate the sparse image |
| `build-shim.sh` | Build a lavender shim from a base checkout + kernel with the known-good boot args |

## Log pull

```bash
# Phone in TWRP, adb available
adb push pull-sweetlog.sh /tmp/
adb shell sh /tmp/pull-sweetlog.sh
adb pull /tmp/sweetlog.bin sweetlog.bin
python3 sweet-log.py sweetlog.bin boot.txt
```

A healthy header looks like:

```
# SANTONILOG1: version 1, ring 4194304, total 110468 bytes, flush #604
```

A header-only log (`total 0`, `flush #0`) means the eMMC write path never flushed.

## DataMigration helper

On the Mac, with the raw image at `~/ios16-root.img` and `bergamota` at `~/bergamota`:

```bash
sudo bash fix-datamigration.sh
# -> ~/ios16-root-dm.simg
```

It writes the plist to `/private/var/mobile/Library/Preferences` (the `/var` symlink resolves there), verifies it with `plutil`, unmounts and regenerates the sparse image. The first boot still migrates; later boots should skip.

## Shim

```bash
./build-shim.sh /path/to/ios-lavender /path/to/kernel.development.lavender
# -> /path/to/ios-lavender/shim/out/shim-boot.img
```

The default command line is the lock-screen build: `rd=emmcroot0`, `emmcrw=1 rootrw=1`, the CS flags, `sweetlog=1`, the GUI switches and `cpus=4`. Override `EXTRA_CMDLINE` to test a different configuration.

The base `shim/build.sh` also needs `resources/lavender_dtb.bin` and the product ADT (`d22ap.adt` from the iPhone X IPSW). Those are not distributed here.
