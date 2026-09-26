#!/bin/sh
# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only
#
# Builds the RPM for one architecture in a Sailfish SDK Docker image, as the
# release workflow does. The RPM lands in out/.
#
#   ci/build-rpm.sh <aarch64|armv7hl> [Sailfish OS release, default 5.1.0.11]
set -eu

arch=$1
release=${2:-5.1.0.11}
image=coderus/sailfishos-platform-sdk-$arch:$release

cd "$(dirname "$0")/.."
mkdir -p out
chmod 777 out

# The image runs as its own user (mersdk), so build in a copy of the sources
# in its home rather than in the mounted checkout. scratchbox2 in the image
# runs the ARM tools; the host needs no emulation set up.
docker run --rm -v "$PWD":/share:ro -v "$PWD/out":/out "$image" /bin/bash -euxc '
    mkdir -p ~/build
    cd ~/build
    cp -r /share/. .
    # Leftovers of a local sfdk build in the source tree
    rm -rf RPMS out plugins .sfdk documentation.list Makefile ./*/Makefile
    find . \( -name "*.o" -o -name "moc_*" -o -name "*.moc" \) -delete
    mb2 -t "SailfishOS-$0-$1" build
    cp RPMS/*.rpm /out/
' "$release" "$arch"

ls -l out
