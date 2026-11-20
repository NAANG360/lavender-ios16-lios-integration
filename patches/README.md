# Patches

The full XNU patch series lives in [`aLpHa-Git-69/ios-lavender`](https://github.com/aLpHa-Git-69/ios-lavender) (`xnu/patches/`, branch `lavender`). This directory contains the lavender delta that produced the lock-screen boot.

## Included here

| Patch | Purpose |
| --- | --- |
| `0191-lavender-touch-stub-common-headers.patch` | Build the shared `santoni_touch_probe()` entry point as an unsupported-board stub and keep the common headers |
| `0192-sdhci-wait-card-ready-after-write.patch` | Wait for DAT0 + `TRAN`/`READY_FOR_DATA` (CMD13) after every PIO write; the eMMC write fix |

`0190-lavender-gic-rwp-and-reset-code-clean.patch` is also part of the lavender series (GIC RWP wait and reset cleanup); it is in the base fork and is not duplicated here.

## Applying

Clone the base fork at the pinned commit, copy these patches into `xnu/patches/`, and let the CI apply the series:

```bash
git clone -b lavender https://github.com/aLpHa-Git-69/ios-lavender.git
cd ios-lavender
cp /path/to/repositorio/patches/0191-*.patch xnu/patches/
cp /path/to/repositorio/patches/0192-*.patch xnu/patches/
git add xnu/patches
git commit -m "lavender: touch stub + sdhci write-ready"
git push origin lavender
```

The `xnu.yml` workflow applies every `xnu/patches/*.patch` with `git apply` and fails if a critical patch is skipped.

## 0192: the 30 s vs 1 s timeout

The working patch uses `CACHE_FLUSH_TIMEOUT_NS` (**30 s**). A 1 s variant using `CMD6_BUSY_TIMEOUT_NS` was built as `0192b` (CI run `44636855966`, kernel `2a6b5421…`) and **rejected**: it stopped at `cc_fips_test` because some writes need more than one second. Do not shorten the timeout without a card-specific measurement.

## Tests

The base tree has host tests for the SDHCI driver:

- `tests/test_sdhci_current.c` compiles the real patched driver against a mock MMIO/card backend.
- `tests/prepare_kernel_mock.py` rebuilds the mock from the patch series with `git apply`.
- `tests/run_tests.sh` runs the mock, GPT parser, log ring, GIC wait, payload and patch-structure checks.

The 0192 change is covered by the write path, the lock behavior and the `card not ready after write` timeout test. See `tests/README.md`.

## 0191 note

The touch entry point is still a stub. It compiles as an unsupported-board stub so the kernel builds for lavender while the Novatek driver is missing. It is not a working touch driver.
