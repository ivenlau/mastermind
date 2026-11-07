<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, sound effects, and battery profiles, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

| File | Description |
| --- | --- |
| [`fonts/cipher_chars.txt`](fonts/cipher_chars.txt) | Character inventory (383 CJK/symbol chars) extracted from `main/` string literals; the `--symbols` input for every font below. ASCII 0x20-0x7E is included via `--range`. |
| [`fonts/cipher_font_12.c`](fonts/cipher_font_12.c) · [`fonts/cipher_font_14.c`](fonts/cipher_font_14.c) · [`fonts/cipher_font_16.c`](fonts/cipher_font_16.c) | Noto Sans SC Regular subset, 12/14/16 px, bpp 4, uncompressed, for the Super Cipher Machine UI body/status text. |
| [`fonts/cipher_font_20.c`](fonts/cipher_font_20.c) · [`fonts/cipher_font_28.c`](fonts/cipher_font_28.c) | Noto Sans SC Bold subset, 20/28 px, bpp 4, uncompressed, for titles and large numerals. |

Source: Noto Sans SC (SIL Open Font License 1.1) static OTFs from
`https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf`
(sha256 `faa6c9df…706d5ea9`) and `…/NotoSansSC-Bold.otf` (sha256 `c6cb5a93…0b64767c0`).
The 8 MB source OTFs are kept outside the repository and are not committed.

Conversion (pinned `lv_font_conv` 1.5.3, run from the workspace `fonttools/`):

```bash
python tools/gen_cipher_font.py --fonts-dir ../fonttools/fonts
```

Destination: compiled into `main` via `main/CMakeLists.txt` `target_sources`;
symbols `cipher_font_12/14/16/20/28` with Montserrat fallback in `main/ui_font.c`.
`tests/test_font_inventory.py` fails the static gate when UI text changes without
regenerating. Flash cost of the five subsets is about 1.2 MB (8 MB Flash, plenty).

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |
| [`images/screenshots/realshot5.png`](images/screenshots/realshot5.png) | 240 × 320, PNG | Real-device capture: home screen, brass theme, status-bar clock. Reserved cover material for community play updates. |
| [`images/screenshots/realshot7.png`](images/screenshots/realshot7.png) | 240 × 320, PNG | Real-device capture: Standard-mode game board with canvas rows. Reserved cover material for community play updates. |
| [`images/screenshots/realshot8.png`](images/screenshots/realshot8.png) | 240 × 320, PNG | Real-device capture: Digits mode with ten distinct digit colors. Reserved cover material for community play updates. |
| [`images/screenshots/image1.png`](images/screenshots/image1.png) | 768 × 1024, PNG RGB | AI-generated promo poster: "Super Cipher Machine" logo and mascot over a stylized Standard-mode board with the solve hint. Reserved main cover for the gameplay release. Supplied by the fork owner on 2026-11-07. |
| [`images/screenshots/image2.png`](images/screenshots/image2.png) | 1536 × 2048, PNG RGB | AI-generated promo render: the AI Passport playing a Standard-mode game on a desk. Reserved main cover for the gameplay release. Supplied by the fork owner on 2026-11-07. |
| [`images/screenshots/image3.png`](images/screenshots/image3.png) | 768 × 1024, PNG RGB | AI-generated promo render: the AI Passport clipped to a backpack strap running Digits mode. Reserved main cover for the gameplay release. Supplied by the fork owner on 2026-11-07. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Battery profiles

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
contains the supplied CW2017 profile for the Wanli New Energy 500 mAh cell
(profile2, XIL40X, 2026-10-20). It is an unchanged copy of the project-supplied
file, retained at the maintainer's request for this firmware update. Its
original filename is recorded in the paired Chinese document. No separate
license was included in the supplied file.

The format is 80 lines of two hexadecimal digits, one byte per line in register
order. The BSP embeds those bytes in `components/bsp/src/bsp_battery.c`; the
host gate checks the source file and initializer for an exact match. Text-file
SHA-256: `7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`.
Decoded 80-byte SHA-256:
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`.
This is a fuel-gauge profile, not a charging configuration or a generic profile
for every 500 mAh cell; see the [hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).
