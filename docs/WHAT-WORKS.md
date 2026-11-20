# What works / what does not

This is the honest state of the lavender port as of **2026-11-19**. The device boots to the iOS 16.7.16 lock screen; it is not a usable phone.

## Works

| Subsystem | Status | Evidence |
| --- | --- | --- |
| XNU kernel | Works | `kernel.development.lavender`, Mach-O arm64, `xnu-8796` |
| CPU | Works | 4× Cortex-A73 Gold, 16 KiB pages; `SWEET CPU: requested=4 selected=4` |
| GICv3 | Works | GIC distributor/redistributor, SPIs; timer interrupts |
| Timers | Works | Virtual and physical timer, 19.2 MHz reference |
| eMMC enumeration | Works | CMD0/1/2/3/9/7/8, EXT_CSD, 25 MHz 8-bit high-speed |
| eMMC reads | Works | PIO/SDMA, LBA0 MBR check, HFSX mount |
| eMMC writes | Works | CMD24/CMD25, journal replay, log ring; no CMD24/25 timeouts after patch 0192 |
| HFSX root | Works | `rd=emmcroot0`, read-write, `/System`, `/private/var`, `/var` symlink |
| Framebuffer | Works | AppleCLCD 1080×2340, stride 4320, `0x9d400000` |
| IOSurface / software composition | Works | `SWEET SURF` surfaces, `SWEET FB` swaps |
| `launchd` | Works | PID 1, `mount-phase-1/2`, `Early boot complete` |
| `backboardd` | Works | IOKit matches, surfaces, display on, swaps |
| `SpringBoard` | Works | Execs, opens `AppleKeyStore`, `IOPMrootDomain` |
| iOS boot UI | Works | Apple logo, progress bar, lock screen |
| Code signing bring-up | Works | `cs_enforcement_disable=1`, `unentitled_ios_sim_launch=1`, `launchd_unsecure_cache=1` |
| AMFI / AKS stubs | Partial | Built-in AMFI policy, `AppleKeyStore` user client; `WrapKey` (selector 10) returns `kIOReturnUnsupported` |
| Buttons | Works | Patch `0201`: Power (PM660 KPDPWR), Volume Down (RESIN), Volume Up (PM660L GPIO7) over the SPMI arbiter v3, `SWEET HID: key … (1 clients)` |
| USB network | Works | Patches `0200`, `0210`–`0213`: QUSB2 PHY power-up and UTMI mux release, Linux DWC3 bring-up, CDC-NCM. Windows 11 binds UsbNcm (426 Mbps link), `ping 192.168.7.2` 70/70, live kernel log on UDP 5140 |
| Panel orientation | Works | Portrait and upright, full 1080×2340 panel (`docs/images/lock-screen.jpg`) |
| Persistent log | Works | 4 MiB `SANTONILOG1` ring on SYSTEM; `sweet-log.py` decodes it |
| DataMigration skip | Partial | `com.apple.DataMigration.plist` with `LastSystemVersion=20H392`; first boot still migrates, later boots should skip |
| Battery fallback | Partial | Static 100 %, 4200 mV; no gauge |
| Watchdog stub | Partial | `IOWatchdog` accepts check-ins, never fires; `wdt=-1` |

## Does not work yet

| Subsystem | Status | Why / next step |
| --- | --- | --- |
| Touch | Missing | Novatek NT36672 on QUP1 I2C (`0x62`); only probe/stub patches. Need Novatek protocol + PM660 LDO11 + GPIO 66/67 |
| Camera button | N/A | Lavender has no camera key; the DTB `camera_*` entries are reference-board leftovers |
| GPU acceleration | Missing | Adreno 512 has no driver; empty `IOAcceleratorES` keeps QuartzCore from waiting 30 s; rendering is software |
| PosterBoard / lock-screen poster | Crash | `PosterBoard` traps in `-[PBFPosterExtensionDataStore _stateLock_setupSwitcherConfigurationForFirstTime]` when SpringBoard prewarms it (symbolicated, see TROUBLESHOOTING); `sweetnopb=1` (patch `0217`) keeps it from running for the isolation test |
| Camera | Missing | Camera stack and button are not implemented |
| Wi-Fi | Missing | No WCN3990 firmware/driver |
| Bluetooth | Missing | `bluetoothd` respawns without hardware |
| Audio | Missing | No codec driver |
| Battery gauge | Missing | Static fallback only |
| Lock screen colours | To verify | Date, clock and status bar render red/orange in the lock screen photo: wallpaper tint or framebuffer colour format, not checked |
| RTC | Partial | Shows 1969/December 31 because the PM660 RTC is not read; `sweetrtc` (patch `0216`) supplies a build-time base and the clock advances from it |
| `preboot` | Partial | `libignition: failed to lookup preboot: 25`; boot continues unsealed |
| Data volume | Partial | `mount: data volume missing, but not required in env: 0`; `/private/var` is on the root HFSX |
| Second boot | Partial | Runs without the data migration (`sweetlog-usbkeys-0202`), but takes ~18 min to the lock screen surfaces |
| Daily driver | No | This is a research bring-up |

## Boot milestones observed in the log

```
Darwin Kernel Version 22.6.0
SWEET CPU: requested=4 selected=4 (limit=8)
SWEET CPU: 4/4 CPUs initialized, SGI1 verified
WHYRED SDHCI: enumeration complete: 25 MHz, 8-bit, high-speed
BSD root: emmcroot0
hfs: mounted iOSRoot on device b(2, 0)
exec launchd
WHYRED LOG: first flush ok (13300 bytes, header ok)
Early boot complete. Continuing system boot.
SWEET FB: display on
SWEET FB: swap 1..6
exec SpringBoard
```

The lock screen is the current end state. There is no panic and no eMMC timeout in the successful boot.

## Known-good artifacts (not distributed here)

| Artifact | Size | SHA-256 |
| --- | --- | --- |
| Kernel `kernel.development.lavender` (0192, 30 s wait) | 17,836,144 B | `793d232f7c56be8e351e2fe6246b21515f20835c508e2cdbb72016d3830a87be` |
| Shim `shim-boot-lavender-0192-lite-cpus4-2026-11-19.img` | 6,709,248 B | `ba282d7977a23189e084705606f71dbf2c226fbbc02475bc7891a6c4c44e9425` |
| Root raw image with DataMigration plist | 16 GiB | hash printed by `fix-datamigration.sh` (not distributed) |
| Root sparse image flashed to `userdata` | 11,626,409,816 B | `cec991082f4e7bf2ae30a5ebcabf75b096d44617f410a93a835b2fc324f971e0` |

The 1 s timeout variant (`0192b`, kernel `2a6b5421…`) was rejected: it stopped at `cc_fips_test`. The 30 s variant is the one that reached the lock screen.
