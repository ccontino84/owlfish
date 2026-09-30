# Owlfish

Easier on the eyes in the evening: Owlfish tints the Sailfish OS display
warmer and can make it darker than its lowest brightness, like Android's
*Night Light* and *Extra Dim* in one.

- **Warmth** from no tint (6500 K) down to no blue at all (1900 K), all the
  time, between fixed times, or from sunset to sunrise, with a gradual change
  at the start and end.
- **Dimming** on top of the current brightness, by up to 75 %, optionally only
  in the dark: the light sensor turns it off in bright light, for example
  outdoors, and back on when it gets darker.
- One switch, in Settings and as a top menu shortcut.

Black stays black: the filter multiplies each colour channel, so it does not
wash out dark areas the way an overlay app does.

## Tested on

| Device | Sailfish OS |
|---|---|
| Jolla Phone (2026) | 5.2.0.17 |
| Sony Xperia 10 III | 5.1.0.11 |

Other devices with Sailfish OS 5.x are likely to work, but have not been
tried yet. Reports are welcome in the
[issues](https://github.com/ccontino84/owlfish/issues).

Owlfish was written with an AI coding assistant (Claude). It was designed,
reviewed and tested on real devices by the author.

## Install

1. Download the RPM for your device from the
   [latest release](https://github.com/ccontino84/owlfish/releases/latest):
   `aarch64` for most current devices (Jolla Phone, Xperia 10 II/III/IV and
   later), `armv7hl` for 32-bit installations.
2. In **Settings > Untrusted software**, turn on *Allow untrusted software*.
3. Open the downloaded file (from the browser's downloads or the Files app)
   and install it.
4. **Restart the phone.** The home screen only loads Owlfish when it starts.
5. Open **Settings > Owlfish** (under Look and feel) and switch it on. To add
   the top menu shortcut, go to Settings > Top menu and turn on "Owlfish"
   under *Switches*.

**Upgrading** works the same way: install the new RPM over the old one and
restart the phone, because the old version keeps running until then. Your
settings are kept.

With Developer mode, the same from a terminal:

```sh
devel-su pkcon install-local owlfish-*.rpm
systemctl --user restart lipstick     # restarts the home screen; closes open apps
```

## Using it

**Settings > Owlfish.** Options that do not apply are greyed out, not
hidden.

- **Enabled**: the master switch, also available as the top menu shortcut
  (tap to toggle, long press to open the page).
- **Colour temperature**
  - **Warmth**: 6500 K (no tint) to 1900 K (no blue), default 4500 K.
  - **When**: *All the time*, *Fixed times* (default 21:00-07:00) or
    *Sunset to sunrise*; neutral the rest of the day. **Gradual change**
    (off, 30 min, 1 h, 2 h) warms up from the start and is back to neutral
    at the end.
- **Dimming**
  - **Darker than the current brightness**: off, or 5-75 %.
  - **Only in the dark**: the dimming fades out above the chosen light
    level and comes back below 75 % of it. A meter shows the light level
    now.

The schedule only affects the colour and the light sensor only the dimming:
warm at night, darker in the dark.

### Sunset to sunrise

Owlfish calculates the times on the phone for your time zone's city (Berlin
for Europe/Berlin, for example), without location services or network, and
follows you to other time zones. The page shows today's times.

The city may be some way from you. Longitude moves both times by 4 minutes
per degree (about 70 km in central Europe) all year. Latitude changes the
length of the night, little around the equinoxes but most at the
solstices: Munich's and Kiel's nights (both on Berlin time) then differ by
about an hour. If the times are too far off, turn on **Set location
manually** and paste your coordinates from a map app, e.g. `60.17, 24.94`.

Under the midnight sun the colour stays neutral; in polar night it stays
warm all day. Beyond 60° north or south the times are less precise, and
fixed times give a steadier routine.

## If something goes wrong

The settings page shows a warning at the top when Owlfish is not running.

- **"Not supported on this device."** Your device's own configuration
  loads Qt plugins through the same setting as Owlfish
  (`QT_QPA_GENERIC_PLUGINS`). Owlfish gives way rather than switch off
  your device's plugins, so it does nothing. Please report it in the
  [issues](https://github.com/ccontino84/owlfish/issues) with your device's
  name.
- **"Not running. Restart the phone to start it."** It was installed or
  updated since the last restart. If it stays after a restart, a system
  update may have added a conflicting setting: reinstall Owlfish to check
  again.
- **"Turned off after the home screen failed to start."** The crash guard
  (see below) has turned Owlfish off. Tap **Reset** and restart the phone to
  try again.

**Touch, mouse or keyboard stop working after installing or a system
update.** Unlikely, as the installer checks for the conflict above: only an
update that later adds a device file read before Owlfish's can cause it.
To recover over SSH (USB or Wi-Fi, with Developer mode and a password set):

```sh
devel-su rm /var/lib/environment/compositor/00-owlfish.conf   # or: devel-su rpm -e owlfish
devel-su reboot
```

Without SSH, use your device's recovery mode, if it has one, to delete the
same file.

## Known issues

- **Wrong icon on a newly added top menu shortcut** (Sailfish OS 5.2.0.17):
  the top menu may show another switch's icon, e.g. *Do not disturb*.
  Removing and adding the shortcut again fixes it. The bug is in the
  system's top menu.

## Uninstall

Owlfish has no app icon, so it is removed from a terminal (Developer mode):

```sh
devel-su pkcon remove owlfish
systemctl --user restart lipstick
```

Settings stay in dconf; remove them with `dconf reset -f /apps/owlfish/`.

## Safety and privacy

Owlfish is a plugin that the home screen (lipstick) loads at startup. It
modifies no system file: it only adds its own, including an environment
file that tells lipstick to load it.

- **Crash guard:** each home screen start counts as unhealthy until the
  filter has run for 30 s. After 3 unhealthy starts in a row Owlfish stays
  off until reset on its settings page (or with
  `rm ~/.cache/owlfish/unhealthy-starts`).
- **Never too dark:** dimming stops at 75 %, so the screen stays readable.
- **Only in the home screen:** the plugin does nothing in any other process.
- **No cost when off:** with no tint and no dimming the filter is removed.
- **Privacy:** the light sensor is only read while "only in the dark" is in
  use, and by the settings page while it is open. Sunset and sunrise come
  from the time zone or the coordinates you type, never from location
  services. Nothing leaves the phone.

## Development

Building, testing, how it works inside: [DEVELOPMENT.md](DEVELOPMENT.md).

## License

GNU Lesser General Public License, version 2.1 only (LGPL-2.1-only, the same
as lipstick), see [LICENSE](LICENSE).
Copyright (c) 2026 [ccontino84](https://github.com/ccontino84).
