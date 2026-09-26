# Owlfish

Easier on the eyes in the evening: Owlfish tints the Sailfish OS display
warmer and can make it darker than its lowest brightness, like Android's
*Night Light* and *Extra Dim* in one.

- **Warmth** from no tint (6500 K) down to no blue at all (1900 K), all day or
  only at night, with a gradual change at the start and end times.
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

With Developer mode, the same from a terminal:

```sh
devel-su pkcon install-local owlfish-*.rpm
systemctl --user restart lipstick     # restarts the home screen; closes open apps
```

## Using it

**Settings > Owlfish.** Every option is always shown in the same place;
options that do not apply are greyed out.

- **Enabled**: the master switch, also available as the top menu shortcut
  (tap to toggle, long press to open the page).
- **Colour temperature**
  - **Warmth**: 6500 K (no tint) to 1900 K (no blue), default 4500 K.
  - **Only at night**: warm only between the start and end times (default
    21:00-07:00), neutral the rest of the day. **Gradual change** (off,
    30 min, 1 h, 2 h) warms up from the start time and is back to neutral at
    the end time. A line below says what the colour is doing now.
- **Dimming**
  - **Darker than the current brightness**: off, or 5-75 %.
  - **Only in the dark**: above the chosen light level the dimming fades out,
    and it comes back below 75 % of that level. A meter on the same scale
    shows the light level now.

The schedule only affects the colour and the light sensor only the dimming,
so with both set you get: warm at night, darker in the dark.

## Known issues

- **Wrong icon on a newly added top menu shortcut** (seen on Sailfish OS
  5.2.0.17): right after adding "Owlfish" in Settings > Top menu, the top
  menu may show the shortcut with another switch's icon, e.g. *Do not
  disturb*. Removing and adding the shortcut again fixes it; restarting the
  phone probably does too. This happens in the system's top menu: Owlfish
  declares its shortcut the same way as the built-in ones.

## Uninstall

Owlfish has no app icon, so it is removed from a terminal (Developer mode):

```sh
devel-su pkcon remove owlfish
systemctl --user restart lipstick
```

Settings stay in dconf; remove them with `dconf reset -f /apps/owlfish/`.

**If the home screen does not come up** after installing, Owlfish turns
itself off after three failed starts (see Safety). To remove it by hand over
SSH:

```sh
devel-su rm /var/lib/environment/compositor/90-owlfish.conf
systemctl --user restart lipstick
```

## Safety and privacy

Owlfish is a plugin that the home screen (lipstick) loads at startup. It does
not modify any system file: it only adds its own files, including one
environment file that tells lipstick to load it.

- **Crash guard:** each home screen start counts as unhealthy until the
  filter has run for 30 s. After 3 unhealthy starts in a row Owlfish stays
  inactive. Re-enable it with `rm ~/.cache/owlfish/unhealthy-starts`.
- **Never too dark:** dimming stops at 75 %, so the screen stays readable
  enough to turn it off again.
- **Only in the home screen:** the plugin does nothing in any other process.
- **No cost when off:** with no tint and no dimming the filter is removed.
- **Privacy:** the light sensor is only read while dimming "only in the
  dark" is in use (and the settings page shows the current level while it
  is open). Nothing leaves the phone; there is no network access.

## Development

Building, testing, how it works inside: [DEVELOPMENT.md](DEVELOPMENT.md).

## License

GNU Lesser General Public License, version 2.1 only (LGPL-2.1-only, the same
as lipstick), see [LICENSE](LICENSE).
Copyright (c) 2026 [ccontino84](https://github.com/ccontino84).
