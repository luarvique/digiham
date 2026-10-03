#!/usr/bin/env bash
set -euo pipefail

# This is an example EasyPal decoding pipeline to show the basic usage of the tools in this project.
# The individual steps are documented below.

if [ $# -eq 0 ]; then
    echo "Usage: $0 frequency"
    exit 1
fi

# this gives us an upper sideband demodulated audio signal at 12kHz
rtl_fm -f $1 -M usb -s 12000 | \
# the toolchain needs 32bit float input, so we need to convert it
csdr convert_s16_f | \
# decode the received files (usually JPEG images) and write them to a single file
# in raw mode, only the bytes of the files are written
easypal_decoder --raw > image.jpg
