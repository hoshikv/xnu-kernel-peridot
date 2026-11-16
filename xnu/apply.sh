#!/bin/bash
set -e
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DIR=${1:-${XNU_SRC:-$HERE/../xnu-src/xnu-8796.121.2}}
URL=${XNU_URL:-https://github.com/apple-oss-distributions/xnu.git}
TAG=xnu-8796.121.2
BASE=c54f35ca767986246321eb901baf8f5ff7923f6a

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
[ "$(git rev-parse HEAD)" = "$BASE" ] || { echo "apply.sh: $TAG is $(git rev-parse HEAD), expected $BASE" >&2; false; }
echo 'BUILD/' >> .git/info/exclude
git apply --whitespace=nowarn "$HERE/hakios.patch"

# Add arm64e/PAC support for SM8635 (peridot) using Python for reliable edits
python3 << 'PYEOF'
import re

# BCM2837.h: add HAS_NATIVE_PAC for arm64e
with open('pexpert/pexpert/arm64/BCM2837.h', 'r') as f:
    content = f.read()
if 'HAS_NATIVE_PAC' not in content:
    content = content.replace(
        '#define __ARM64_PMAP_SUBPAGE_L1__ 1',
        '#define __ARM64_PMAP_SUBPAGE_L1__ 1\n\n/*\n * arm64e on a non-Apple ARMv8.3 CPU: pointer authentication uses the\n * architectural key registers (see HAS_NATIVE_PAC in arm64/pac_asm.h).\n */\n#if defined(__arm64e__)\n#define HAS_APPLE_PAC             1\n#define HAS_NATIVE_PAC            1\n#endif'
    )
    with open('pexpert/pexpert/arm64/BCM2837.h', 'w') as f:
        f.write(content)

# board_config.h: update MAX_CPUS comment
with open('pexpert/pexpert/arm64/board_config.h', 'r') as f:
    content = f.read()
content = content.replace(
    '#define MAX_CPUS                       8 /* the Raspberry Pi has four; the boards this runs on now have up to eight */',
    '#define MAX_CPUS                       8 /* SM8635: 1+3+4 cores */'
)
with open('pexpert/pexpert/arm64/board_config.h', 'w') as f:
    f.write(content)
PYEOF

cp -a "$HERE/files/." .
"$HERE/hfs/hfs.sh" put "$DIR"
NDRIVERS=0
while read -r sub tree _; do
	case "$sub" in ''|'#'*) continue ;; esac
	[ -f "$HERE/../drivers/$sub/$(basename "$tree")" ] || { echo "apply.sh: drivers/$sub/$(basename "$tree") is in drivers/map but missing" >&2; false; }
	mkdir -p "$(dirname "$tree")"
	cp -p "$HERE/../drivers/$sub/$(basename "$tree")" "$tree"
	NDRIVERS=$((NDRIVERS + 1))
done < "$HERE/../drivers/map"
python3 "$HERE/../tools/hakios-config.py" external-files | while IFS="$(printf '\t')" read -r src tree; do
	mkdir -p "$(dirname "$tree")"
	cp -p "$src" "$tree"
done
printf '%s\n' config/MASTER.hakios bsd/dev/hakios.drivers.h >> .git/info/exclude
python3 "$HERE/../tools/hakios-config.py" kernel --tree "$DIR"
echo "apply.sh: $DIR: $TAG + $(grep -c '^diff --git' "$HERE/hakios.patch") changed and $(find "$HERE/files" -type f | wc -l) new files + $NDRIVERS drivers"
