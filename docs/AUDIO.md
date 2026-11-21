# Audio

Status: **bring-up started (2026-11-21)**. No sound yet. Patch `0500` is the read-only
platform probe; it has not run on the phone. The full plan and the hardware evidence are
in [`work/briefs/audio-sdm660-brief.md`](../work/briefs/audio-sdm660-brief.md).

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
| `0500` | read-only probe: GCC LPASS GDSC/vote, Q6SS, LPASS SMMU, digital codec/MCLK/AVTIMER/pinctrl, SMEM TOC + ADSP partition items, APCS IPC | written; tests green; pending kernel build + device boot (`lavaud=1`) |
| `0501` | LPASS/ADSP power-up (SMMU vote, GDSC un-collapse, re-read Q6SS) | planned |
| `0502` | read `adsp.mdt` + `adsp.bNN` from the modem FAT16, MDT parse and range check | planned |
| `0503` | TrustZone PAS load (PAS 1: `INIT_IMAGE`, `MEM_SETUP`, `AUTH_AND_RESET`) | planned |
| `0504` | SMP2P out item + `slave-kernel` READY, GLINK `lpass` channels | planned |
| `0505` | APR `apr_audio_svc`, AFE/ASM/ADM service enumeration | planned |
| `0506` | PM660L codec over SPMI (ID, supplies, reset, MCLK, HP/SPK/MBHC) | planned |
| `0507` | LPASS digital codec + SWR master + AFE `INT4_MI2S_RX` port | planned |
| `0508` | ASM stream + ADM COPP to `INT4_MI2S_RX`, 1 kHz test tone | planned |
| `0509` | `/dev/lavender-audio` + the HAL plug-in (`Speaker`/`Hawking`/`Codec`) | planned |
| `0510`+ | microphone (`INT3_MI2S_TX`/ADC), jack/MBHC, volume, suspend | planned |

## Running the probe

Build the shim with the audio boot args and boot it like any other test image:

```
lavaud=1 lavauddelay=25
```

`lavaud=0` (or absent) keeps the driver silent. The thread waits 25 s after the eMMC
probe so the persistent log and the USB live log are up, then announces each access 2 s
in advance. Expected lines in `SANTONILOG1` / the UDP live log:

```
SWEET AUDIO: A0 platform probe in 25 s
SWEET AUDIO: gcc lpass_smmu_cbcr ... lpass_adsp_gdscr ...
SWEET AUDIO: LPASS ADSP domain is ON|OFF
SWEET AUDIO: q6 reset ... (only when the domain is on)
SWEET AUDIO: lpass smmu sCR0 ...
SWEET AUDIO: codec ... swr ...
SWEET AUDIO: smem TOC ... apps<->adsp ... item ... (SMP2P/GLINK ids)
SWEET AUDIO: apcs ipc ...
SWEET AUDIO: A0 probe done
```

If the domain is off after ABL, the probe deliberately skips the Q6SS, codec and SMMU
reads: a read of a gated LPASS block on this SoC can hang the bus. The next patch
(`0501`) is the one that turns the domain on and then re-reads them.

## References

- Hardware facts and the full ladder: [`work/briefs/audio-sdm660-brief.md`](../work/briefs/audio-sdm660-brief.md)
- Reusable ADSP/GLINK/APR shape: `refs/ios-topaz/xnu/patches/0144`–`0146`,
  `refs/ios-lavender/drivers/audio/hakios/`
- TrustZone PIL sequence: `xnu/patches/0241` (GPU zap) and Linux
  `drivers/soc/qcom/subsys-pil-tz.c`
- Vendor codec reference: `MiCode/Xiaomi_Kernel_OpenSource`, branch `lavender-q-oss`,
  `sound/soc/codecs/` (`wcd9335` is not used; the PM660L driver and `msm-dig-codec`
  are the ones to read)
