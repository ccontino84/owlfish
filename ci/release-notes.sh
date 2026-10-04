#!/bin/sh
# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only
#
# Prints the CHANGELOG.md entry of one version (the lines under its
# "## <version> (date)" heading), for the GitHub release text. Fails if the
# changelog has no entry for it.
#
#   ci/release-notes.sh <version> [changelog, default CHANGELOG.md]
set -eu

version=$1
changelog=${2:-CHANGELOG.md}

notes=$(awk -v heading="## $version" '
    $0 == heading || index($0, heading " (") == 1 { found = 1; next }
    found && /^## / { exit }
    found { print }
' "$changelog" | sed -e '/./,$!d')
if [ -z "$notes" ]; then
    echo "No entry for $version in $changelog" >&2
    exit 1
fi
printf '%s\n' "$notes"
