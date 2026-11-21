/*
 * lavender: audio platform probe, A0/A1/A2. Boot-arg gated.
 *
 *   lavaud=1        run the read-only probe (A0)
 *   lavaud=2        also power up the LPASS ADSP domain if the bootloader
 *                   left it off, then read the Q6SS (A1)
 *   lavaud=3        also read adsp.mdt from the modem partition's FAT16,
 *                   parse its ELF/hash table and check every segment blob
 *                   against the ADSP carve-out (A2, read only)
 *   lavauddelay=S   seconds to wait after boot before it (default 25), so the
 *                   console ring flusher and the USB live log are up
 *
 * The phone does NOT have a WCD9335: the live DTBO disables the SLIMbus audio
 * controller ("slim_aud") and the enabled sound card is qcom,sdm660-asoc-snd
 * with the PM660L analog codec (SPMI 0xf000) plus the LPASS digital codec
 * (0x152c0000) and its SoundWire master (0x152c1000). The DSP owns the data
 * path; before writing any of it this probe answers the questions the ladder
 * needs (work/briefs/audio-sdm660-brief.md):
 *
 *   - did the bootloader leave the LPASS ADSP power domain and the SMMU vote
 *     on (GCC 0x7d034 / 0x7d014)?
 *   - if the domain is on: what does the ADSP Q6SS (LPASS 0x15700000) say?
 *     reset, clock mux, power and BHS state (the DSP may already be running)
 *   - what does the LPASS Q6 SMMU (0x05100000) have mapped?
 *   - what do the LPASS digital codec, AVTIMER, MCLK and LPI pinctrl show?
 *   - is the SMEM TOC intact and does the apps<->ADSP partition still hold an
 *     item from an earlier run? That also discovers this firmware's SMP2P and
 *     GLINK item ids, which are not SM6225's 429/443/478-480.
 *   - what does the APCS IPC register (0x17911008) show for the ADSP bits?
 *
 * It READS ONLY at lavaud=1 and lavaud=3. A clock-gated LPASS block is not
 * touched: if the ADSP domain is off, the Q6SS, codec and SMMU sections are
 * skipped instead of risking a bus hang. lavaud=2 clears the LPASS ADSP GDSC's
 * SW_COLLAPSE (after voting the LPASS SMMU clock) and re-reads the Q6SS; if the
 * domain does not come on it is put back and the rest is skipped. lavaud=3
 * adds the ADSP firmware dry run from the modem FAT16: the MDT is parsed and
 * every segment is checked against the carve-out, but nothing is written and
 * TrustZone is not called. Every access is announced 2 s in advance so the log
 * leaves the machine even if the access kills it.
 *
 * Register meanings: Linux gcc-sdm660.c, subsys-pil-tz.c and pil-q6v5.c, the
 * SDM660 DTB and the SMEM partition layout already used by the topaz port,
 * read as behaviour references (values only, no code copied).
 */
#if defined(ARM64_BOARD_CONFIG_LAVENDER)

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>
#include <vm/memory_types.h>
#include <vm/vm_kern.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
/* osfmk/arm/machine_routines.h, not exported to the BSD layer: 0177 already
 * uses it for the framebuffer, because io_submap is only 8 MiB. */
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, uint32_t);
extern int santoni_emmc_read(uint64_t lba, uint32_t count, void *buf);

#define GCC_PA              0x00100000u
#define GCC_SIZE            0x94000u
#define Q6SS_PA             0x15700000u
#define Q6SS_SIZE           0x00001000u
#define LPASS_SMMU_PA       0x05100000u
#define LPASS_SMMU_SIZE     0x00040000u
#define LPASS_CODEC_PA      0x152c0000u
#define LPASS_CODEC_SIZE    0x00002000u
#define LPASS_PINCTRL_PA    0x15070000u
#define LPASS_PINCTRL_SIZE  0x00001000u
#define LPASS_MCLK_PA       0x15020000u
#define LPASS_MCLK_SIZE     0x00001000u
#define LPASS_AVTIMER_PA    0x150f7000u
#define LPASS_AVTIMER_SIZE  0x00001000u
#define SMEM_PA             0x86000000u
#define SMEM_SIZE           0x00200000u
#define APCS_IPC_PA         0x17911000u
#define APCS_IPC_SIZE       0x00001000u

/* The phone's modem partition is FAT16 at GPT LBA 786432 (same layout the
 * modem driver validates: 512 B sectors, 32 sectors/cluster, 1 reserved,
 * 2 FATs, 512 root entries, 49 FAT sectors). */
#define MODEM_LBA           786432ull
#define FAT_SECTOR          512u
#define FAT_SPC             32u
#define FAT_ROOT_ENTRIES    512u
#define FAT_SIZE_SECTORS    49u
#define FAT_CLUSTER_BYTES   (FAT_SPC * FAT_SECTOR)
#define FAT_ROOT_LBA        (MODEM_LBA + 1 + 2 * FAT_SIZE_SECTORS)
#define FAT_DATA_LBA        (FAT_ROOT_LBA + \
    (FAT_ROOT_ENTRIES * 32 / FAT_SECTOR))
#define FAT_MAX_CLUSTER     0xfff8u
#define ADSP_CARVE_PA       0x92a00000u
#define ADSP_CARVE_SIZE     0x01e00000u
#define ADSP_MDT_MAX        (256u << 10)

struct elf32_phdr {
	uint32_t p_type, p_offset, p_vaddr, p_paddr;
	uint32_t p_filesz, p_memsz, p_flags, p_align;
} __attribute__((packed));

/* GCC byte offsets from GCC_PA (gcc-sdm660.c) */
#define GCC_LPASS_SMMU_CBCR     0x7d014u  /* hlos1_vote_lpass_adsp_smmu_clk */
#define GCC_LPASS_ADSP_GDSCR    0x7d034u  /* hlos1_vote_lpass_adsp_gdsc */
#define CBCR_EN                 (1u << 0)
#define CBCR_OFF                (1u << 31)
#define GDSC_PWR_ON             (1u << 31)
#define GDSC_SW_COLLAPSE        (1u << 0)

/* QDSP6SS byte offsets from Q6SS_PA (pil-q6v5.c) */
#define Q6SS_RESET              0x014u
#define Q6SS_GFMUX_CTL          0x020u
#define Q6SS_PWR_CTL            0x030u
#define Q6SS_MEM_PWR_CTL        0x034u
#define Q6SS_XO_CBCR            0x038u
#define Q6SS_SLEEP              0x03cu
#define Q6SS_BHS_STATUS         0x078u
#define Q6SS_MEM_PWR_CTL2       0x0b0u
#define Q6V62_BHS_STATUS        0x0c4u

/* SMMUv2 GR0 offsets (arm-smmu.c) */
#define SMMU_SCR0               0x0000u
#define SMMU_SACR               0x0010u
#define SMMU_IDR0               0x0020u
#define SMMU_IDR1               0x0024u
#define SMMU_IDR7               0x003cu
#define SMMU_SGFAR              0x0040u
#define SMMU_SGFSR              0x0048u
#define SMMU_SMR(n)             (0x0800u + ((n) << 2))
#define SMMU_S2CR(n)            (0x0c00u + ((n) << 2))
#define SMMU_SMR_VALID          (1u << 31)
#define SMMU_MAX_SMRS           128

/* SMEM v12 layout, as already used by the topaz port (TopazModem.cpp). */
#define SMEM_TOC_OFF            (SMEM_SIZE - 0x1000u)
#define SMEM_TOC_ENTRIES        64u
#define SMEM_ADSP_PID           2u
#define SMEM_APPS_HOST          0u
#define SMEM_GLOBAL_HOST        0xfffeu
#define SMEM_ITEM_CANARY        0xa5a5u
#define SMP2P_MAGIC             0x504d5324u     /* "$SMP" */
#define SMP2P_MAX_ENTRY         16u

struct smem_toc_entry {
	uint32_t offset, size, flags;
	uint16_t host0, host1;
	uint32_t cacheline, rsvd[7];
} __attribute__((packed));

struct smem_toc {
	uint8_t  magic[4];
	uint32_t version, num_entries, rsvd[5];
} __attribute__((packed));

struct smem_part {
	uint8_t  magic[4];
	uint16_t host0, host1;
	uint32_t size, free_uncached, free_cached, rsvd[3];
} __attribute__((packed));

struct smem_item {
	uint16_t canary, item;
	uint32_t size;
	uint16_t pad_data, pad_hdr;
	uint32_t rsvd;
} __attribute__((packed));

struct smp2p_hdr {
	uint32_t magic;
	uint8_t  version, features[3];
	uint16_t local_pid, remote_pid, total, valid;
	uint32_t flags;
} __attribute__((packed));

struct smp2p_entry {
	char     name[16];
	uint32_t value;
} __attribute__((packed));

static vm_offset_t gcc, q6ss, smmu, codec, pinctrl, mclk, avtimer, smem, apcs;
static int lpass_on;
static uint32_t audio_step;
static int audio_chan;
static uint8_t *fat_fat, *fat_root, *fat_image, *fat_bounce, *adsp_mdt;
static uint32_t fat_image_len;
static int fat_ready;

static inline uint32_t
audio_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);

	__asm__ volatile("dsb ld" ::: "memory");
	return v;
}

static void
audio_wr(vm_offset_t base, uint32_t off, uint32_t val)
{
	*(volatile uint32_t *)(base + off) = val;
	__asm__ volatile("dsb sy" ::: "memory");
}

static void
audio_wait(unsigned ms)
{
	uint64_t deadline;

	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&audio_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

/* Announce, then give the log 2 s to leave the machine. */
static void
audio_announce(const char *what)
{
	printf("SWEET AUDIO: about to %s\n", what);
	audio_wait(2000);
}

static void
audio_gcc(void)
{
	uint32_t cbcr, gdscr;

	audio_announce("read the GCC LPASS ADSP vote and power domain (read only)");
	cbcr = audio_rd(gcc, GCC_LPASS_SMMU_CBCR);
	gdscr = audio_rd(gcc, GCC_LPASS_ADSP_GDSCR);
	lpass_on = ((gdscr & GDSC_PWR_ON) != 0) && ((gdscr & GDSC_SW_COLLAPSE) == 0);
	printf("SWEET AUDIO: gcc lpass_smmu_cbcr %08x (en %u, %s)\n", cbcr,
	    cbcr & CBCR_EN, (cbcr & CBCR_OFF) ? "OFF" : "on");
	printf("SWEET AUDIO: gcc lpass_adsp_gdscr %08x (pwr_on %u, collapse %u)\n",
	    gdscr, (gdscr & GDSC_PWR_ON) != 0, (gdscr & GDSC_SW_COLLAPSE) != 0);
	printf("SWEET AUDIO: LPASS ADSP domain is %s\n", lpass_on ? "ON" : "OFF");
}

static int
audio_poll(vm_offset_t base, uint32_t off, uint32_t mask, uint32_t want,
    unsigned ms)
{
	for (unsigned i = 0; i < ms; i++) {
		if ((audio_rd(base, off) & mask) == want) {
			return 0;
		}
		audio_wait(1);
	}
	return ETIMEDOUT;
}

/*
 * A1: the LPASS ADSP power domain. The bootloader leaves it collapsed on some
 * boots; the GCC GDSC is the only LPASS power control the AP owns (the LPASS
 * internal clocks belong to the DSP). The SMMU vote clock is taken first
 * because the LPASS register window is not usable without it. If the GDSC
 * never reports PWR_ON, the collapse bit is put back and the caller skips
 * every read of the block.
 */
static int
audio_power_up(void)
{
	uint32_t v, gdscr;

	if (lpass_on) {
		printf("SWEET AUDIO: LPASS ADSP already on; nothing to write\n");
		return 0;
	}
	audio_announce("power up the LPASS ADSP domain (lavaud>=2)");

	v = audio_rd(gcc, GCC_LPASS_SMMU_CBCR);
	printf("SWEET AUDIO: smmu cbcr before %08x\n", v);
	if (v & CBCR_OFF) {
		audio_wr(gcc, GCC_LPASS_SMMU_CBCR, v | CBCR_EN);
		if (audio_poll(gcc, GCC_LPASS_SMMU_CBCR, CBCR_OFF, 0, 100)) {
			v = audio_rd(gcc, GCC_LPASS_SMMU_CBCR);
			printf("SWEET AUDIO: smmu cbcr still off %08x\n", v);
			return EIO;
		}
	}
	printf("SWEET AUDIO: smmu cbcr after %08x (on)\n",
	    audio_rd(gcc, GCC_LPASS_SMMU_CBCR));

	gdscr = audio_rd(gcc, GCC_LPASS_ADSP_GDSCR);
	printf("SWEET AUDIO: adsp gdscr before %08x\n", gdscr);
	if (!(gdscr & GDSC_PWR_ON) || (gdscr & GDSC_SW_COLLAPSE)) {
		audio_wr(gcc, GCC_LPASS_ADSP_GDSCR, gdscr & ~GDSC_SW_COLLAPSE);
		if (audio_poll(gcc, GCC_LPASS_ADSP_GDSCR, GDSC_PWR_ON,
		    GDSC_PWR_ON, 200)) {
			gdscr = audio_rd(gcc, GCC_LPASS_ADSP_GDSCR);
			printf("SWEET AUDIO: adsp gdscr after %08x (STILL OFF), "
			    "putting the collapse back\n", gdscr);
			audio_wr(gcc, GCC_LPASS_ADSP_GDSCR,
			    gdscr | GDSC_SW_COLLAPSE);
			return EIO;
		}
	}
	gdscr = audio_rd(gcc, GCC_LPASS_ADSP_GDSCR);
	lpass_on = ((gdscr & GDSC_PWR_ON) != 0) &&
	    ((gdscr & GDSC_SW_COLLAPSE) == 0);
	printf("SWEET AUDIO: adsp gdscr after %08x (%s)\n", gdscr,
	    lpass_on ? "on" : "still off");
	return lpass_on ? 0 : EIO;
}

/* --- A2: adsp.mdt and the segment blobs in the modem FAT16 ---------------- */

static inline uint16_t
audio_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t
audio_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* The eMMC is shared with the root and the console flusher; retry like the
 * modem driver does instead of hammering the controller. */
static int
audio_emmc_read(uint64_t lba, uint32_t count, void *buf)
{
	int err = EIO;

	for (int attempt = 0; attempt < 5; attempt++) {
		err = santoni_emmc_read(lba, count, buf);
		if (err == 0) {
			return 0;
		}
		printf("SWEET AUDIO: eMMC read 0x%llx+%u failed %d (attempt "
		    "%d/5)\n", (unsigned long long)lba, count, err,
		    attempt + 1);
		audio_wait(100);
	}
	return err;
}

/* Build the 8.3 name of a FAT directory entry, without the trailing dot. */
static void
audio_fat_name(const uint8_t *e, char *out, size_t outlen)
{
	size_t n = 0;

	for (int i = 0; i < 8 && e[i] != ' '; i++) {
		if (n + 1 < outlen) {
			out[n++] = (char)e[i];
		}
	}
	if (e[8] != ' ' && n + 1 < outlen) {
		out[n++] = '.';
		for (int i = 8; i < 11 && e[i] != ' '; i++) {
			if (n + 1 < outlen) {
				out[n++] = (char)e[i];
			}
		}
	}
	out[n] = '\0';
}

static uint32_t
audio_fat_next(uint32_t cluster)
{
	if (cluster * 2 + 2 > 32u * 1024u) {
		return FAT_MAX_CLUSTER;
	}
	return audio_le16(fat_fat + cluster * 2);
}

static int
audio_fat_open(void)
{
	const uint8_t *b;
	uint32_t fat_bytes, cluster, off;
	int err;

	if (fat_ready) {
		return 0;
	}
	if (fat_fat == NULL) {
		if (kmem_alloc(kernel_map, (vm_offset_t *)&fat_fat,
		    32u * 1024u, KMA_DATA | KMA_ZERO,
		    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS ||
		    kmem_alloc(kernel_map, (vm_offset_t *)&fat_root,
		    FAT_CLUSTER_BYTES, KMA_DATA | KMA_ZERO,
		    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS ||
		    kmem_alloc(kernel_map, (vm_offset_t *)&fat_image,
		    FAT_CLUSTER_BYTES, KMA_DATA | KMA_ZERO,
		    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS ||
		    kmem_alloc(kernel_map, (vm_offset_t *)&fat_bounce,
		    FAT_CLUSTER_BYTES, KMA_DATA | KMA_ZERO,
		    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
			printf("SWEET AUDIO: no memory for the FAT cache\n");
			return ENOMEM;
		}
	}
	audio_announce("read the modem FAT16 and its IMAGE/ directory");
	err = audio_emmc_read(MODEM_LBA, 1, fat_bounce);
	if (err) {
		return err;
	}
	b = fat_bounce;
	if (audio_le16(b + 0x0b) != FAT_SECTOR || b[0x0d] != FAT_SPC ||
	    audio_le16(b + 0x0e) != 1 || b[0x10] != 2 ||
	    audio_le16(b + 0x11) != FAT_ROOT_ENTRIES ||
	    audio_le16(b + 0x16) != FAT_SIZE_SECTORS) {
		printf("SWEET AUDIO: unexpected modem BPB (%u/%u/%u/%u/%u/%u)\n",
		    audio_le16(b + 0x0b), b[0x0d], audio_le16(b + 0x0e),
		    b[0x10], audio_le16(b + 0x11), audio_le16(b + 0x16));
		return EINVAL;
	}

	fat_bytes = FAT_SIZE_SECTORS * FAT_SECTOR;
	if (fat_bytes > 32u * 1024u) {
		return EINVAL;
	}
	err = audio_emmc_read(MODEM_LBA + 1, FAT_SIZE_SECTORS, fat_fat);
	if (err) {
		return err;
	}
	err = audio_emmc_read(FAT_ROOT_LBA, FAT_ROOT_ENTRIES * 32 / FAT_SECTOR,
	    fat_root);
	if (err) {
		return err;
	}

	cluster = 0;
	for (uint32_t i = 0; i < FAT_ROOT_ENTRIES; i++) {
		const uint8_t *e = fat_root + i * 32;
		char name[13];

		if (e[0] == 0x00) {
			break;
		}
		if (e[0] == 0xe5 || e[0x0b] == 0x0f) {
			continue;
		}
		audio_fat_name(e, name, sizeof(name));
		if (strcasecmp(name, "IMAGE") == 0 && (e[0x0b] & 0x10)) {
			cluster = audio_le16(e + 0x1a) |
			    (audio_le16(e + 0x14) << 16);
			break;
		}
	}
	if (cluster < 2 || cluster >= FAT_MAX_CLUSTER) {
		printf("SWEET AUDIO: IMAGE/ not found in the modem FAT\n");
		return ENOENT;
	}
	off = FAT_DATA_LBA + (cluster - 2) * FAT_SPC;
	err = audio_emmc_read(off, FAT_SPC, fat_image);
	if (err) {
		return err;
	}
	fat_image_len = FAT_CLUSTER_BYTES;
	fat_ready = 1;
	printf("SWEET AUDIO: modem FAT16 open, IMAGE/ cluster %u, %u bytes "
	    "cached\n", cluster, fat_image_len);
	return 0;
}

static int
audio_fat_lookup(const char *name, uint32_t *cluster, uint32_t *size)
{
	if (!fat_ready) {
		return ENOENT;
	}
	for (uint32_t i = 0; i < fat_image_len / 32; i++) {
		const uint8_t *e = fat_image + i * 32;
		char entry[13];

		if (e[0] == 0x00) {
			break;
		}
		if (e[0] == 0xe5 || e[0x0b] == 0x0f || (e[0x0b] & 0x10)) {
			continue;
		}
		audio_fat_name(e, entry, sizeof(entry));
		if (strcasecmp(entry, name) == 0) {
			*cluster = audio_le16(e + 0x1a) |
			    (audio_le16(e + 0x14) << 16);
			*size = audio_le32(e + 0x1c);
			return 0;
		}
	}
	return ENOENT;
}

static int
audio_fat_read(const char *name, void *dst, uint32_t max, uint32_t *out)
{
	uint32_t cluster, size, off = 0;
	int err;

	err = audio_fat_lookup(name, &cluster, &size);
	if (err) {
		printf("SWEET AUDIO: %s not found in IMAGE/\n", name);
		return err;
	}
	if (size > max) {
		printf("SWEET AUDIO: %s is %u bytes, max %u\n", name, size, max);
		return EINVAL;
	}
	while (off < size) {
		uint32_t n = MIN(size - off, FAT_CLUSTER_BYTES);

		err = audio_emmc_read(FAT_DATA_LBA + (cluster - 2) * FAT_SPC,
		    FAT_SPC, fat_bounce);
		if (err) {
			printf("SWEET AUDIO: %s read failed %d at %u\n", name,
			    err, off);
			return err;
		}
		memcpy((uint8_t *)dst + off, fat_bounce, n);
		off += n;
		if (off < size) {
			cluster = audio_fat_next(cluster);
			if (cluster < 2 || cluster >= FAT_MAX_CLUSTER) {
				printf("SWEET AUDIO: %s chain ends at %u/%u\n",
				    name, off, size);
				return EIO;
			}
		}
	}
	if (out != NULL) {
		*out = size;
	}
	return 0;
}

/* Read only the first n bytes of a blob: enough to prove the chain reads. */
static int
audio_fat_peek(const char *name, void *dst, uint32_t n, uint32_t *size)
{
	uint32_t cluster, fsize, off;
	int err;

	err = audio_fat_lookup(name, &cluster, &fsize);
	if (err) {
		printf("SWEET AUDIO: %s not found in IMAGE/\n", name);
		return err;
	}
	if (size != NULL) {
		*size = fsize;
	}
	if (fsize < n || n > FAT_CLUSTER_BYTES) {
		return EINVAL;
	}
	off = FAT_DATA_LBA + (cluster - 2) * FAT_SPC;
	err = audio_emmc_read(off, FAT_SPC, fat_bounce);
	if (err || !fat_ready) {
		return err ? err : EIO;
	}
	memcpy(dst, fat_bounce, n);
	return 0;
}

static int
audio_check_segment(const char *name, uint32_t paddr, uint32_t filesz,
    uint32_t memsz, unsigned *bad)
{
	uint8_t peek[8] = { 0 };
	uint32_t fat_size = 0;
	int err;

	if ((uint64_t)paddr < ADSP_CARVE_PA ||
	    (uint64_t)paddr + memsz > ADSP_CARVE_PA + ADSP_CARVE_SIZE ||
	    memsz < filesz) {
		printf("SWEET AUDIO: %s outside the carve-out: %08x+%08x "
		    "(file %u)\n", name, paddr, memsz, filesz);
		(*bad)++;
		return EINVAL;
	}
	err = audio_fat_peek(name, peek, sizeof(peek), &fat_size);
	if (err) {
		printf("SWEET AUDIO: %s: no readable blob (%d)\n", name, err);
		(*bad)++;
		return err;
	}
	printf("SWEET AUDIO: %-11s %08x memsz %06x file %06x fat %06x "
	    "first %08x\n", name, paddr, memsz, filesz, fat_size,
	    audio_le32(peek));
	if (fat_size != filesz) {
		printf("SWEET AUDIO: %s FAT size %u != filesz %u\n", name,
		    fat_size, filesz);
		(*bad)++;
		return EINVAL;
	}
	return 0;
}

/*
 * A2: read the ADSP metadata from the modem partition and validate it against
 * the reserved region. Nothing is written to the carve-out and TrustZone is
 * not called; this is the loader's dry run. The hash segment names the blobs
 * (adsp.bNN); if it is missing or malformed, the phdr index naming is used.
 */
static void
audio_firmware(void)
{
	const uint8_t *mdt;
	uint32_t mdt_len = 0, phoff, phnum, phent, i;
	uint32_t hash_off = 0, hash_len = 0, entries = 0, bad = 0, found = 0;
	uint64_t total = 0;
	int err;

	if (audio_fat_open() != 0) {
		return;
	}
	if (adsp_mdt == NULL &&
	    kmem_alloc(kernel_map, (vm_offset_t *)&adsp_mdt, ADSP_MDT_MAX,
	    KMA_DATA | KMA_ZERO, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("SWEET AUDIO: no memory for the ADSP MDT\n");
		return;
	}
	audio_announce("read ADSP.MDT from the modem FAT16");
	err = audio_fat_read("ADSP.MDT", adsp_mdt, ADSP_MDT_MAX, &mdt_len);
	if (err) {
		return;
	}
	mdt = adsp_mdt;
	if (mdt_len < 0x34 || memcmp(mdt, "\177ELF", 4) != 0) {
		printf("SWEET AUDIO: ADSP.MDT is not the expected ELF\n");
		return;
	}
	phoff = audio_le32(mdt + 28);
	phent = audio_le16(mdt + 42);
	phnum = audio_le16(mdt + 44);
	printf("SWEET AUDIO: ADSP.MDT %u bytes, entry %08x phoff %u phnum %u "
	    "phent %u\n", mdt_len, audio_le32(mdt + 24), phoff, phnum, phent);
	if (phent != 32 || phoff > mdt_len || phnum == 0 ||
	    phoff + (uint64_t)phnum * 32 > mdt_len) {
		printf("SWEET AUDIO: ADSP.MDT program headers are invalid\n");
		return;
	}
	for (i = 0; i < phnum; i++) {
		const uint8_t *ph = mdt + phoff + i * 32;

		if (audio_le32(ph) == 1 &&
		    ((audio_le32(ph + 28) >> 24) & 7u) == 2u) {
			hash_off = audio_le32(ph + 4);
			hash_len = audio_le32(ph + 16);
			break;
		}
	}
	if (hash_len >= 8 && hash_off + (uint64_t)hash_len <= mdt_len &&
	    audio_le32(mdt + hash_off) == 1) {
		entries = audio_le32(mdt + hash_off + 4);
	}
	printf("SWEET AUDIO: ADSP.MDT hash at %u+%u, %u entries\n", hash_off,
	    hash_len, entries);
	if (entries > 64) {
		entries = 0;
	}
	if (entries != 0) {
		for (i = 0; i < entries; i++) {
			const uint8_t *e = mdt + hash_off + 8 + i * 40;
			const uint8_t *ph;
			char name[33];
			uint32_t off, len, paddr, filesz, memsz;

			if ((uint64_t)(i + 1) * 40 + 8 > hash_len) {
				printf("SWEET AUDIO: hash entry %u is truncated\n",
				    i);
				bad++;
				break;
			}
			len = audio_le32(e);
			off = audio_le32(e + 4);
			memcpy(name, e + 8, 32);
			name[32] = '\0';
			if (off + 32 > mdt_len) {
				printf("SWEET AUDIO: %s phdr offset %u is "
				    "outside the MDT\n", name, off);
				bad++;
				continue;
			}
			ph = mdt + off;
			paddr = audio_le32(ph + 12);
			filesz = audio_le32(ph + 16);
			memsz = audio_le32(ph + 20);
			if (len != filesz) {
				printf("SWEET AUDIO: %s hash len %u != "
				    "filesz %u\n", name, len, filesz);
				bad++;
				continue;
			}
			found++;
			total += memsz;
			(void)audio_check_segment(name, paddr, filesz, memsz,
			    &bad);
		}
	} else {
		printf("SWEET AUDIO: no usable hash table; using phdr index "
		    "names\n");
		for (i = 0; i < phnum; i++) {
			const uint8_t *ph = mdt + phoff + i * 32;
			char name[16];
			uint32_t paddr, filesz, memsz;

			if (audio_le32(ph) != 1 ||
			    ((audio_le32(ph + 28) >> 24) & 7u) == 2u ||
			    audio_le32(ph + 16) == 0) {
				continue;
			}
			paddr = audio_le32(ph + 12);
			filesz = audio_le32(ph + 16);
			memsz = audio_le32(ph + 20);
			snprintf(name, sizeof(name), "ADSP.B%02u", i);
			found++;
			total += memsz;
			(void)audio_check_segment(name, paddr, filesz, memsz,
			    &bad);
		}
	}
	printf("SWEET AUDIO: ADSP firmware dry run: %u segments, %llu bytes, "
	    "%u bad\n", found, (unsigned long long)total, bad);
	if (bad != 0) {
		printf("SWEET AUDIO: NOT loading ADSP (validation failed)\n");
	}
}

static void
audio_q6ss(void)
{
	if (!lpass_on) {
		printf("SWEET AUDIO: LPASS off; not touching the Q6SS at %08x\n",
		    Q6SS_PA);
		return;
	}
	audio_announce("read the ADSP Q6SS registers (domain is on)");
	printf("SWEET AUDIO: q6 reset %08x gfmux %08x pwr %08x mem_pwr %08x\n",
	    audio_rd(q6ss, Q6SS_RESET), audio_rd(q6ss, Q6SS_GFMUX_CTL),
	    audio_rd(q6ss, Q6SS_PWR_CTL), audio_rd(q6ss, Q6SS_MEM_PWR_CTL));
	printf("SWEET AUDIO: q6 xo_cbcr %08x sleep %08x bhs %08x bhs62 %08x "
	    "mem_pwr2 %08x\n", audio_rd(q6ss, Q6SS_XO_CBCR),
	    audio_rd(q6ss, Q6SS_SLEEP), audio_rd(q6ss, Q6SS_BHS_STATUS),
	    audio_rd(q6ss, Q6V62_BHS_STATUS),
	    audio_rd(q6ss, Q6SS_MEM_PWR_CTL2));
}

static const char *
audio_smmu_type(uint32_t s2cr)
{
	switch ((s2cr >> 16) & 3u) {
	case 0:
		return "translate";
	case 1:
		return "bypass";
	case 2:
		return "fault";
	default:
		return "reserved";
	}
}

static void
audio_lpass_smmu(void)
{
	uint32_t cbcr = audio_rd(gcc, GCC_LPASS_SMMU_CBCR);

	if (cbcr & CBCR_OFF) {
		printf("SWEET AUDIO: LPASS SMMU vote clock is off; not reading "
		    "0x%08x\n", LPASS_SMMU_PA);
		return;
	}
	audio_announce("read the LPASS Q6 SMMU global registers");
	printf("SWEET AUDIO: lpass smmu sCR0 %08x sACR %08x IDR0 %08x IDR1 %08x "
	    "IDR7 %08x\n", audio_rd(smmu, SMMU_SCR0), audio_rd(smmu, SMMU_SACR),
	    audio_rd(smmu, SMMU_IDR0), audio_rd(smmu, SMMU_IDR1),
	    audio_rd(smmu, SMMU_IDR7));
	printf("SWEET AUDIO: lpass smmu sGFAR %08x sGFSR %08x\n",
	    audio_rd(smmu, SMMU_SGFAR), audio_rd(smmu, SMMU_SGFSR));
	for (uint32_t i = 0; i < 16; i++) {
		uint32_t smr = audio_rd(smmu, SMMU_SMR(i));
		uint32_t s2cr;

		if (!(smr & SMMU_SMR_VALID)) {
			continue;
		}
		s2cr = audio_rd(smmu, SMMU_S2CR(i));
		printf("SWEET AUDIO: lpass SMR[%u] %08x sid %04x -> S2CR %08x "
		    "%s cb %u\n", i, smr, smr & 0x7fffu, s2cr,
		    audio_smmu_type(s2cr), s2cr & 0xffu);
	}
}

static void
audio_lpass_blocks(void)
{
	if (!lpass_on) {
		printf("SWEET AUDIO: LPASS off; skipping codec/pinctrl/MCLK/"
		    "AVTIMER reads\n");
		return;
	}
	audio_announce("read the LPASS digital codec and SoundWire block");
	printf("SWEET AUDIO: codec %08x %08x %08x %08x swr %08x %08x %08x %08x\n",
	    audio_rd(codec, 0x000), audio_rd(codec, 0x004),
	    audio_rd(codec, 0x008), audio_rd(codec, 0x00c),
	    audio_rd(codec, 0x1000), audio_rd(codec, 0x1004),
	    audio_rd(codec, 0x1008), audio_rd(codec, 0x100c));
	audio_announce("read the LPASS LPI pinctrl, MCLK and AVTIMER registers");
	printf("SWEET AUDIO: lpi pinctrl %08x %08x %08x %08x\n",
	    audio_rd(pinctrl, 0x000), audio_rd(pinctrl, 0x004),
	    audio_rd(pinctrl, 0x008), audio_rd(pinctrl, 0x00c));
	printf("SWEET AUDIO: mclk_reg %08x, avtimer %08x %08x\n",
	    audio_rd(mclk, 0x018), audio_rd(avtimer, 0x00c),
	    audio_rd(avtimer, 0x010));
}

static void
audio_smp2p_item(const uint8_t *data, uint32_t len)
{
	const struct smp2p_hdr *h = (const struct smp2p_hdr *)data;

	if (len < sizeof(*h)) {
		return;
	}
	printf("SWEET AUDIO: smp2p version %u local %u remote %u total %u "
	    "valid %u flags %08x\n", (unsigned)h->version,
	    (unsigned)h->local_pid, (unsigned)h->remote_pid,
	    (unsigned)h->total, (unsigned)h->valid, h->flags);
	for (uint32_t i = 0; i < h->valid && i < SMP2P_MAX_ENTRY &&
	    sizeof(*h) + (i + 1) * sizeof(struct smp2p_entry) <= len; i++) {
		const struct smp2p_entry *e =
		    (const struct smp2p_entry *)(data + sizeof(*h) +
		    i * sizeof(*e));
		char name[sizeof(e->name) + 1];

		memcpy(name, e->name, sizeof(e->name));
		name[sizeof(e->name)] = '\0';
		printf("SWEET AUDIO:   entry %u '%s' = %08x\n", i, name,
		    e->value);
	}
}

static void
audio_smem_part(const char *tag, const struct smem_toc_entry *te)
{
	vm_offset_t base = smem + te->offset;
	const struct smem_part *p = (const struct smem_part *)base;
	uint32_t used = 0, items = 0;

	if (te->size < sizeof(*p) || memcmp(p->magic, "$PRT", 4) != 0) {
		printf("SWEET AUDIO: %s %u/%u @%05x size %05x: no $PRT header\n",
		    tag, (unsigned)te->host0, (unsigned)te->host1, te->offset,
		    te->size);
		return;
	}
	printf("SWEET AUDIO: %s %u/%u @%05x size %05x used %05x cached %05x\n",
	    tag, (unsigned)p->host0, (unsigned)p->host1, te->offset, p->size,
	    p->free_uncached, p->free_cached);
	if (p->free_uncached > p->size || p->free_uncached < sizeof(*p)) {
		printf("SWEET AUDIO: %s: bad free pointer %05x\n", tag,
		    p->free_uncached);
		return;
	}
	while (used + sizeof(struct smem_item) <= p->free_uncached) {
		const struct smem_item *h = (const struct smem_item *)
		    (base + sizeof(*p) + used);
		const uint8_t *data;
		uint32_t payload, step, magic;

		if (h->canary != SMEM_ITEM_CANARY) {
			printf("SWEET AUDIO: %s: bad canary %04x at %05x\n", tag,
			    (unsigned)h->canary, used);
			return;
		}
		if (h->pad_data > h->size || h->size == 0) {
			printf("SWEET AUDIO: %s: bad item %u size %u pad %u\n",
			    tag, (unsigned)h->item, h->size,
			    (unsigned)h->pad_data);
			return;
		}
		payload = h->size - h->pad_data;
		step = sizeof(*h) + h->pad_hdr + h->size;
		data = (const uint8_t *)h + sizeof(*h) + h->pad_hdr;
		printf("SWEET AUDIO: %s item %u size %u payload %u\n", tag,
		    (unsigned)h->item, h->size, payload);
		memcpy(&magic, data, sizeof(magic));
		if (payload >= sizeof(struct smp2p_hdr) &&
		    magic == SMP2P_MAGIC) {
			audio_smp2p_item(data, payload);
		}
		items++;
		used += step;
		if (items >= 32) {
			printf("SWEET AUDIO: %s: stopped after 32 items\n", tag);
			return;
		}
	}
}

static void
audio_smem(void)
{
	const struct smem_toc *toc = (const struct smem_toc *)
	    (smem + SMEM_TOC_OFF);
	const struct smem_toc_entry *te = (const struct smem_toc_entry *)
	    (toc + 1);
	uint32_t found = 0;

	audio_announce("read the SMEM header and partition table (read only)");
	printf("SWEET AUDIO: smem header %08x %08x %08x %08x\n",
	    audio_rd(smem, 0x00), audio_rd(smem, 0x04), audio_rd(smem, 0x08),
	    audio_rd(smem, 0x0c));
	if (memcmp(toc->magic, "$TOC", 4) != 0 || toc->num_entries >
	    SMEM_TOC_ENTRIES) {
		printf("SWEET AUDIO: no SMEM $TOC at %08x (magic %c%c%c%c, "
		    "entries %u)\n", SMEM_PA + SMEM_TOC_OFF, toc->magic[0],
		    toc->magic[1], toc->magic[2], toc->magic[3],
		    toc->num_entries);
		return;
	}
	printf("SWEET AUDIO: smem TOC v%u, %u entries\n", toc->version,
	    toc->num_entries);
	for (uint32_t i = 0; i < toc->num_entries; i++) {
		const struct smem_toc_entry *e = &te[i];

		if (e->offset == 0 || e->size == 0 || e->offset >= SMEM_SIZE) {
			continue;
		}
		found++;
		if (e->host0 == SMEM_APPS_HOST && e->host1 == SMEM_ADSP_PID) {
			audio_smem_part("apps<->adsp", e);
		} else {
			printf("SWEET AUDIO: smem part %u/%u @%05x size %05x\n",
			    (unsigned)e->host0, (unsigned)e->host1, e->offset,
			    e->size);
		}
	}
	printf("SWEET AUDIO: smem TOC holds %u partitions\n", found);
}

static void
audio_apcs(void)
{
	audio_announce("read the APCS IPC register for the ADSP bits");
	printf("SWEET AUDIO: apcs ipc %08x (glink bit9 %u, smp2p bit10 %u)\n",
	    audio_rd(apcs, 0x008), (audio_rd(apcs, 0x008) >> 9) & 1u,
	    (audio_rd(apcs, 0x008) >> 10) & 1u);
}

static void
lavender_audio_thread(__unused void *arg, __unused wait_result_t wr)
{
	uint32_t delay_s = 25;

	(void)PE_parse_boot_argn("lavauddelay", &delay_s, sizeof(delay_s));
	if (delay_s > 300) {
		delay_s = 300;
	}
	printf("SWEET AUDIO: A0/A1 probe (lavaud=%u) in %u s\n", audio_step,
	    delay_s);
	audio_wait(delay_s * 1000);

	gcc = ml_io_map(GCC_PA, GCC_SIZE);
	if (gcc == 0) {
		printf("SWEET AUDIO: GCC map failed\n");
		return;
	}
	audio_gcc();

	if (audio_step >= 2) {
		(void)audio_power_up();
	}

	q6ss = ml_io_map(Q6SS_PA, Q6SS_SIZE);
	if (q6ss != 0) {
		audio_q6ss();
	} else if (lpass_on) {
		printf("SWEET AUDIO: Q6SS map failed\n");
	}

	smmu = ml_io_map_unmappable(LPASS_SMMU_PA, LPASS_SMMU_SIZE,
	    VM_WIMG_IO);
	if (smmu != 0) {
		audio_lpass_smmu();
	} else {
		printf("SWEET AUDIO: LPASS SMMU map failed\n");
	}

	codec = ml_io_map(LPASS_CODEC_PA, LPASS_CODEC_SIZE);
	pinctrl = ml_io_map(LPASS_PINCTRL_PA, LPASS_PINCTRL_SIZE);
	mclk = ml_io_map(LPASS_MCLK_PA, LPASS_MCLK_SIZE);
	avtimer = ml_io_map(LPASS_AVTIMER_PA, LPASS_AVTIMER_SIZE);
	if (codec != 0 && pinctrl != 0 && mclk != 0 && avtimer != 0) {
		audio_lpass_blocks();
	} else {
		printf("SWEET AUDIO: LPASS block map failed (codec %d pinctrl %d "
		    "mclk %d avtimer %d)\n", codec != 0, pinctrl != 0, mclk != 0,
		    avtimer != 0);
	}

	smem = ml_io_map_unmappable(SMEM_PA, SMEM_SIZE, VM_WIMG_IO);
	if (smem != 0) {
		audio_smem();
	} else {
		printf("SWEET AUDIO: SMEM map failed\n");
	}

	apcs = ml_io_map(APCS_IPC_PA, APCS_IPC_SIZE);
	if (apcs != 0) {
		audio_apcs();
	} else {
		printf("SWEET AUDIO: APCS map failed\n");
	}

	if (audio_step >= 3) {
		audio_firmware();
	}

	printf("SWEET AUDIO: probe done (lavaud=%u)\n", audio_step);
}

int
lavender_audio_probe(void)
{
	static int started;
	uint32_t step = 0;
	thread_t thread;

	if (started ||
	    !PE_parse_boot_argn("lavaud", &step, sizeof(step)) || step == 0) {
		return 0;
	}
	started = 1;
	audio_step = step;
	if (kernel_thread_start(lavender_audio_thread, NULL, &thread) ==
	    KERN_SUCCESS) {
		thread_deallocate(thread);
	}
	return 0;
}

#else /* !ARM64_BOARD_CONFIG_LAVENDER */

int lavender_audio_probe(void);
int
lavender_audio_probe(void)
{
	return 0;
}

#endif
