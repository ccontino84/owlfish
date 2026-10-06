// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

.pragma library

// The settings the page edits, with their defaults; they must match the
// lipstick plugin (settings.h). "Reset to defaults" unsets these keys.
var values = {
    enabled: false,
    temperature: 4500,
    saturation: 100,
    schedule: false,
    schedule_sun: false,
    schedule_from: 1260,
    schedule_to: 420,
    schedule_transition: 60,
    location_manual: false,
    // Out of range: not set
    latitude: 1000,
    longitude: 1000,
    dim: 0,
    dim_when: "always",
    dim_from: 1260,
    dim_to: 420,
    dim_cutoff: true,
    dim_cutoff_lux: 1000,
    correction: "none",
    correction_strength: 50
}

// Also unset by the reset: the troubleshooting key, and keys that only
// pre-releases used. Not the keys the plugin writes (als_*, sun_*, auto_*).
var hiddenKeys = ["renderer", "debug_renderer", "debug_matrix", "debug_transfer",
                  "profile", "filter", "preset"]
