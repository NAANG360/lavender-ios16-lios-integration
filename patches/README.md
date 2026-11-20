# Patches

The base XNU patch series lives in [`aLpHa-Git-69/ios-lavender`](https://github.com/aLpHa-Git-69/ios-lavender) (`xnu/patches/`, branch `lavender`, pinned at `83356567124670f50a320b8ecc7c04f7ccf8f8e0`). That tree is shared with other devices and is used **read-only** here. This directory is the source of truth for the lavender delta. Lavender changes are made here and are not committed to `ios-lavender`.

## Included here

| Patch | Purpose | Status |
| --- | --- | --- |
| `0191-lavender-touch-stub-common-headers.patch` | Build the shared `santoni_touch_probe()` entry point as an unsupported-board stub and keep the common headers | In the pinned base already; kept for reference |
| `0192-sdhci-wait-card-ready-after-write.patch` | Wait for DAT0 + `TRAN`/`READY_FOR_DATA` (CMD13) after every PIO write; the eMMC write fix | On device: reached the lock screen |
| `0200-lavender-usb-gadget.patch` | SDM660 USB device (DWC3) with the CDC-ECM network and live log; starts the input and USB drivers on lavender | Host-tested; not yet built or run on the device |
| `0201-lavender-pmic-keys.patch` | Power / Volume Up / Volume Down through the SDM660 SPMI arbiter v3 | Host-tested; not yet built or run on the device |

`0190-lavender-gic-rwp-and-reset-code-clean.patch` is also part of the lavender series (GIC RWP wait and reset cleanup). It is in the base fork and is not duplicated here.

## Applying

Order: the base series, then the patches here that the base does not already carry (`0192`, `0200`, `0201`). Work in a local checkout. Do not push to `ios-lavender`.

```bash
git clone -b lavender https://github.com/aLpHa-Git-69/ios-lavender.git
cd ios-lavender
git checkout 83356567124670f50a320b8ecc7c04f7ccf8f8e0
cp /path/to/lavender-ios16/patches/0192-*.patch xnu/patches/
cp /path/to/lavender-ios16/patches/0200-*.patch xnu/patches/
cp /path/to/lavender-ios16/patches/0201-*.patch xnu/patches/
python3 tools/extract_drivers.py   # 0200 changes santoni_sdhci.c: refresh drivers/ mirrors
bash tests/run_tests.sh
```

The base `xnu.yml` workflow applies every `xnu/patches/*.patch` with `git apply` and fails if a critical patch is skipped. See `docs/BUILD.md` for how the kernel is built.

On Windows, clone with `-c core.autocrlf=false`. A CRLF checkout makes `git apply` reject LF patches such as `0192`.

## 0192: the 30 s vs 1 s timeout

The working patch uses `CACHE_FLUSH_TIMEOUT_NS` (**30 s**). A 1 s variant using `CMD6_BUSY_TIMEOUT_NS` was built as `0192b` (CI run `44636855966`, kernel `2a6b5421…`) and **rejected**: it stopped at `cc_fips_test` because some writes need more than one second. Do not shorten the timeout without a card-specific measurement.

## 0200: USB device

The base tree already had `sweet_usb.c` (patches `0072`–`0077`). It compiled for `LAVENDER`, but with the SM7150 addresses. It also never ran: its only caller is `sweet_ufs_init()`, and that function does not run on an eMMC phone.

- Addresses come from the phone's DTB (`tools/fdtdump.py` on `lavender_dtb.bin`): DWC3 core `0x0a800000`, Qualcomm wrapper (QSCRATCH) `0x0a8f8800`, QUSB2 **v1** PHY `0x0c012000`, GCC `0x00100000`. The GDSC and branch clock offsets come from Linux `gcc-sdm660.c` (`usb_30_gdsc` `0xf004`, master `0xf008`, sleep `0xf00c`, mock_utmi `0xf010`, cfg_noc_usb3_axi `0x5018`, aggre2_usb3_axi `0xf03c`, ahb2phy `0x6a004`, rx0_usb2_clkref `0x88018`).
- Boot-arg `sweetusb`:
  - `1` (default): read-only. DWC3 and the PHY are read only if the GCC says the block is powered and clocked.
  - `2`: starts the CDC-ECM gadget. The phone is `ue0` 192.168.7.2/24 and sends the console as UDP to 192.168.7.1:5140.
  - `3` (experimental): first turns on the USB30 GDSC and the seven branch clocks from the GCC, then does what `2` does. Lavender boots the shim from `recovery`, not with `fastboot boot`, so ABL may leave USB off. No RCG, reset, PMIC regulator or PHY tuning writes.
- If ABL left the core in host or OTG mode, it is put in device mode before the device soft reset.
- The live log reads the `santoni_sdhci.c` console ring through new readers, `santoni_log_copy()` and `santoni_log_position()`. The `sweet_ufs.c` ring stays empty on lavender.
- `sweet_keys_init()` and `sweet_usb_probe()` are called from `IOFindBSDRoot`, after the eMMC probe. Both are idempotent.

## 0201: buttons

`sweet_keys.c` was the topaz driver (SM6225, arbiter v5). From the DTB, SDM660 has a v2/v3 arbiter:

- core `0x0800f000`, observer `0x09400000`, cnfg `0x0800a000`, ee 0;
- the APID table is at core + `0x800`, there is no APID count register (512 APIDs), and each observer channel is at obsrvr + `0x8000 * apid`.

| Key | Source | Bit |
| --- | --- | --- |
| Power | PM660 (SID 0) PON `0x800`, KPDPWR | `PON_INT_RT_STS` bit 0 |
| Volume Down | PM660 PON, RESIN | `PON_INT_RT_STS` bit 1 |
| Volume Up | PM660L (SID 2) GPIO7 = `gpio@c600`, active low | `INT_RT_STS` (`0xc610`) bit 0 |

The driver is still read-only: the only MMIO write is the EXT_READL command. If the GPIO is not an enabled input, Volume Up is withheld and the PMIC is left unchanged. `sweetkeys=0` disables the driver. The `camera_*` entries in the DTB are reference-board leftovers (lavender has no camera key) and are not polled.

## Tests

- `tests/host/` (this repo) compiles the real patched `sweet_usb.c`/`sweet_keys.c` against simulated registers. See `tests/README.md`.
- The base tree's `tests/run_tests.sh` covers SDHCI (`0192`), GPT, the log ring, GIC and the patch structure. With `0200`/`0201` added and the mirrors refreshed it passes completely.

## 0191 note

The touch entry point is still a stub. It compiles as an unsupported-board stub so the kernel builds for lavender while the Novatek driver is missing. It is not a working touch driver.
