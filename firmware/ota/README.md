# OTA files for dijilele.com

Upload these to **https://dijilele.com/** (site root) when you release firmware.

## Manifests

**Auto-updated on build** (from `version.txt` + `DIJILELE_PRODUCT_ID`) into this folder:

| File | Product |
|------|---------|
| `dijilele-firmware-latest-dijilele-m.json` | Dijilele M (id 1) |
| `dijilele-firmware-latest-dijilele-s.json` | Dijilele S (id 2) |

Matching `dijilele-firmware-version-<slug>.txt` files are updated too.

To refresh manually: `./update-ota-manifest.sh` (requires `DIJILELE_PRODUCT_ID=1` or `2`).

Upload the JSON/txt for the product you built to **https://dijilele.com/** (site root).

## Binaries (from `firmware/` after build)

| File | Build command |
|------|----------------|
| `dijilele-m-1-0-4.bin` | `DIJILELE_PRODUCT_ID=1 ./build-16mb.sh build` |
| `dijilele-s-1-0-4.bin` | `DIJILELE_PRODUCT_ID=2 ./build-4mb.sh build` |

Version segments use dashes (`1-0-4`), not dots.

## Legacy (old firmware without product id)

- `dijilele-firmware-latest.json`
- `dijilele-firmware-version.txt`
- `dijilele-1-0-4.bin`

## Server

- HTTPS GET, no auth
- **CORS required** for Instrument Control in the browser / Flutter web (WebView uses native HTTP and does not need CORS):
  ```
  Access-Control-Allow-Origin: *
  ```
  On Cloudflare Pages / static hosting, add a `_headers` file or Page Rule. Without this, `fetch()` from the app WebView origin fails even when the JSON file exists.
- `.json` → `application/json`, `.bin` → `application/octet-stream`
