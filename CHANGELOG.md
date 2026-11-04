# Changelog

All notable changes to the `apollo-dc` project will be documented in this file. This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]()

First Dreamcast port of Apollo Save Tool, based on [apollo-ps2](https://github.com/bucanero/apollo-ps2).

### Added

* VMU save management: scan the VMUs plugged into the controllers, pick one, and list its saves with their titles and icons
  - Save details: descriptions, creator, file type, size, date, icon count, VMS CRC check
  - Japanese save titles, drawn with the Dreamcast BIOS font
* Save tools: hex editor, delete, apply cheat codes (the VMS CRC is kept valid)
* Copy saves between VMUs, export saves to `.DCI` and `.VMI`/`.VMS`, import `.DCI`/`.VMI` saves
* Bulk management: copy or export selected/all saves
* Full VMU dumps: raw 128 KB `.BIN` images, as a complete memory card backup
* Offline Saves DB: the [Apollo saves database](https://github.com/bucanero/apollo-saves) on the disc, ready to import into a VMU
* Settings stored on a VMU (`APOLLO__OPTS`)
* Self-booting `.cdi` disc image with the app, the save patches, and the Saves DB
* SD card adapter (`/sd/`) and dcload (`/pc/`) storage
