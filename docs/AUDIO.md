# Audio

Status: **bring-up started (2026-11-21)**. No sound yet. Patches `0500` (read-only probe),
`0501` (LPASS/ADSP power-up), `0502` (ADSP firmware dry run), `0503` (TrustZone PAS load),
`0504` (post-PAS inventory), `0505` (SDM660 SMP2P v1 layout + vendor ids) and `0506` (SMP2P
out 429 + kick) are written; `0500`–`0505` compile in the kernel CI. None has run on the
phone yet. The full plan and the hardware evidence are in
[`docs/AUDIO-BRIEF.md`](AUDIO-BRIEF.md).

## What the phone actually has

The port's first assumption ("no codec driver, port the WCD9335") was wrong for this
board. The live DTBO (`artifacts/backup/dtbo.img`) disables the SLIMbus audio controller
(`slim_aud`), so the `tasha` (WCD9335) and `tavil` (WCD9341) nodes are off. The enabled
card is `qcom,sdm660-asoc-snd` with:

- **PM660L analog codec** at SPMI SID 3, base `0xf000` (`qcom,pmic-analog-codec`, with
  the speaker OCP/boost and MBHC blocks);
- **LPASS digital codec** at `0x152c0000` and its **SoundWire master** at `0x152c1000`
  (`msm-dig-codec`, `msm_sdw_codec`); the WSA881x amplifiers are not fitted
  (`qcom,wsa-disable`);
- **ADSP** (LPASS Hexagon) at `0x15700000`, TrustZone PAS id **1**, firmware `adsp`,
  carve-out `0x92a00000` + 30 MiB;
- the stock vendor image's `audio_platform_info_intcodec.xml` maps the speaker to AFE
  port **`INT4_MI2S_RX`**, headphones/handset to `INT0_MI2S_RX` and the built-in mic to
  `INT3_MI2S`; the speaker mixer path is `INT4_MI2S_RX -> RX4/RX5 -> COMP -> Spkr BOOST
  -> SWR DAC_Port` on the PM660L.

So the path is `ASM stream -> ADM COPP -> AFE INT4_MI2S_RX -> LPASS digital codec/SWR ->
PM660L codec -> speaker`. SLIMbus and WCD9335 are not part of it.

## iOS side

iOS 16 on this port has no `IOAudio2` device; Apple's audio kernel drivers are closed and
not in the open-source XNU build. As in the topaz port, the user-visible device will be a
userland AudioServer plug-in in `/System/Library/Audio/Plug-Ins/HAL` presenting
`Speaker` (and the `Hawking` companion), writing 48 kHz stereo S16_LE frames to a kernel
`/dev/lavender-audio` node. The kernel then feeds the AFE path above. The topaz plug-in
(`refs/ios-topaz/userland/audio/audio_plugin.c`) is the template.

## Ladder (patches 0500–0599)

| Patch | What | State |
| --- | --- | --- |
| `0500` | read-only probe: GCC LPASS GDSC/vote, Q6SS, LPASS SMMU, digital codec/MCLK/AVTIMER/pinctrl, SMEM TOC + ADSP partition items, APCS IPC | **kernel CI build green** (run `44927793793`); pending device boot (`lavaud=1`) |
| `0501` | LPASS/ADSP power-up (SMMU vote, GDSC un-collapse, re-read Q6SS); restores the collapse on failure | written; host tests green; pending device boot (`lavaud=2`) |
| `0502` | read `adsp.mdt` + `adsp.bNN` from the modem FAT16, parse the ELF/hash table and check every segment against the `0x92a00000` carve-out (read-only dry run) | written; host tests green; pending device boot (`lavaud=3`) |
| `0503` | TrustZone PAS load (`lavaud=4`): refuse over stale SMEM, `INIT_IMAGE`, `MEM_SETUP`, copy the segments into `0x92a00000`, `AUTH_AND_RESET`, then read the Q6SS; `lavaudforce=1` overrides the stale check | written; host tests green; pending device boot |
| `0504` | read-only inventory after PAS (`lavaud=5`): wait up to 3 s for the ADSP's SMP2P item, dump the apps↔ADSP partition items, `$SMP` headers/entries and the GLINK fifo candidates | written; host tests green; pending device boot |
| `0505` | fix the SDM660 SMP2P **v1 packed header** (`feature_version`/`rem_loc_proc_id`/`valid_total_ent`, not the SM6225 layout) and print the vendor ids: AP→ADSP 429, ADSP→AP 443, GLINK 478/479/480 | written; host tests green; pending device boot |
| `0506` | `lavaud=6`: creates the AP SMP2P item 429 in the apps↔ADSP partition (hwlock 3, v1 header, `master-kernel`/`sleepstate`), kicks the ADSP on APCS bit 10 and waits up to 5 s for item 443's `slave-kernel` READY. First SMEM write of the series | written; host tests green; pending device boot |
| `0507` | GLINK `lpass` edge 478/479/480, channel open/version and APR `apr_audio_svc` | planned |
| `0508` | PM660L codec over SPMI (ID, supplies, reset, MCLK, HP/SPK/MBHC) | planned |
| `0509` | LPASS digital codec + SWR master + AFE `INT4_MI2S_RX` port | planned |
| `0510` | ASM stream + ADM COPP to `INT4_MI2S_RX`, 1 kHz test tone | planned |
| `0511` | `/dev/lavender-audio` + the HAL plug-in (`Speaker`/`Hawking`/`Codec`) | planned |
| `0512`+ | microphone (`INT3_MI2S_TX`/ADC), jack/MBHC, volume, suspend | planned |

## Running the probe

Build the shim with the audio boot args and boot it like any other test image:

```
lavaud=1 lavauddelay=25    # read-only probe
lavaud=2 lavauddelay=25    # probe + LPASS ADSP power-up
lavaud=3 lavauddelay=25    # + adsp.mdt/adsp.bNN dry run from the modem FAT16
lavaud=4 lavauddelay=25    # + TrustZone PAS load and ADSP start
lavaud=5 lavauddelay=25    # + post-PAS SMEM/SMP2P inventory (read only)
lavaud=6 lavauddelay=25    # + create SMP2P out 429, kick the ADSP, wait READY
```

`lavaud=0` (or absent) keeps the driver silent. The thread waits 25 s after the eMMC
probe so the persistent log and the USB live log are up, then announces each access 2 s
in advance. Expected lines in `SANTONILOG1` / the UDP live log:

```
SWEET AUDIO: A0/A1 probe (lavaud=N) in 25 s
SWEET AUDIO: gcc lpass_smmu_cbcr ... lpass_adsp_gdscr ...
SWEET AUDIO: LPASS ADSP domain is ON|OFF
SWEET AUDIO: smmu cbcr before/after ...        (lavaud>=2, only if off)
SWEET AUDIO: adsp gdscr before/after ...       (lavaud>=2, only if off)
SWEET AUDIO: q6 reset ...                      (domain on)
SWEET AUDIO: lpass smmu sCR0 ...
SWEET AUDIO: codec ... swr ...
SWEET AUDIO: smem TOC ... apps<->adsp ... item ... (SMP2P/GLINK ids)
SWEET AUDIO: apcs ipc ...
SWEET AUDIO: ADSP.MDT ... hash ... ADSP firmware dry run: N segments, ...  (lavaud>=3)
SWEET AUDIO: probe done (lavaud=N)
```

At `lavaud=1`, if the domain is off after ABL, the probe deliberately skips the Q6SS,
codec and SMMU reads: a read of a gated LPASS block on this SoC can hang the bus.
`lavaud=2` is the step that turns the domain on and then re-reads them; if the GDSC never
reports `PWR_ON`, the collapse bit is put back and the rest is skipped.

## References

- Hardware facts and the full ladder: [`docs/AUDIO-BRIEF.md`](AUDIO-BRIEF.md)
- Reusable ADSP/GLINK/APR shape: `refs/ios-topaz/xnu/patches/0144`–`0146`,
  `refs/ios-lavender/drivers/audio/hakios/`
- TrustZone PIL sequence: `xnu/patches/0241` (GPU zap) and Linux
  `drivers/soc/qcom/subsys-pil-tz.c`
- Vendor codec reference: `MiCode/Xiaomi_Kernel_OpenSource`, branch `lavender-q-oss`,
  `sound/soc/codecs/` (`wcd9335` is not used; the PM660L driver and `msm-dig-codec`
  are the ones to read)
