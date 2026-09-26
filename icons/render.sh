#!/bin/sh
# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only
# Renders every icon-m-*.svg for every Silica theme scale. The PNGs are
# committed, so building the package does not need an SVG renderer.
# Sizes are icon-m in each sailfish-content-graphics-*-zN package.
set -e
cd "$(dirname "$0")"
for svg in icon-m-*.svg; do
    name=${svg%.svg}
    for spec in z1.0:64 z1.25:80 z1.5:96 z1.5-large:72 z1.75:112 z2.0:128 z2.5:160; do
        scale=${spec%%:*}
        size=${spec##*:}
        mkdir -p "$scale"
        rsvg-convert -w "$size" -h "$size" "$svg" -o "$scale/$name.png"
    done
done
