# Notice and third-party credits

This repository contains original documentation and scripts, plus a small lavender delta. It does not contain Apple binaries or the base XNU tree.

## Base projects

- **ios-sweet** — [`dipa-studio/ios-sweet`](https://github.com/dipa-studio/ios-sweet), MIT License, Copyright (c) 2026 dipa-studio. The upstream tooling and methodology.
- **ios-lavender** — [`aLpHa-Git-69/ios-lavender`](https://github.com/aLpHa-Git-69/ios-lavender), branch `lavender`. The XNU tree, shim, eMMC SDCC v5 driver, lavender DTB and the `NOTES.md` bring-up log. The repository does not ship a LICENSE file in the checked-out branch; its code remains with its authors and is used here under the upstream project's terms. Contact the authors for relicensing.
- **bergamota** — [`ry-diffusion/bergamota`](https://github.com/ry-diffusion/bergamota), `mkiosroot-macos.sh`, `img2simg.py` and shim ideas. The checked-out branch does not ship a LICENSE file; its code remains with its authors.
- **ios-topaz** — [`MakrSas/ios-topaz`](https://github.com/MakrSas/ios-topaz), reference for the A73/A53 topology, Wi-Fi and GPU work.
- **HakiOS** — [`oosexgang/hakios-xnu-kernel`](https://github.com/oosexgang/hakios-xnu-kernel), methodology reference. No `sdm660` support.

## Apple

- **XNU** — Apple open-source `xnu-8796`, Apple Public Source License (APSL).
- The iOS userland, IPSW, device trees, dyld shared cache and other Apple binaries are **not** included and are not licensed by this repository.

## Tools

- **TWRP** — the lavender recovery image used for backups/logs; see [twrp.me](https://twrp.me/xiaomi/xiaomiredminote7.html).
- **LLVM/clang/LLD** — kernel and shim toolchain.
- **Docker** — reproducible shim build.

## Photo

`docs/images/lock-screen.jpg` is a photo of the project author's Redmi Note 7 running the iOS 16.7.16 lock screen. It is included as evidence of the milestone.

## Original work

The original documentation, the `0192` write-ready patch, the host-test changes, the `fix-datamigration.sh` helper and the repository organization are MIT (see `LICENSE`).
