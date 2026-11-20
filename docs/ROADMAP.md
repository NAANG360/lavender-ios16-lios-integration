# Roadmap

The port boots to the iOS 16.7.16 lock screen. The next work is ordered by what blocks actual use.

## P0 — make the device usable

1. **Novatek NT36672 touch**
   - QUP1 I2C at `0x0c175000`, address `0x62`, IRQ GPIO 67, reset GPIO 66, VDDIO PM660 LDO11.
   - Implement the Novatek protocol, power-up sequence and `SweetHIDTouch` events.
   - Verify with the probe patches `0184`–`0188` and a raw I2C scan before writing the driver.
   - Check the panel variant: Synaptics uses `0x20`; this unit is Novatek.

2. **PM660/PM660L buttons**: patch `0201` is written and host-tested; the device boot is next.
   - Power (PM660 KPDPWR), Volume Down (RESIN) and Volume Up (PM660L GPIO7) are mapped to HID. Lavender has no camera key.
   - Check the `SWEET KEYS:` lines in `SANTONILOG1` and the `key=N down=…` events on a press.
   - Power long-press should also be handled cleanly; the PMIC hardware reset is the fallback.
   - This unblocks rebooting and testing the second boot.

3. **USB gadget**: patch `0200` is written and host-tested; the device boot is next.
   - DWC3 device mode with CDC-ECM and the UDP live log: `sweetusb=1` (read-only probe), `2` (gadget), `3` (also power up the GDSC/clocks).
   - On the first boot, read `SWEET USB: GDSC … (on|off)`. If ABL leaves the PHY regulators off, the next step is the PM660 LDOs plus the DTB `qcom,qusb-phy-init-seq`.
   - Windows has no CDC-ECM driver: add an NCM function, or use macOS/Linux as the host.
   - This gives `adb`/live logs and makes recovery much easier.

4. **Panel orientation**
   - The lock screen is rotated 90°. Fix the panel/ADT orientation or add a framebuffer rotation.
   - Verify the touch coordinate transform after rotation.

5. **Second-boot check**
   - Once the `0201` buttons work on the device, reboot and confirm the `DataMigration` plist skips the long migration.
   - If it does not, inspect `datamigrator` and the data volume.

## P1 — platform completeness

6. **Data volume / preboot**
   - `libignition: failed to lookup preboot: 25` and `mount: data volume missing` are still present.
   - Create a proper data volume or bind the preboot paths; reduce the first-boot migration.
   - whyred reached its home screen without a full preboot, so this may be a warning rather than a hard gate.

7. **GPU**
   - Adreno 512 has no iOS driver. The empty `IOAcceleratorES` is only a workaround.
   - Investigate a Mesa/freedreno path or a minimal `IOAccelerator` that serves `IOSurface`.
   - The topaz port has GPU work on a related Adreno generation; use it as a reference, not a transplant.

8. **RTC**
   - Set the PMIC RTC so the date is not 1969/December 31.

9. **Wi-Fi / Bluetooth**
   - WCN3990 firmware/driver is missing. The whyred/topaz trees have reference code.

10. **Audio**
    - No codec driver. The PM660/PM660L audio path needs bring-up.

11. **Battery gauge**
    - The current fallback is static (100 %, 4200 mV). Add the PM660 gauge.

## P2 — research

12. **4 KiB pages / 8 cores**
    - XNU supports 4 KiB on arm64, but the iOS 16 userland assumes 16 KiB.
    - The 16 KiB Gold-only path is the supported one; 4 KiB/8 cores is a separate experiment.

13. **Performance**
    - Software rendering, synchronous eMMC writes and a single boot CPU were the main costs.
    - With `cpus=4` and the eMMC write fix, the lock screen is reachable; the UI is still slow.

14. **Daily driver**
    - Not a goal. The port has no data protection, no SEP, permissive AMFI/sandbox and no usable cellular/Wi-Fi/audio.

## How to help

- Test on other lavender variants (panel/touch differences) and report the I2C addresses.
- Provide sanitized boot logs and `SANTONILOG1` dumps.
- Send patches for touch, buttons, USB or orientation with a clear test procedure.
- Do not include Apple binaries, personal data or Apple IDs in issues.
