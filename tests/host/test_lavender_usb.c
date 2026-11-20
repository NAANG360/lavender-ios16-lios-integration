/*
 * Host test for bsd/dev/sweet_usb.c on LAVENDER (patch 0200): the real
 * driver source against fake SDM660 GCC, DWC3, QSCRATCH and QUSB2 v1
 * register files at the addresses of the phone's DTB.
 *
 * SCENARIO 1 (sweetusb=1, ABL left everything on): read-only. No GCC,
 *   DWC3, QSCRATCH or PHY register may change; the SM7150 addresses must
 *   never be mapped.
 * SCENARIO 2 (sweetusb=3, recovery boot left USB off and the core in OTG
 *   mode): GDSC and the seven branch clocks are enabled, the core is put in
 *   device mode, the VBUS session is forced in QSCRATCH, the device
 *   controller is soft-reset, given our event buffer, run, and the event
 *   and live-log threads are started.
 * SCENARIO 3 (sweetusb=1, ABL left USB off): nothing beyond GCC status is
 *   read and nothing is written.
 */
#include "mock_mmio.h"

void mock_barrier(void);
#define MOCK_BARRIER(...) mock_barrier()

#define T_GCC_PA     0x00100000u
#define T_DWC3_PA    0x0a800000u
#define T_QS_PA      0x0a8f8800u
#define T_QUSB2_PA   0x0c012000u

static const uint32_t cbcr_offs[7] = { 0xf008, 0xf00c, 0xf010, 0x5018, 0xf03c, 0x6a004, 0x88018 };

#define GDSCR      (*mock_reg(T_GCC_PA + 0xf004))
#define DWC(off)   (*mock_reg(T_DWC3_PA + (off)))

/* Fake hardware reaction points */
static void
hw_step(void)
{
	if (!(GDSCR & 1u)) {
		GDSCR |= 1u << 31;                      /* collapse released -> power on */
	}
	for (unsigned i = 0; i < 7; i++) {
		uint32_t *c = mock_reg(T_GCC_PA + cbcr_offs[i]);
		if (*c & 1u) {
			*c &= ~(1u << 31);              /* enabled -> running */
		}
	}
	if (DWC(0xc704) & (1u << 30)) {
		DWC(0xc704) &= ~(1u << 30);             /* soft reset completes */
	}
	if (DWC(0xc704) & (1u << 31)) {
		DWC(0xc70c) &= ~(1u << 22);             /* run -> not halted */
	}
	for (unsigned ep = 0; ep < 8; ep++) {
		DWC(0xc80c + 0x10 * ep) &= ~(1u << 10); /* endpoint command done */
	}
}

void
mock_barrier(void)
{
	hw_step();
}

void
delay(int usec)
{
	(void)usec;
	hw_step();
}

wait_result_t
thread_block(thread_continue_t continuation)
{
	(void)continuation;
	return 0;
}

/* Memory and cache */
vm_map_t kernel_map;
pmap_t kernel_pmap;
static uint8_t dma_area[0x4000] __attribute__((aligned(0x4000)));
#define DMA_PN     0x123456u

kern_return_t
kmem_alloc_contig(vm_map_t map, vm_offset_t *addr, vm_size_t size, vm_offset_t mask,
    ppnum_t max_pnum, ppnum_t pnum_mask, int flags, int tag)
{
	(void)map; (void)mask; (void)max_pnum; (void)pnum_mask; (void)flags; (void)tag;
	if (size > sizeof(dma_area)) {
		return 1;
	}
	*addr = (vm_offset_t)dma_area;
	return KERN_SUCCESS;
}

ppnum_t
pmap_find_phys(pmap_t pmap, addr64_t va)
{
	(void)pmap;
	return va == (addr64_t)(vm_offset_t)dma_area ? DMA_PN : 0;
}

void
flush_dcache64(addr64_t pa, unsigned len, boolean_t phys)
{
	(void)pa; (void)len; (void)phys;
}

/* Locks and networking: succeed, record nothing beyond the interface. */
static lck_grp_t the_grp;
static lck_mtx_t the_mtx;
lck_grp_t *lck_grp_alloc_init(const char *n, void *a) { (void)n; (void)a; return &the_grp; }
lck_mtx_t *lck_mtx_alloc_init(lck_grp_t *g, void *a) { (void)g; (void)a; return &the_mtx; }
void lck_mtx_lock(lck_mtx_t *m) { (void)m; }
void lck_mtx_unlock(lck_mtx_t *m) { (void)m; }
const uint8_t etherbroadcastaddr[ETHER_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
int ether_demux(void) { return 0; }
int ether_add_proto(void) { return 0; }
int ether_del_proto(void) { return 0; }
int ether_check_multi(void) { return 0; }
int ether_frameout_extended(void) { return 0; }
static int the_ifnet;
static unsigned ifnet_attached, ifaddr_set;
errno_t ifnet_allocate_extended(const struct ifnet_init_eparams *i, ifnet_t *ifp) { (void)i; *ifp = (ifnet_t)&the_ifnet; return 0; }
errno_t ifnet_attach(ifnet_t ifp, const void *ll) { (void)ifp; (void)ll; ifnet_attached++; return 0; }
errno_t ifnet_set_addrlen(ifnet_t ifp, int len) { (void)ifp; (void)len; return 0; }
errno_t ifnet_set_hdrlen(ifnet_t ifp, int len) { (void)ifp; (void)len; return 0; }
errno_t ifnet_set_mtu(ifnet_t ifp, int mtu) { (void)ifp; (void)mtu; return 0; }
errno_t ifnet_set_baudrate(ifnet_t ifp, uint64_t b) { (void)ifp; (void)b; return 0; }
errno_t ifnet_set_flags(ifnet_t ifp, uint32_t f, uint32_t m) { (void)ifp; (void)f; (void)m; return 0; }
errno_t ifnet_set_lladdr(ifnet_t ifp, const void *ll, size_t len) { (void)ifp; (void)ll; (void)len; return 0; }
errno_t ifnet_input(ifnet_t ifp, mbuf_t m, const struct ifnet_stat_increment_param *s) { (void)ifp; (void)m; (void)s; return 0; }
errno_t ifnet_stat_increment_out(ifnet_t ifp, uint32_t p, uint32_t b, uint32_t e) { (void)ifp; (void)p; (void)b; (void)e; return 0; }
errno_t proto_plumb(int pf, ifnet_t ifp) { (void)pf; (void)ifp; return 0; }
static int the_socket;
errno_t sock_socket(int d, int t, int p, void *cb, void *c, socket_t *so) { (void)d; (void)t; (void)p; (void)cb; (void)c; *so = (socket_t)&the_socket; return 0; }
errno_t sock_ioctl(socket_t so, unsigned long req, void *arg) { (void)so; (void)arg; if (req == SIOCAIFADDR) { ifaddr_set++; } return 0; }
errno_t sock_send(socket_t so, const struct msghdr *msg, int flags, size_t *sent) { (void)so; (void)msg; (void)flags; *sent = 0; return 0; }
void sock_close(socket_t so) { (void)so; }
errno_t mbuf_gethdr(int how, int type, mbuf_t *m) { (void)how; (void)type; *m = NULL; return 1; }
errno_t mbuf_mclget(int how, int type, mbuf_t *m) { (void)how; (void)type; (void)m; return 1; }
int mbuf_flags(mbuf_t m) { (void)m; return 0; }
size_t mbuf_maxlen(mbuf_t m) { (void)m; return 0; }
void *mbuf_data(mbuf_t m) { (void)m; return NULL; }
size_t mbuf_len(mbuf_t m) { (void)m; return 0; }
errno_t mbuf_setlen(mbuf_t m, size_t len) { (void)m; (void)len; return 0; }
errno_t mbuf_pkthdr_setlen(mbuf_t m, size_t len) { (void)m; (void)len; return 0; }
size_t mbuf_pkthdr_len(mbuf_t m) { (void)m; return 0; }
errno_t mbuf_pkthdr_setrcvif(mbuf_t m, ifnet_t ifp) { (void)m; (void)ifp; return 0; }
errno_t mbuf_pkthdr_setheader(mbuf_t m, void *h) { (void)m; (void)h; return 0; }
void mbuf_pkthdr_adjustlen(mbuf_t m, int a) { (void)m; (void)a; }
errno_t mbuf_setdata(mbuf_t m, void *d, size_t len) { (void)m; (void)d; (void)len; return 0; }
errno_t mbuf_copydata(mbuf_t m, size_t off, size_t len, void *out) { (void)m; (void)off; (void)len; (void)out; return 0; }
void mbuf_freem(mbuf_t m) { (void)m; }

/* The SYSTEM console ring readers added to santoni_sdhci.c by 0200. */
static unsigned santoni_reader_calls;
uint32_t santoni_log_copy(uint64_t *pos, char *out, uint32_t max);
uint64_t santoni_log_position(void);
uint32_t santoni_log_copy(uint64_t *pos, char *out, uint32_t max) { (void)pos; (void)out; (void)max; santoni_reader_calls++; return 0; }
uint64_t santoni_log_position(void) { santoni_reader_calls++; return 0; }

#include USB_SOURCE

static uint8_t *gcc_mem, *dwc_mem, *qs_mem, *phy_mem;

static void
setup(bool powered, uint32_t gctl)
{
	gcc_mem = mock_add_region(T_GCC_PA, 0x94000);
	dwc_mem = mock_add_region(T_DWC3_PA, 0xd000);
	qs_mem = mock_add_region(T_QS_PA, 0x100);
	phy_mem = mock_add_region(T_QUSB2_PA, 0x400);
	GDSCR = powered ? (1u << 31) : 1u;              /* SW_COLLAPSE set when off */
	for (unsigned i = 0; i < 7; i++) {
		*mock_reg(T_GCC_PA + cbcr_offs[i]) = powered ? 1u : (1u << 31);
	}
	DWC(0xc120) = 0x5533290a;                       /* GSNPSID */
	DWC(0xc110) = gctl;
	DWC(0xc70c) = 1u << 22;                         /* halted */
	*mock_reg(T_QUSB2_PA + 0x38) = 1u << 5;           /* PLL locked */
	*mock_reg(T_QUSB2_PA + 0xb4) = 0x22;              /* powered */
}

int
main(void)
{
#if SCENARIO == 1
	setup(true, 0x00112000u);
	uint8_t *snap_gcc = malloc(0x94000), *snap_dwc = malloc(0xd000);
	uint8_t snap_qs[0x100], snap_phy[0x400];
	memcpy(snap_gcc, gcc_mem, 0x94000);
	memcpy(snap_dwc, dwc_mem, 0xd000);
	memcpy(snap_qs, qs_mem, sizeof(snap_qs));
	memcpy(snap_phy, phy_mem, sizeof(snap_phy));
	mock_bootarg_sweetusb = 1;
	sweet_usb_probe();
	CHECK(memcmp(snap_gcc, gcc_mem, 0x94000) == 0, "sweetusb=1 wrote a GCC register");
	CHECK(memcmp(snap_dwc, dwc_mem, 0xd000) == 0, "sweetusb=1 wrote a DWC3 register");
	CHECK(memcmp(snap_qs, qs_mem, sizeof(snap_qs)) == 0, "sweetusb=1 wrote QSCRATCH");
	CHECK(memcmp(snap_phy, phy_mem, sizeof(snap_phy)) == 0, "sweetusb=1 wrote the PHY");
	CHECK(mock_find(T_DWC3_PA, 4)->maps == 1, "DWC3 not read");
	CHECK(mock_find(T_QUSB2_PA, 4)->maps == 1, "QUSB2 not read");
	CHECK(mock_nthreads == 0, "sweetusb=1 started threads");
	sweet_usb_probe();
	CHECK(mock_find(T_GCC_PA, 4)->maps == 1, "second probe ran again");
#elif SCENARIO == 2
	setup(false, 0x00113000u);                      /* PrtCapDir = 3 (OTG) */
	mock_bootarg_sweetusb = 3;
	sweet_usb_probe();
	CHECK(GDSCR & (1u << 31), "GDSC not powered (0x%08x)", GDSCR);
	CHECK(!(GDSCR & 1u), "SW_COLLAPSE still set");
	for (unsigned i = 0; i < 7; i++) {
		uint32_t c = *mock_reg(T_GCC_PA + cbcr_offs[i]);
		CHECK((c & 1u) && !(c & (1u << 31)), "clock 0x%x not running (0x%08x)", cbcr_offs[i], c);
	}
	CHECK(((DWC(0xc110) >> 12) & 3u) == 2u, "GCTL not in device mode (0x%08x)", DWC(0xc110));
	CHECK((DWC(0xc110) & ~(3u << 12)) == (0x00113000u & ~(3u << 12)), "other GCTL bits changed");
	CHECK(*mock_reg(T_QS_PA + 0x10) & ((1u << 20) | (1u << 28)), "VBUS override not set");
	CHECK(*mock_reg(T_QS_PA + 0x30) & (1u << 24), "LANE0_PWR_PRESENT not set");
	CHECK(DWC(0xc704) & (1u << 31), "RUN_STOP not set");
	CHECK(!(DWC(0xc70c) & (1u << 22)), "controller still halted");
	uint64_t evt = ((uint64_t)DWC(0xc404) << 32) | DWC(0xc400);
	CHECK(evt == ptoa_64(DMA_PN), "event buffer 0x%llx, want 0x%llx",
	    (unsigned long long)evt, (unsigned long long)ptoa_64(DMA_PN));
	CHECK(DWC(0xc720) == 0x3, "DALEPENA 0x%x", DWC(0xc720));
	CHECK(ifnet_attached == 1 && ifaddr_set == 1, "ue0 not created/configured");
	CHECK(mock_nthreads == 2, "threads %u, want event + live-log", mock_nthreads);
#elif SCENARIO == 3
	setup(false, 0x00112000u);
	uint8_t *snap_gcc = malloc(0x94000);
	memcpy(snap_gcc, gcc_mem, 0x94000);
	mock_bootarg_sweetusb = 1;
	sweet_usb_probe();
	CHECK(memcmp(snap_gcc, gcc_mem, 0x94000) == 0, "sweetusb=1 wrote a GCC register");
	CHECK(mock_find(T_DWC3_PA, 4)->maps == 0, "unclocked DWC3 was mapped");
	CHECK(mock_find(T_QUSB2_PA, 4)->maps == 0, "PHY mapped with the controller off");
#endif
	if (mock_failures) {
		fprintf(stderr, "test_lavender_usb scenario %d: %u failure(s)\n", SCENARIO, mock_failures);
		return 1;
	}
	printf("test_lavender_usb scenario %d: OK\n", SCENARIO);
	return 0;
}
