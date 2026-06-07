# FilaMan-System-ESP32 — ACE Pro Hybrid Fork

This is a fork of [Fire-Devils/FilaMan-System-ESP32](https://github.com/Fire-Devils/FilaMan-System-ESP32)
(the ESP32 scale/NFC firmware for the [FilaMan](https://github.com/Fire-Devils/filaman-system)
filament management system) that adds **Anycubic ACE Pro NFC tag support**.

Tags written by this firmware are **dual-format**: a single NTAG215 carries both
the Anycubic ACE Pro binary format *and* an OpenSpool-compatible NDEF record —
so the same spool is recognized natively by the ACE Pro, by Bambu-Lab-style
OpenSpool readers and by the FilaMan scale itself.

## Quick start: how to write an ACE Pro (hybrid) tag

> **TL;DR — three steps, then every tag you write is ACE Pro compatible:**

1. **Flash this firmware** on the FilaMan scale
   ([Releases](../../releases) → `upgrade_filaman_firmware_acepro_*.bin` via the scale's web UI).
2. **Enable hybrid mode once** — just open this URL in any browser
   (stored in flash, survives reboots):

   | Action | URL | Response |
   |---|---|---|
   | Enable | `http://<scale-ip>/api/acepro?enabled=1` | `{"hybrid": true}` |
   | Disable | `http://<scale-ip>/api/acepro?enabled=0` | `{"hybrid": false}` |
   | Status | `http://<scale-ip>/api/acepro` | current state |
3. **Write the tag from FilaMan as usual:** open the spool → *Write RFID tag* →
   pick the scale device → hold the tag to the reader. That's it — the result
   is a dual-format tag (ACE Pro + OpenSpool NDEF + FilaMan spool id).

**Requirements & tips**

- Use **NTAG215** tags (NTAG213 is too small for the dual format).
- Spools used in an ACE Pro need a tag on **both sides** (one reader per
  slot pair) — simply flip the spool and write again.
- Verify what's on a tag: `GET http://<scale-ip>/api/dump` (hex dump, pages 0–45).
- If the ACE shows a previously failed tag as unreadable even after a rewrite:
  **power-cycle the ACE** — it caches the RFID state per tag UID.
- One-off hybrid write without the global toggle: include `"format":"acepro"`
  in the write payload.

## What this fork adds

- **Hybrid tag writing** (`nfc_acepro.cpp/h`): ACE Pro binary format on
  pages 4–34 (magic `7B 00 65 00`, SKU, brand, material, color, temps),
  FilaMan spool id on pages 35–39, OpenSpool NDEF from page 40
- **Hybrid tag reading**: fast path and full read auto-detect the format and
  locate the NDEF record accordingly
- **Persistent hybrid mode**: `GET /api/acepro?enabled=1` — when enabled, every
  regular write job from the FilaMan UI produces a hybrid tag (no backend
  changes required); alternatively send `"format":"acepro"` in the write payload
- **Diagnostic endpoint**: `GET /api/dump` returns a raw hex dump of tag
  pages 0–45 (invaluable for debugging tag issues)
- **Robustness fixes**: full user-area wipe before each write, zero-initialized
  tag structs (garbage bytes make the ACE reject a tag), tag-presence retry,
  scan-path-consistent UID reporting
- **SKU scheme** `VENDOR-MATERIAL-SPOOLID` (e.g. `ESUN-PLA-1`): the trailing
  segment carries the FilaMan spool id, which downstream integrations
  (e.g. Klipper/ACE panel live sync) can resolve back to real spool data

The reverse-engineered tag format — including empirically verified findings on
what the ACE Pro firmware actually reads, validates and ignores — is documented
in [`docs/acepro_specification.md`](docs/acepro_specification.md).

## How this fork stays current

A scheduled GitHub Actions workflow merges `Fire-Devils/FilaMan-System-ESP32`
`main` into this branch **daily**, builds the firmware and publishes a release
with ready-to-flash binaries. Upstream fixes therefore land here automatically;
merge conflicts or build failures open an issue instead of silently breaking.

Grab the latest binaries from the [Releases](../../releases) page:

| Asset | Purpose |
|---|---|
| `upgrade_filaman_firmware_acepro_*.bin` | firmware update via the scale's web UI |
| `upgrade_filaman_website_acepro_*.bin` | web UI (LittleFS) update, when needed |
| `filaman_full_acepro_*.bin` | full image for initial flashing via USB |

## Hardware, setup and general documentation

Unchanged from upstream — see the
[original README](https://github.com/Fire-Devils/FilaMan-System-ESP32#readme)
and the [FilaMan documentation](https://docu.filaman.app).

## Credits

- [ManuelW77](https://github.com/ManuelW77) / Fire-Devils — FilaMan and the
  original firmware
- ACE Pro hybrid tag support originally developed in
  [Anzarion/Filaman](https://github.com/Anzarion/Filaman) (v2.x standalone
  firmware) and ported to the v3 FilaMan-System architecture here
- Tag format research: [DnG-Crafts/ACE-RFID](https://github.com/DnG-Crafts/ACE-RFID),
  [Molodos/anycubic-nfc-filament](https://github.com/Molodos/anycubic-nfc-filament),
  [mrRobot62/anycubic_filament_sku_sniffer](https://github.com/mrRobot62/anycubic_filament_sku_sniffer)

Licensed under the MIT License, same as upstream.