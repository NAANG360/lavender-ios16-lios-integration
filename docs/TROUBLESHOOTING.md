# Troubleshooting

## eMMC CMD24/CMD25 timeouts

**Symptom:** the SYSTEM log ring is empty or header-only; the kernel log shows:

```
WHYRED SDHCI: command 24 failed (arg 0x00220000, INT_STATUS 0x00018000 cmd-timeout, ...)
WHYRED SDHCI: recover: stopping an open-ended transfer
WHYRED SDHCI: recovery incomplete (5)
```

**Cause:** `sdhci_write_pio()` returned after the controller's transfer-complete, but the card was still programming and held DAT0 busy. The next CMD24/CMD25 arrived too early.

**Fix:** patch `0192` adds `sdhci_wait_write_ready()` after `sdhci_wait_transfer_complete()`:

1. delay 1 ms;
2. poll `PRESENT_STATE` until `!CMD_INHIBIT && !DATA_INHIBIT && DAT0` is high;
3. send CMD13 and require `TRAN` + `READY_FOR_DATA`;
4. on timeout, recover and return `ETIMEDOUT`.

The working timeout is **30 s** (`CACHE_FLUSH_TIMEOUT_NS`). A 1 s variant (`CMD6_BUSY_TIMEOUT_NS`) was rejected: it stopped at `cc_fips_test` because some writes need more than 1 s.

**Verify:** the ring header should show a growing `total` and `flush #N`; the log should contain `WHYRED LOG: first flush ok` and no CMD24/25 timeouts.

## The first boot takes forever (DataMigration)

**Symptom:** Apple logo + progress bar for tens of minutes; the log shows `com.apple.datamigrator` and `Early boot complete`, then the bar barely moves.

**Cause:** a fresh iOS root image has no migrated data. `DataMigration` runs on first boot. With `cpus=1` it takes ~30+ minutes and can crawl at 99 %. The boot UI is presented by `backboardd`; `SpringBoard` waits in `BKSSystemShellService _checkIn` until the migration ends.

**Fix / mitigation:**

- Use `cpus=4` (all Gold cores).
- Keep `com.apple.DataMigration.plist` with `LastSystemVersion = 20H392` in the root image. It lets later boots skip the migration; the first boot still migrates.
- Let the first boot finish. Do not reboot in the middle; the migration restarts.
- If it never advances, check `mount: data volume missing` and the data volume/`preboot` setup. The port has a single HFSX root; `/private/var` is a directory, not a separate APFS data volume.

**Whyred reference:** the whyred notes (2026-10-17) document the same gate and the v10 image with the plist. Without the plist the migration repeated on every boot.

## Only one CPU is active

**Symptom:** `SWEET CPU: requested=1 selected=1 (limit=8)`.

**Fix:** add `cpus=4` to the shim command line. The Silver A53 cores cannot run 16 KiB pages; do not try `cpus=8` with the 16 KiB kernel. The safe fallback is `cpus=1`.

## GPU / `IOAcceleratorES`

**Symptom:** `backboardd` repeatedly matches `IOAcceleratorES`; the UI is slow or a frame stalls.

**Cause:** Adreno 512 has no iOS driver. The kernel publishes an **empty** `IOAcceleratorES` service so `CAMetalContextCreate` does not wait 30 s. Rendering is software (`AppleCLCD` + `IOSurface`).

**What to do:** this is expected. The lock screen is reached with software rendering. GPU acceleration is a separate project (Mesa/freedreno, see topaz). Do not remove the empty service without providing a real accelerator; QuartzCore will wait 30 s per process.

## Touch does not work

**Cause:** the Novatek NT36672 driver is not implemented. The base tree has probe/stub patches (`0184`–`0188`) but no finished Novatek protocol, no PM660 LDO11 power-up and no `SweetHIDTouch` integration.

**Next:** implement the Novatek I2C protocol on QUP1 (`0x62`), GPIO 66 reset / GPIO 67 IRQ, PM660 LDO11, then feed events to `SweetHIDTouch`. Check the panel variant: Synaptics uses `0x20`.

## Physical buttons do nothing / USB is not detected

**Cause:** on kernels without patches `0200`/`0201`, the keys are not read and the USB device driver never starts. With the patches, check `SANTONILOG1`:

- `SWEET KEYS: unsupported arbiter version=…`: the arbiter is not the expected v3 layout. `… volume-up withheld`: the PM660L GPIO7 is not an enabled input. `read error`: the SPMI reads fail.
- `SWEET USB: controller unpowered or unclocked` with `sweetusb=1`: ABL left USB off; retry with `sweetusb=3`. `GDSC … TIMEOUT` or `clk … STILL OFF` with `sweetusb=3`: the GCC alone is not enough.
- No `SWEET KEYS`/`SWEET USB` lines at all: the kernel does not have `0200`.
- The host sees no network: Windows has no CDC-ECM driver. Use macOS/Linux and set 192.168.7.1/24 on the USB interface.

There is still no `adb`/`fastboot` while iOS runs.

**Recovery:**

1. Hold **Power** 20–30 s (PMIC hardware reset).
2. Try **Power + Volume Down** 20–30 s, then **Power + Volume Up** 20–30 s.
3. If nothing works, unplug the charger and let the battery drain. The eMMC root is not lost.
4. When off, charge and boot with **Power + Volume Up**.

Do not use EDL/test points unless you know the hardware; the phone is not bricked.

## Lock screen is rotated 90° / date is 1969

- The panel/ADT orientation is not fixed; the framebuffer is composed at 1080×2340 but the UI is rotated. This is a display/ADT issue, not a kernel panic.
- The RTC is not set, so iOS shows 1969/December 31. Set the PMIC RTC or let iOS sync time later.

## `launchd` dies with `CS_KILLED`

Use the bring-up flags:

```
cs_enforcement_disable=1 unentitled_ios_sim_launch=1 launchd_unsecure_cache=1
```

Without them, the unsealed root and the iOS platform binaries are killed.

## `libignition: failed to lookup preboot: 25`

The preboot volume is missing. The boot continues unsealed; whyred reached its home screen with the same message. It is not the current lock-screen gate. A proper preboot/data volume is future work.

## `securityd sel 10 -> 0xe00002c7`

`0xe00002c7` is `kIOReturnUnsupported`. Selector 10 is `kAKSWrapKey`; the built-in `AppleKeyStore` stub does not implement it. The boot continues. If it becomes a gate, implement `WrapKey`/`UnwrapKey` in the AKS stub.

## `CRASH driverkitd`

Known userspace crash (`sig 9`, reason namespace 1 code 0x7). It does not stop the boot. Do not chase it before touch/buttons/USB.

## `fastboot boot` does nothing / MIUI restores recovery

- `fastboot boot` is stubbed by this ABL: it returns `OKAY` but does not execute the image.
- `fastboot reboot recovery` boots Android and MIUI restores the stock recovery.
- Use `fastboot flash recovery <shim>` and **Power + Volume Up**.

## USB link drops during `userdata` flash

A long sparse flash can drop the link (`????????????`, "no link"). Reconnect the cable, re-enter fastboot and retry. Do not disconnect while the flash is in progress.

## Pulling the log

```bash
adb push tools/pull-sweetlog.sh /tmp/
adb shell sh /tmp/pull-sweetlog.sh
adb pull /tmp/sweetlog.bin sweetlog.bin
python3 tools/sweet-log.py sweetlog.bin boot.txt
```

`tools/sweet-log.py` accepts the raw 4,194,816-byte area. If sector 0 is blank, use `--salvage` only when the ring start really looks like a kernel log.
