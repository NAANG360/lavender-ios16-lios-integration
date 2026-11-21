# audio on lavender (SDM660): what the hardware is, and the ladder

Status: research brief for the `0500`–`0599` series. Written 2026-11-21 after the
GPU (`0236`–`0253`) and Wi-Fi/modem (`0700`–`0706`) hard-path series. Nothing in this
brief has run on the phone yet; the device facts below come from the phone's own
device tree, its stock vendor image and the port's existing drivers.

## 1. The premise was wrong: there is no WCD9335 and no SLIMbus audio

The repo's `docs/HARDWARE.md` / `docs/ROADMAP.md` said "no codec driver; the
PM660/PM660L audio path needs bring-up", which is right, but it is easy to read that
as "port the WCD9335". The phone's own DTBO says otherwise:

- `artifacts/backup/dtbo.img` (the live partition) contains exactly one overlay:
  `fragment@0` targeting `slim_aud` with `status = "disabled"`.
  `python3 tools/fdtdump.py` on `work/dtbo0.bin` shows it. So the SLIMbus audio
  controller at `0x151c0000` and both `tasha` (WCD9335) and `tavil` (WCD9341) nodes
  are **disabled on this board**. Do not bring up SLIMbus, and do not port
  `wcd9335.c`; there is no such codec.
- The enabled sound card in the same DTB (`/soc/sound`, `status = "okay"`) is
  `qcom,sdm660-asoc-snd`, model `sdm660-snd-card`, `qcom,wsa-disable`, and its
  codecs are `msm-stub-codec`, **`msm-dig-codec`**, **`analog-codec`**,
  `msm_sdw_codec` and `msm-ext-disp-audio-codec-rx`.
- `analog-codec` is `/soc/qcom,spmi@800f000/qcom,pm660l@3/analog-codec@f000`:
  compatible `qcom,pmic-analog-codec`, `reg = <0xf000 0x200>`, SPMI interrupts
  `spk_cnp_int`, `spk_clip_int`, `spk_ocp_int`, MBHC/HPH/ear OCP, supplies
  `cdc-vdda-cp`/`cdc-vdd-pa` (PM660L BOB) and `cdc-vdd-mic-bias`, MCLK 9.6 MHz.
  **The PM660L itself is the codec, including the speaker amplifier.** There is no
  external smart PA in the audio path.
- `msm-dig-codec` is `/soc/.../analog-codec@f000/msm-dig-codec`, `reg = <0x152c0000 0>`:
  the LPASS digital codec block. Its SoundWire master is
  `/soc/msm-sdw-codec@152c1000` with the `wsa881x_en` children; with
  `qcom,wsa-disable` those WSA amplifiers are not fitted, but the SoundWire link
  between the LPASS digital codec and the PM660L codec is what carries the PCM.
- Extra LPASS nodes: `lpi_pinctrl@15070000` (codec reset on LPI GPIO 24, MCLK on
  LPI GPIO 18), `audio_ext_clk` with `qcom,mclk-clk-reg = <0x15020018>`, and
  `avtimer@150f700c`.

## 2. The real audio map (lavender)

| Piece | Address / node | Notes |
| --- | --- | --- |
| ADSP (LPASS Hexagon, QDSP6) | `qcom,lpass@15700000`, PAS id **1** | `qcom,pil-tz-generic`, firmware `adsp`, SMEM id `0x1a7`, XO 19.2 MHz, `qcom,proxy-timeout-ms = 10000` |
| ADSP carve-out | `0x92a00000`, **30 MiB** (`0x1e00000`) | `/reserved-memory/adsp_fw_region@92a00000`; the modem carve-out ends at `0x92a00000` |
| ADSP Q6SS registers | `0x15700000` (`reg = <0x15700000 0x100>`) | reset `0x014`, GFMUX `0x020`, PWR `0x030`, sleep `0x03c`, XO CBCR `0x038`, BHS `0x078`, v62 BHS `0x0c4` |
| LPASS power/vote | GCC `0x7d034` GDSC `hlos1_vote_lpass_adsp`, `0x7d014` clock `hlos1_vote_lpass_adsp_smmu` | the only LPASS controls the GCC driver exposes on SDM660 |
| LPASS SMMU | `/soc/arm,smmu-lpass_q6@5100000` | SMMUv2 register layout at `0x05100000` |
| LPASS digital codec | `0x152c0000`; master `0x152c1000` | `msm-dig-codec` + `msm_sdw_codec` |
| Codec (PM660L) | SPMI SID **3**, base `0xf000`, 0x200 bytes | analog codec + MBHC + speaker OCP; supplies on BOB |
| SMEM | `0x86000000`, 2 MiB | TOC at `0x861ff000` (`$TOC`), partitions `$PRT` |
| APCS IPC | `0x17911000 + 0x8` | GLINK mask `0x200` (bit 9), SMP2P mask `0x400` (bit 10) |
| SMP2P/GLINK IRQs | ADSP SMP2P SPI `0x9e`, GLINK SPI `0x9d` | `qcom,smp2p-adsp@17911008`, `qcom,glink-smem-native-xprt-adsp` |
| Codec reset | LPI GPIO 24 (`lpi_pinctrl@15070000`) | not TLMM GPIO 64; node name is misleading |
| MCLK | LPI GPIO 18 func2, register `0x15020018` | 9.6 MHz (`0x927c00`) |
| Primary MI2S pins | TLMM GPIO 12/13/14/15 | `pri_mi2s_*`, used by the reference TDM node, **not** by lavender's speaker |

### AFE ports (from the stock vendor image, not guesswork)

`work/audio_platform_info_intcodec.xml`, extracted from
`vendor.img` (`work/gpu-fw/.../images/vendor.img`), says:

- headphones / handset / line: `INT0_MI2S_RX`;
- speaker, speaker_external_1/2, speaker_vbat: **`INT4_MI2S_RX`**;
- built-in mic: `INT3_MI2S` (internal codec) or `SLIMBUS_0` (external-codec variant);
- speaker mixer path (`/vendor/etc/mixer_paths*.xml`): `INT4_MI2S_RX Audio Mixer
  MultiMedia1`, `INT4_MI2S_RX Channels Two`, `RX4 MIX1 INP1: RX4`, `RX5 MIX1 INP1:
  RX5`, `RX4/RX5 Digital Volume 67`, `COMP1/COMP2 Switch`, `SpkrLeft/Right BOOST
  Switch`, `SpkrLeft/Right VISENSE Switch`, `SpkrLeft/Right SWR DAC_Port Switch`.
  That is the PM660L speaker path, fed by the LPASS SoundWire DAC port; no
  `TAS2557`/`TFA98xx`/`MAX98927`/`WSA881x` control appears.

Consequence: the audio data path is
`ASM stream -> ADM COPP -> AFE INT4_MI2S_RX -> LPASS digital codec/SoundWire ->
PM660L analog codec (SWR DAC + SPK boost) -> speaker`.
The AP configures the PM660L over SPMI and the LPASS digital codec/SWR block; the
ADSP runs the AFE port and the PCM flow. This is the same shape as Android's
`msm-pcm`/`q6asm`/`q6adm`/`q6afe` + `msm-analog-codec` + `msm-dig-codec` stack.

## 3. What iOS side needs

There is no Apple `IOAudio2` device on this port: the iOS 16 kernelcache has the
closed `AppleHDA`/`AppleAOPAudioController` stack and none of it is in the
open-source XNU build. The practical path, already proven by `ios-topaz`
(`userland/audio/audio_plugin.c`, `audio_plugin.plist`) and by HakiOS
(`hakios.audio.c`), is:

1. a kernel `/dev/<board>-audio` node: 48 kHz stereo S16_LE frames from the
   mediaserverd HAL plug-in;
2. a userland AudioServer plug-in in `/System/Library/Audio/Plug-Ins/HAL` that
   presents `Speaker` + `Hawking` (+ `Codec`, + `Actuator`) to the iOS virtual
   audio device and writes the mixed frames to that node;
3. the kernel takes those frames into the AFE path above.

The topaz plug-in is the template; only the device name and the `/dev` node change.
For a first audible milestone the kernel can play a fixed test tone without the
plug-in (like `hakios.sound.c` `sound=2/3`), which is how the GPU series proved
CP/RB before any Metal driver.

## 4. Reference material: what is reusable, what is not

| Source | Reusable | Not reusable |
| --- | --- | --- |
| `refs/ios-topaz/xnu/patches/0144-topaz-audio-adsp.patch` | TrustZone PAS sequence (`INIT_IMAGE 1`, `MEM_SETUP 2`, `AUTH_AND_RESET 5`), MDT/ELF segment loader, SMP2P item layout, GLINK channel/queue implementation, "never boot over stale SMEM state" rule | addresses (SM6225 `0x53800000` carve-out), GPR/AudioReach protocol (`APM GET_SPF_STATE`), item IDs 429/443/478-480 |
| `refs/ios-topaz/xnu/patches/0145`, `0146` | codec/stream step organisation, RPM vote helper (TopazRpm), tone test shape | WCD937x over SoundWire, sia8159 amp, SM6225 RPMh |
| `refs/ios-lavender/drivers/audio/hakios/hakios.adsp.c`, `hakios.sound.c` | **APR** channel (`apr_audio_svc`), ASM/ADM/AFE command shapes, internal-codec AFE port config, SMMU SID mask trick, test-tone/stream setup | SM6375/POCO X5 Pro addresses, WCD938x/SoundWire bolero macros, AW88261 amp, virtio-sound |
| this repo `0700`/`lavender_wlan.c`, `0701`–`0706`/`lavender_modem.c` | the read-only probe format (announce + 2 s), GCC branch/GDSC read, SCM call convention (`scm_call`), Q6SS/RMB state read, **FAT16 modem-partition firmware reader**, eMMC read lock/retries | MSS-specific reset (ADSP uses PAS/TZ, not the MSS restart) |
| this repo `0240`/`0241` GPU patches | working SCM PIL calls for PAS 13 (zap), firmware file read from the iOS root | PAS id 13, GPU addresses |
| `MiCode/Xiaomi_Kernel_OpenSource` branch `lavender-q-oss` | the phone's own `sound/soc/codecs/wcd9335.c` is not used, but `msm-analog-codec`-equivalent registers, `msm-dig-codec`, `msm_sdw_codec`, `msm-pcm-q6` and the `tfa98xx` tree are the vendor reference | it is Android 4.4 ASoC; not portable code |
| Linux `drivers/soc/qcom/subsys-pil-tz.c`, `pil-q6v5.c`, `smp2p.c`, `glink_smem_native_xprt.c` | exact PAS command numbers/arginfo, Q6SS offsets, SMP2P item layout, APCS kick semantics | none of it runs on XNU as-is |
| Linux `drivers/clk/qcom/gcc-sdm660.c` | `hlos1_vote_lpass_adsp` GDSC `0x7d034`, SMMU vote `0x7d014`, branch/GDSC bit meanings | LPASS internal clocks are not in the GCC |

## 5. The ladder (0500–0599, one patch at a time)

Each patch is boot-arg gated, off by default, and advances one layer. Numbers are
reserved as we go so another agent cannot collide (this series uses 0500+).

| Patch | What | Boot arg | Risk / gate |
| --- | --- | --- | --- |
| `0500` | read-only platform probe: GCC LPASS GDSC/vote, Q6SS registers, LPASS SMMU, digital codec/AVTIMER/MCLK, SMEM TOC + ADSP partition items (discovers SMP2P/GLINK item IDs), APCS IPC | `lavaud=1` | reading a clock-gated LPASS block can hang the bus: skip the block unless the LPASS GDSC reports on |
| `0501` | LPASS/ADSP power-up: vote `hlos1_vote_lpass_adsp_smmu`, un-collapse the LPASS ADSP GDSC, wait for PWR_ON, re-read Q6SS; no firmware | `lavaud=2` | if the GDSC never comes on, stop (do not touch Q6SS) |
| `0502` | ADSP firmware read: FAT16 `image/adsp.mdt` + `adsp.bNN` from the modem partition, MDT hash-table parse, size/range check against `0x92a00000..0x94800000`; no TZ call yet | `lavaud=3` | read-only; validates the loader before any write |
| `0503` | TrustZone PAS load: copy segments, `INIT_IMAGE`, `MEM_SETUP`, `AUTH_AND_RESET` (PAS 1), wait for ADSP PBL/err-ready | `lavaud=4` | never if SMEM shows an earlier run; `adsp_stop=1` shuts down first |
| `0504` | SMP2P out item + `slave-kernel` READY, GLINK `lpass` edge, channel bring-up (discovered item IDs) | `lavaud=5` | only after 0503 says the DSP is up |
| `0505` | APR `apr_audio_svc`: enumerate AFE/ASM/ADM services; no audio yet | `lavaud=6` | polling only, no interrupts |
| `0506` | PM660L codec bring-up over SPMI: ID/revision, supplies, codec reset (LPI GPIO 24), MCLK 9.6 MHz, MBHC/HPH/SPK blocks; read back | `lavaud=7` | codec writes after supplies are verified; keep `cdc-vdd-pa`/BOB state |
| `0507` | LPASS digital codec + SWR master + AFE `INT4_MI2S_RX` port: bit clock/frame sync, SWR frame shape, port started | `lavaud=8` | must not touch LPASS registers while the DSP owns the block; check 0505 first |
| `0508` | ASM stream + ADM COPP to `INT4_MI2S_RX`; 1 kHz stereo tone, speakers muted/enabled per mixer sequence | `lavaud=9` | audible; keep volume at minimum, monitor codec OCP |
| `0509` | `/dev/lavender-audio` sink and the topaz-derived HAL plug-in (Speaker/Hawking/Codec); mediaserverd frames | `lavaud=10` | userland install on the root image; no Apple binaries |
| ``0510``+` | microphone (INT3_MI2S_TX/ADC), jack detection/MBHC, volume, suspend | | after the speaker path works |

`0500` is deliberately the same shape as `0236` (GPU step 1) and `0700` (WLAN F0b):
it only reads, announces every access 2 s ahead, and logs enough to decide the next
step without another blind boot.

## 6. Open questions that 0500 answers

1. **Is the LPASS ADSP power domain on after ABL?** (`0x7d034` PWR_ON/SW_COLLAPSE)
   and is the SMMU vote on (`0x7d014`)?
2. **Is the ADSP already running?** The SMEM ADSP partition (host0 0, host1 2) will
   hold the ADSP's `$SMP` item and GLINK fifos if it ran; this decides whether
   `AUTH_AND_RESET` can be called without a shutdown first.
3. **Which SMEM item IDs does this firmware use for SMP2P out/in and GLINK
   descriptor/RX/TX?** The topaz ids (429/443, 478/479/480) are SM6225's; the TOC
   and item walk gives the real ones. The item walk prints `$SMP` headers and sizes.
4. **Is the LPASS Q6SS already out of reset / does the GDSC stay on?**
5. **What is the LPASS SMMU state?** (global sCR0, SMR/S2CR mapping) - needed before
   any ASM buffer is handed to the DSP.
6. **Does the phone's APCS IPC register show stale ADSP bits?** (GLINK `0x200`,
   SMP2P `0x400`)

## 7. Safety

- Read-only first, always. A wrong read of a powered-down LPASS block on this SoC
  can take the bus down without a panic; every access is announced and spaced.
- `0x92a00000` is the ADSP carve-out and `0x94800000..0x94a00000` is the MBA
  region; the modem series already proved the MSS carve-outs are usable from XNU
  (MBA alive). Do not write until the firmware loader's range check passes.
- Do not boot the ADSP over SMEM state from an earlier run: shut down (PAS 4) or
  refuse, exactly as the Windows topaz driver learned.
- The PM660L codec shares the PMIC with the keys/RTC; codec writes go through the
  existing SPMI arbiter path and only after the supplies (`BOB`, `cdc-vdd-pa`) are
  read as on.
- Tests: `tests/run_tests.sh` (patch structure + mirrors) before every commit; the
  kernel build is a CI run, then a shim and a device boot with `sweetusb=2` so the
  live log reaches `192.168.7.1:5140`.
