# Clock QR

`clock_qr.[ch]` uses the Nayuki C encoder v1.8.0, pinned to commit
`720f62bddb7226106071d4728c292cb1df519ceb`. Original source:
[qrcodegen.c](https://github.com/nayuki/QR-Code-generator/blob/720f62bddb7226106071d4728c292cb1df519ceb/c/qrcodegen.c),
[qrcodegen.h](https://github.com/nayuki/QR-Code-generator/blob/720f62bddb7226106071d4728c292cb1df519ceb/c/qrcodegen.h).
MIT notice is in `LICENSE`. Upstream names are retained for auditability;
comments, indentation and branch braces follow this repository's convention.
Two equivalent unsigned enum range assertions avoid ARM GCC `-Wtype-limits`.

The wrapper fixes version 4, medium correction and mask 3 with byte mode.
It needs 138-byte output and scratch buffers, accepts 1–62 bytes, and uses
no heap or static mutable scratch. Fixed settings make independent fixtures
deterministic. Both generated Wi-Fi identity and URL fit this budget.

Run `BSP/QRCode/tests/run_tests.ps1` from repository root. MSVC C11 `/W4 /WX`
tests compare every module with independently generated reference matrices,
then test actual page draw calls, screen bounds, five-pixel modules and four
modules of quiet zone. PPM page rasters are written to `build/tests/qr/`.
No Python packages are required for this baseline.

For optional independent decoding and PNG exports, install Pillow and
`zxing-cpp` into a local environment, then run:

```text
python BSP/QRCode/tests/verify_previews.py build/tests/qr build/wifi-validation
```

The static reference matrices can be regenerated using Python `qrcode==8.2`:

```text
python BSP/QRCode/tests/generate_reference.py
```

Firmware integration requires source paths `BSP/QRCode/clock_qr.c` and
`BSP/QRCode/qrcodegen.c`, plus include path `BSP/QRCode`.
