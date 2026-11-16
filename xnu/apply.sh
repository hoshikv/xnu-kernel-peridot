#!/bin/bash
# Get Apple's XNU sources (iOS 27 / xnu-10063.141.1) and put the HakiOS work on top.
# usage: xnu/apply.sh [DIR]              (default: $XNU_SRC, else xnu-src/xnu-10063.141.1)
# env:   XNU_URL   where to fetch from (default: Apple's repository on GitHub)
set -e
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DIR=${1:-${XNU_SRC:-$HERE/../xnu-src/xnu-10063.141.1}}
URL=${XNU_URL:-https://github.com/apple-oss-distributions/xnu.git}
TAG=xnu-10063.141.1

[ -e "$DIR" ] && { echo "apply.sh: $DIR already exists; not touching it" >&2; exit 1; }
[ -s "$HERE/hakios.patch" ] && [ -d "$HERE/files" ] || { echo "apply.sh: no hakios.patch and files/ in $HERE" >&2; exit 1; }
[ -f "$HERE/../drivers/map" ] || { echo "apply.sh: no drivers/map next to xnu/ (the drivers are in drivers/)" >&2; exit 1; }
mkdir -p "$(dirname "$DIR")"
DIR=$(cd "$(dirname "$DIR")" && pwd)/$(basename "$DIR")
git init -q "$DIR"
trap 'echo "apply.sh: failed; removing the incomplete $DIR" >&2; rm -rf "$DIR"' ERR
cd "$DIR"
git remote add origin "$URL"
git fetch -q --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG"
git checkout -q -b hakios "$TAG"
echo 'BUILD/' >> .git/info/exclude
git apply --whitespace=nowarn "$HERE/hakios.patch"
cp -a "$HERE/files/." .
# HFS Plus (the data volume): Apple's release, fetched like XNU, and this port's changes over it
"$HERE/hfs/hfs.sh" put "$DIR"
# the drivers are kept by subsystem (drivers/<subsystem>/), not at their place in Apple's tree: drivers/map says where
# each one goes, one line per file: "<subsystem> <path in the tree>"
NDRIVERS=0
while read -r sub tree _; do
	case "$sub" in ''|'#'*) continue ;; esac
	[ -f "$HERE/../drivers/$sub/$(basename "$tree")" ] || { echo "apply.sh: drivers/$sub/$(basename "$tree") is in drivers/map but missing" >&2; false; }
	mkdir -p "$(dirname "$tree")"
	cp -p "$HERE/../drivers/$sub/$(basename "$tree")" "$tree"
	NDRIVERS=$((NDRIVERS + 1))
done < "$HERE/../drivers/map"
# the drivers of the SoC and device repositories checked out (socs/*/drivers/, devices/*/drivers/)
python3 "$HERE/../tools/hakios-config.py" external-files | while IFS="$(printf '\t')" read -r src tree; do
	mkdir -p "$(dirname "$tree")"
	cp -p "$src" "$tree"
done
# which of them are built is the configuration's (drivers/*/conf, CONFIG_HAKIOS_*): put into the tree as XNU reads it.
printf '%s\n' config/MASTER.hakios bsd/dev/hakios.drivers.h >> .git/info/exclude
python3 "$HERE/../tools/hakios-config.py" kernel --tree "$DIR"
echo "apply.sh: $DIR: $TAG + $(grep -c '^diff --git' "$HERE/hakios.patch") changed and $(find "$HERE/files" -type f | wc -l) new files + $NDRIVERS drivers"
