---
home: true
title: nekosu - the kernel level rootkit for files manager access control.
footer: GPL Licensed | Copyright © 2025-2026 FMAC-Team
---

## Highlights

- **Powerful Features**
  - Supports file access control, penetrating deep into the kernel, allowing users to customize return codes.
  - Simple and easy-to-use root feature.
  - Can build as LKM mode (don't support fmac).
  - In the future, mounting modules via file redirection will be allowed.

- **Fmac support**
  - We allow the replacement of syscall functions to support FMAC functionality.
  - The configuration is simple and easy to understand, and it supports booting in safe mode(temp disable).

## About us:

Nekosu is part of the fmac module and is planned to be decoupled from fmac in the future.

## Get nekosu

You can get nekosu lkm for arm64 and x86_64 devices in [Release](https://github.com/FMAC-Team/build_action/releases).
If you want fmac feature,you need to clone our project and replace syscall function.The doc will be coming soon. :⁠-⁠)

Alternatively, initramfs can be patched via Nekosu Manager in the future.

Everything will come soon. 😶‍🌫️
