# Host tests

The base tree has a host-side test suite that compiles the **real patched SDHCI driver** against a mock MMIO/card backend. It caught a real bug in the 0192 patch before the kernel build: the new helper could not be named `sdhci_wait_card_ready` because the CMD6 helper already has that name. It was renamed `sdhci_wait_write_ready`.

## What is covered

- SDHCI register layout (SDCC v4 and v5), PIO reads/writes, FIFO/PRESENT_STATE fallbacks.
- CMD24/CMD25 write path, transfer-complete and the new `sdhci_wait_write_ready()`.
- Cache state (`CACHE_CTRL`), `sdhci_synchronize_cache()` and the 30 s cache barrier.
- Recovery paths (vendor reset, clock cycle, inhibit handling).
- GPT parser with the SYSTEM log fence, log ring packing/wrap-around, GIC wait, payload checks.

## Run

In the base tree (`aLpHa-Git-69/ios-lavender`):

```bash
bash tests/run_tests.sh
```

The relevant files are:

- `tests/test_sdhci_current.c` — the current-driver mock; includes the write path, lock behavior and the timeout test.
- `tests/prepare_kernel_mock.py` — rebuilds the mock from the patch series with `git apply`; the selected function list must include `sdhci_wait_write_ready`.
- `tests/test_log_ring.c` — `SANTONILOG1` header packing and the 4 MiB ring decoder.

The 0192 test asserts that a write whose card never becomes ready returns `ETIMEDOUT` after the **30 s** `CACHE_FLUSH_TIMEOUT_NS` bound (the mock advances simulated time). The 1 s `CMD6_BUSY_TIMEOUT_NS` variant was rejected on hardware: it stopped at `cc_fips_test`.

## Note

The tests are in the base fork, not duplicated here. This directory documents what they cover and why the 0192 timeout is 30 s. If you change the SDHCI driver, run the full suite before building the kernel.
