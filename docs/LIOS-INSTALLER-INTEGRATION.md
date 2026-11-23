# LIOS Lavender — ROM installer handoff

## Output contract

The `lavender-rom-installer-payload` workflow artifact contains two Android boot-image-v0 files for the Redmi Note 7 / 7S (codename `lavender`):

| File | Role |
| --- | --- |
| `boot.img` | Preferred verbose boot: console text stays visible over the Apple boot mark; persistent log ring and detailed code-signing diagnostics enabled. |
| `boot-quiet.img` | Reduced-diagnostics fallback using the same XNU/ADT/boot path. |

It also contains `SHA256SUMS`, `manifest.json` and `INSTALLER-INTEGRATION.md`.

## Installer integration

Use `boot.img` as the final boot image written by the existing custom ROM installer to the normal Android `boot` partition. Do not send it to `recovery`, and do not depend on `fastboot boot`. Preserve the previous ROM's original `boot.img` so the device can be restored.

The artifact does not rewrite `vbmeta`, `dtbo`, modem, persist or other partitions. It does not flash or repartition `userdata`.

## Boot trust

The build checks:
- the XNU kernel is a Lavender arm64 Mach-O;
- the appended DTB matches Lavender's Qualcomm MSM/board IDs;
- the embedded XNU payload carries the required root and bring-up arguments;
- the boot image is within the ABL size limit;
- the output files match the SHA-256 manifest.

These are build-time checks, not a claim that the physical ABL accepts the image. The handset must have an unlocked bootloader, and actual acceptance by that unit's boot chain remains to be tested. No vbmeta/AVB modification is performed automatically.

## Root filesystem dependency

The current reference boot configuration uses `rd=emmcroot0`. A compatible iOS HFSX root must already exist at that device. The root image is not included in this artifact, and this workflow does not decide or change the user's storage layout.
