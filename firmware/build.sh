#!/usr/bin/env bash
# Build a firmware under firmware/<name>/ with the TLSR8232 SDK of FitPro-LT715-TLSR8232
# (pinned commit) in its Docker image, then make it an OTA image.
#   firmware/build.sh dumper      -> firmware/dumper/_build/lt716_dumper.ota.bin
set -eu
here=$(cd "$(dirname "$0")" && pwd)
name=${1:-dumper}
elab=$here/vendor/FitPro-LT715-TLSR8232
commit=cebf23e
image=ghcr.io/e-labinnovations/tlsr8232-sdk
if [[ ! -d $elab ]]; then
  git clone -q https://github.com/e-labInnovations/FitPro-LT715-TLSR8232 "$elab"
fi
git -C "$elab" checkout -q "$commit"
mkdir -p "$elab/examples/lt716_$name"   # mount point (the tree is mounted read-only)
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$elab":/src:ro -v "$here/$name":/src/examples/lt716_$name \
  -w /src/examples/lt716_$name "$image" make
target=$(sed -n 's/^TARGET *= *//p' "$here/$name/Makefile")
python3 -I "$here/telink_fw.py" "$here/$name/_build/$target.bin" "$here/$name/_build/$target.ota.bin"
