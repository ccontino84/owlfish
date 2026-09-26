# Owlfish development

## How it works

Owlfish is a Qt generic plugin that the home screen compositor (lipstick)
loads at startup. It puts a full-screen colour filter on top of the
compositor scene: `out = pixel × gain` per channel, so black stays black.

| File | Purpose |
|---|---|
| `/usr/lib64/qt5/plugins/generic/libowlfish.so` | the plugin |
| `/var/lib/environment/compositor/90-owlfish.conf` | `QT_QPA_GENERIC_PLUGINS=owlfish`, read by `lipstick.service` |
| `/usr/share/jolla-settings/entries/owlfish.json` | registers the Settings page and the top menu shortcut |
| `/usr/share/jolla-settings/pages/owlfish/*.qml` | Settings page and top menu shortcut |
| `/usr/share/themes/sailfish-default/silica/z*/icons-monochrome/icon-m-owlfish.png` | icon, one per theme scale (source: `icons/icon-m-owlfish.svg`, render with `icons/render.sh`) |

- **Loading:** `lipstick.service` reads `EnvironmentFile=-/var/lib/environment/compositor/*.conf`.
  Qt 5.6 appends the `QT_QPA_GENERIC_PLUGINS` entries to lipstick's own
  `-plugin` arguments, so no file of another package is touched. The plugin
  waits for the `LipstickCompositor` window and parents a `ColorFilterItem`
  to its content item with a very high `z`.
- **Filter:** one full-screen quad with a public `QSGMaterial`. Its shader's
  `activate()` sets `glBlendFunc(GL_ZERO, GL_SRC_COLOR)` and `deactivate()`
  restores the renderer's premultiplied blending. It does not use the private
  `QSGRenderNode`, which in Qt 5.6 turns off the renderer's depth-buffer
  optimisation for the whole compositor scene. With gain (1, 1, 1) the node
  is removed.
- **Scope:** the plugin only activates inside the `lipstick` executable, even
  if the environment variable leaks into child processes. It is built with
  hidden symbols (it exports only `qt_plugin_*`), so none of its symbols can
  interpose with lipstick's.
- **Crash guard:** `~/.cache/owlfish/unhealthy-starts` counts starts that
  did not reach 30 s with the filter attached; at 3 the plugin stays inactive.

Source layout (`src/`):

| File | What |
|---|---|
| `plugin.cpp` | `QGenericPlugin` entry, process/window options |
| `controller.*` | finds the window, combines settings, schedule and light sensor into a gain, fades |
| `colorfilteritem.*`, `colorfiltermaterial.*` | the scene graph item and the multiply-blend material |
| `colortemperature.*` | kelvin → per-channel gain |
| `schedule.*` | the daily window and its transitions |
| `ambientcutoff.*`, `alscalibration.*` | bright-light hysteresis and debounce; mce's `AlsValueMultiplier` |
| `settings.*` | dconf keys (mlite `MDConfItem`); environment variables in host builds |
| `crashguard.*` | the unhealthy-start counter |

### How the colour is computed

The gains follow the same construction as redshift's table: black-body
chromaticity (Kim et al. 2002 fit of the Planckian locus), blended into the
CIE daylight locus between 5000 and 6500 K so that 6500 K is D65, i.e. no
tint; then converted to linear sRGB, normalised so that red is 1, and
gamma-encoded, because the filter multiplies gamma-encoded framebuffer
values. This matches redshift's table to within 0.0015. Blue reaches 0 at
exactly 1900 K. Dimming multiplies all channels by the same factor; the
final gain is tint × dimming.

During a gradual change the colour moves in even steps of mireds between no
tint and the chosen warmth.

### Schedule

The start and end may cross midnight; equal times mean the colour stays
neutral. The fade-in starts at the start time and the fade-out ends at the
end time; a gradual change longer than half the window is shortened to fit.
While Owlfish is enabled with a schedule, the plugin updates the colour every
30 s while the display is on, and at once when it turns on (mce
`display_status_ind` on the system bus).

### Light sensor

`QLightSensor`, event driven (sensorfw delivers a reading only when the value
changes), only while Owlfish is enabled with dimming and "only in the dark"
set. Above the threshold the dimming fades out after 1.5 s; below 75 % of it,
it fades back in after 5 s. Moving the threshold applies at once. Some
devices report raw values instead of lux; mce corrects them with
`[Sensors] AlsValueMultiplier` in `/etc/mce/NN*.ini`, and the plugin applies
the same factor (logged at startup, and published as
`/apps/owlfish/als_multiplier` for the Settings page).

## Settings (dconf)

```sh
dconf write /apps/owlfish/enabled true           # default false
dconf write /apps/owlfish/temperature 3400       # kelvin, 1900-6500, default 4500
dconf write /apps/owlfish/dim 50                 # percent, 0-75, default 0
dconf write /apps/owlfish/dim_cutoff false       # default true
dconf write /apps/owlfish/dim_cutoff_lux 500     # 100-50000, default 1000
dconf write /apps/owlfish/schedule true          # warm only at night, default false
dconf write /apps/owlfish/schedule_from 1320     # minutes after midnight (22:00), default 1260
dconf write /apps/owlfish/schedule_to 390        # 06:30, default 420
dconf write /apps/owlfish/schedule_transition 30 # minutes, 0-120, default 60
dconf reset -f /apps/owlfish/                    # back to defaults
```

## Build

Use the Sailfish SDK with the 5.1.0.11 targets. They have the same Qt (5.6.3)
and mlite as 5.2.0.17.

```sh
sfdk config target=SailfishOS-5.1.0.11-aarch64   # see: sfdk tools list
sfdk build                                        # RPM lands in RPMS/
```

## Tests

Host build and tests (desktop Qt 5, needs Xvfb):

```sh
mkdir build && cd build && qmake ../owlfish.pro && make
QT_QPA_PLATFORM=xcb QSG_RENDER_LOOP=basic xvfb-run -a -s "-screen 0 640x480x24" ./tests/tst_owlfish
```

`QT_QPA_PLATFORM=xcb` matters in a Wayland session: otherwise the tests render
on the desktop (possibly scaled) instead of Xvfb, and the pixel checks fail.
In host builds the settings come from `OWLFISH_<KEY>` environment variables
(e.g. `OWLFISH_DIM=50`) instead of dconf.

The tests render real frames and check pixel values: uniform dim,
per-channel gain, black staying black, blend state restored for content drawn
after the filter. They also cover the colour gains against redshift's table,
tint and dimming combined, the schedule (window, transitions, only the
colour), the ambient light cut-off, the crash guard, and loading through
`QGenericPluginFactory`.

Plugin options, for testing outside lipstick:
`QT_QPA_GENERIC_PLUGINS=owlfish:class=<QQuickWindow subclass>:process=<exe or *>`.
The defaults are `class=LipstickCompositor:process=lipstick`.

## Try it on a phone

Enable Developer mode and check that SSH works (USB `192.168.2.15`, or Wi-Fi).

```sh
scp RPMS/owlfish-*.aarch64.rpm defaultuser@192.168.2.15:
ssh defaultuser@192.168.2.15
devel-su pkcon install-local owlfish-*.aarch64.rpm
systemctl --user restart lipstick          # closes open apps
```

Close the Settings app if it was open, so it picks up the new page. Check:

```sh
devel-su sh -c 'grep -l libowlfish /proc/[0-9]*/maps'   # must list only lipstick's pid
```

The plugin logs a line whenever a setting changes. Switch Owlfish in the top
menu, then:

```sh
devel-su journalctl -n 500 --no-pager | grep temperature   # e.g. "enabled true temperature 4500 K dim 0 % ..."
```

Sailfish OS keeps the journal in RAM and only 1 MB of it
(`RuntimeMaxUse=1M`), so lines from the home screen's start are soon gone.
Look for them within a minute or two of restarting it:

```sh
systemctl --user restart lipstick
devel-su journalctl -b --no-pager | grep -E "Filter attached|sensor multiplier|unhealthy"   # e.g. "Filter attached to LipstickCompositor"
```

## Making a release

1. Raise `Version:` in `rpm/owlfish.spec` and push.
2. On GitHub: **Actions > Release > Run workflow**. It checks that
   `v<Version>` does not exist yet, runs the tests, builds the aarch64 and
   armv7hl RPMs against the Sailfish OS 5.1.0.11 SDK targets, and creates the
   release `v<Version>` with both RPMs attached. By default the release is a
   draft: review it, then publish it on the Releases page.

The builds use the community Sailfish SDK images
([coderus/sailfishos-platform-sdk](https://hub.docker.com/r/coderus/sailfishos-platform-sdk))
through `ci/build-rpm.sh`, which also runs locally with Docker:

```sh
ci/build-rpm.sh aarch64      # or armv7hl; RPM in out/ (the image is about 3 GB)
```

The SDK's package check reports a missing `%changelog` and does not know
the SPDX name `LGPL-2.1-only`; neither affects installing the package.
