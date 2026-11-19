# Changelog

All notable changes to the `apollo-dc` project will be documented in this file. This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]()

## [v1.0.0](https://github.com/bucanero/apollo-dc/releases/tag/v1.0.0) - 2026-11-19

First Dreamcast release of Apollo Save Tool, based on [apollo-ps2](https://github.com/bucanero/apollo-ps2).

### Added

* VMU save management: scan the VMUs plugged into the controllers, pick one, and list its saves with their titles and icons
  - Save details: descriptions, creator, file type, size, date, icon count, VMS CRC check, copy protection
  - Japanese save titles, drawn with the Dreamcast BIOS font
  - VMU save icons, shown at 64x64
* Save tools: hex editor, delete, apply cheat codes and save patches (the VMS CRC is kept valid)
  - Save patch engine updated to apollo-lib v3, with MicroPython script support
* Lock / unlock saves: toggle a save's copy-protection flag, applied with "Apply Changes"
* VMZip compressed saves, compatible with Bucanero's [VMZip](https://github.com/bucanero/vmzip) (`VMZIPDAT.NNN` files)
  - Compress a save (bzip2) to the same or another VMU, and decompress it back
  - The original save's date and time are kept, and restored on decompression
  - Details show the original file, size, and compression ratio
  - Mini-games are not compressed
* Copy saves between VMUs, export saves to `.DCI` and `.VMI`/`.VMS`, import `.DCI`/`.VMI` saves
* Bulk management: copy or export selected/all saves
* Full VMU dumps: raw 128 KB `.BIN` images, as a complete memory card backup
* VMU images (`.BIN`, `.VMU`, `.DCM`) on SD card or PC: open them like a VMU, and import saves into them
* Saves DB: the [Apollo saves database](https://github.com/bucanero/apollo-saves), ready to import into a VMU
  - Online Saves DB with a Broadband Adapter (HTTPS), with the offline copy on the disc as fallback
* Background music (S3M player)
* Settings stored on a VMU (`APOLLO__OPTS`)
* Startup splash shown while the app loads, and pixel art on the VMU screens
* Self-booting `.cdi` disc image with the app, the save patches, and the Saves DB
* SD card adapter (`/sd/`) and dcload (`/pc/`) storage
