#!/bin/sh
#
# make_disc.sh - build a self-booting Apollo Save Tool disc image (.cdi)
#
# Usage: make_disc.sh <APOLLO-DC.elf> <apollo-saves dir> <apollo-patches dir> <output.cdi>
#
# Disc layout:
#   /1ST_READ.BIN       the app (mkdcdisc converts and scrambles the ELF)
#   /APOLLO/DATA/       dctitleid.txt + DC *.savepatch, from apollo-patches
#   /SAVEDB/DC/         offline Saves DB: games.txt, <game>/saves.txt, icon0.png, *.zip,
#                       from apollo-saves DC/ (same layout as the online database)
#
# Needs mkdcdisc (https://gitlab.com/simulant/mkdcdisc) in PATH, or MKDCDISC set.

set -e

if [ $# -ne 4 ]; then
	echo "Usage: $0 <APOLLO-DC.elf> <apollo-saves dir> <apollo-patches dir> <output.cdi>" >&2
	exit 1
fi

ELF=$1
SAVES=$2
PATCHES=$3
OUT=$4
MKDCDISC=${MKDCDISC:-mkdcdisc}

for f in "$ELF" "$SAVES/DC/games.txt" "$PATCHES/dctitleid.txt"; do
	[ -e "$f" ] || { echo "Missing: $f" >&2; exit 1; }
done

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/APOLLO/DATA" "$STAGE/SAVEDB"
cp "$PATCHES/dctitleid.txt" "$PATCHES"/DC/*.savepatch "$STAGE/APOLLO/DATA/"
cp -R "$SAVES/DC" "$STAGE/SAVEDB/DC"
# web pages for the GitHub site, not needed on the disc
find "$STAGE/SAVEDB" -name README.md -delete

echo "Disc contents: $(find "$STAGE" -type f | wc -l) files, $(du -sh "$STAGE" | cut -f1)"

"$MKDCDISC" --no-banner \
	--allow-overwrite \
	--disable-data-track-padding \
	-e "$ELF" \
	-D "$STAGE" \
	-n "Apollo Save Tool" \
	-a "Bucanero" \
	-o "$OUT"

echo "Created $OUT"
