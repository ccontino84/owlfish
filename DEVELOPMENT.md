# Owlfish development

## How it works

Owlfish is a Qt generic plugin that the home screen compositor (lipstick)
loads at startup. It puts a full-screen colour filter on top of the
compositor scene: `out = pixel × gain` per channel, so black stays black.

| File | Purpose |
|---|---|
| `/usr/lib64/qt5/plugins/generic/libowlfish.so` | the plugin |
| `/var/lib/environment/compositor/00-owlfish.conf` | `QT_QPA_GENERIC_PLUGINS=owlfish`, read by `lipstick.service`; not shipped (`%ghost`) |
| `/usr/libexec/owlfish/update-env` | writes the file above from the RPM's `%post` (see below) |
| `/usr/share/jolla-settings/entries/owlfish.json` | registers the Settings page and the top menu shortcut |
| `/usr/share/jolla-settings/pages/owlfish/*.qml` | Settings page and top menu shortcut |
| `/usr/share/themes/sailfish-default/silica/z*/icons-monochrome/icon-m-owlfish.png` | icon, one per theme scale (source: `icons/icon-m-owlfish.svg`, render with `icons/render.sh`) |

- **Loading:** `lipstick.service` reads `EnvironmentFile=-/var/lib/environment/compositor/*.conf`.
  Qt 5.6 appends the `QT_QPA_GENERIC_PLUGINS` entries to lipstick's own
  `-plugin` arguments, so no file of another package is touched. The plugin
  waits for the `LipstickCompositor` window and parents a `ColorFilterItem`
  to its content item with a very high `z`.
- **Sharing the variable:** systemd reads those files sorted by name, and
  the last file that sets a variable wins. Some ports set
  `QT_QPA_GENERIC_PLUGINS` themselves (the PineTab2 loads its input
  plugins that way), so Owlfish must not replace their value. On every
  install and upgrade `config/update-env` computes the value the device
  sets without Owlfish, the same way (ignoring `00-owlfish.conf` and 1.1.0's
  `90-owlfish.conf`). Only if it is empty or already names `owlfish` does
  it write `00-owlfish.conf`; otherwise it removes it and prints a warning
  (hidden by `pkcon` and other PackageKit-based installers; `rpm` and
  `zypper` show it), and the installation still succeeds. The `00-` name is
  read first, so a device file added later wins: Owlfish becomes inactive
  instead of the device losing its plugins.
- **Status:** the plugin registers `io.github.ccontino84.owlfish` on the
  session bus, with `status()` (`active`, `starting`, `no-window`,
  `crash-guard`), `version()`, `resetCrashGuard()`, `renderer()` and
  `diagnostics()` (see below). The settings page
  asks once when it opens and shows the running version under its title.
  A double tap on its header opens a hidden page with `diagnostics()` and a
  Copy button, for support (without a reply, the page writes the status it
  found instead).
  Without a reply it reads `00-owlfish.conf`: missing means "not supported
  on this device", present means "restart the phone". A device file added
  after installation also looks like the latter; reinstalling runs the
  check again.
- **Filter:** one full-screen quad with a public `QSGMaterial`. Its shader's
  `activate()` sets `glBlendFunc(GL_ZERO, GL_SRC_COLOR)` (or, for `fetch`,
  `GL_ONE, GL_ZERO` with the destination alpha masked) and `deactivate()`
  restores the renderer's premultiplied blending. It does not use the private
  `QSGRenderNode`, which in Qt 5.6 turns off the renderer's depth-buffer
  optimisation for the whole compositor scene. With gain (1, 1, 1) the node
  is removed.
- **Renderer:** on verified devices the display hardware applies the gain
  instead (`pq`), at no GPU cost; everywhere else the item draws it
  (`fetch` while the saturation is below 100 %, `blend` otherwise). See
  below.
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
| `colorfilteritem.*`, `colorfiltermaterial.*` | the scene graph item and its material: multiply blend, or framebuffer fetch with the saturation |
| `pqdisplay.*` | the display hardware's colour matrix through MediaTek's PQ service |
| `colormatrix.*` | the saturation matrix, and the gain in linear light for the display hardware |
| `colortemperature.*` | kelvin → per-channel gain |
| `schedule.*` | the daily window and its transitions |
| `sun.*` | sunset and sunrise (NOAA's solar equations) |
| `timezonelocation.*` | the system time zone and its city's coordinates from tzdata |
| `ambientcutoff.*`, `alscalibration.*` | bright-light hysteresis and debounce; mce's `AlsValueMultiplier` |
| `settings.*` | dconf keys (mlite `MDConfItem`); environment variables in host builds |
| `crashguard.*` | the unhealthy-start counter |
| `statusservice.*` | the D-Bus status for the settings page and support |

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

The saturation (`ColorMatrix::saturation`) mixes each colour with the grey
of the same Rec. 709 luminance, in linear light. It follows the colour's
schedule like the warmth: at a strength `t` of the schedule the factor is
`1 − t · (1 − saturation)`. Desaturating comes first and the tint after, so
a bedtime screen is a warm grey:
`diag(linearGain(tint × dim)) · saturation`. Only the display hardware and
`fetch` can mix channels. Blending only multiplies each channel, so with
`blend` (forced, or where fetch doesn't work) the saturation is ignored,
the settings page greys it out, and `diagnostics()` says so.

### GPU (`fetch` and `blend`)

Where the display hardware isn't used, the item draws the filter:

- **`fetch`** while the saturation is below 100 %: the shader reads the
  pixel with `GL_ARM_shader_framebuffer_fetch` (`gl_LastFragColorARM`,
  Mali) or `GL_EXT_shader_framebuffer_fetch` (`gl_LastFragData[0]`),
  replaces blending, keeps the destination alpha (`glColorMask`), applies
  the matrix in approximately linear light with gamma 2.0 for the sRGB
  curves (`sqrt(matrix · dst²)`, mediump, no `pow()`), then the gain in
  encoded space, as `blend` does. Within about 1 ΔE00 of the exact linear
  result on average; on the Xperia 10 III it costs about 1 % of frames. The
  exact sRGB curves cost about 5 %, and the matrix on the encoded values
  turned desaturated pure colours much darker (pure red at 0 %: lightness
  23 instead of 53).
- **`blend`** for the gain alone: cheaper on Mali GPUs (about 80 against
  75 fps while swiping on the Jolla Phone).
- Whether the extension exists and the shader links is checked at the
  GPU's first frame (never while the display hardware is used). Until
  then `renderer()` says `fetch`; if it doesn't work, `blend` draws
  everything, the saturation is disabled and `diagnostics()` says why.

### Display hardware (`pq`)

On MediaTek devices the vendor's picture quality service
(`vendor.mediatek.hardware.pq_aidl.IPictureQuality_AIDL/default`, on
`/dev/binder`, which any user can open on the Jolla Phone) sets the display
controller's colour matrix. Owlfish sends `setColorMatrix3x3` with
`diag(linearGain(gain))`: the service works in linear light, so each encoded
gain goes through the sRGB decoding curve. The service's fixed point
follows the display controller's colour correction (CCORR) block, whose
coefficient width the kernel reads from the device tree (`ccorr-bit`): 12
bits means 1024 = 1.0, otherwise 2048 (MediaTek's kernel and the V7
service, as disassembled, both use this rule). With a wrong base even the
reset is wrong, so `pq` is only chosen automatically where the base is
known.

- **Selection** (`renderer` key `auto`): the device tree's `ccorr0` alias
  leads to a MediaTek CCORR node with the Jolla Phone's layout (13 bits,
  `ccorr-num-per-pipe` 1, `ccorr-linear` 1; world-readable files under
  `/sys/firmware/devicetree/base`, read once), or the `ID` in
  `/etc/hw-release` is on the verified list (`jp2601`, kept as an
  override); then `libgbinder.so.1` loads (with `dlopen`,
  so there is no package dependency), the service is there, and it answers
  `getInterfaceVersion` with 7 or more. Otherwise the GPU, and on other
  devices nothing is loaded or called. `pq` skips only the device list;
  `blend` and `fetch` skip everything.
- **Fades:** one call per animation step, skipped when the fixed-point
  matrix is the same as the last one. The item keeps the gain but is
  hidden.
- **Resets:** the service keeps the matrix until it is changed or the
  device restarts. Owlfish resets it (identity, and the separate RGB gain
  to 2048) when it starts using it (which clears what a crashed run left),
  when it stops (renderer key, failure), on `aboutToQuit` and in the
  controller's destructor. Switched off, the fade ends at identity. After a
  crash the screen stays tinted until the next start, which sends the
  whole state again. When the crash guard has tripped, Owlfish makes no
  calls at all, not even a reset: they could be what crashed; a reboot
  clears the hardware.
- **Failure:** if a call fails, Owlfish resets what it can and the item
  draws for the rest of the run.
- **Diagnostics:** the journal line `Renderer <name> - <reason>` at every
  change, and `renderer <name>` in the per-settings-change line.
  `diagnostics()` returns the version and status, the renderer and why, the
  key, the device ID, the device tree's colour correction block, and the
  service version with the call count and times (or why it is not
  available).
- **Screenshots** don't show the hardware's tint; with the GPU they do.

The research behind this (the service's calls, the phone tests) is outside
the repository.

### Schedule

The start and end may cross midnight; equal times mean the colour stays
neutral. The fade-in starts at the start time and the fade-out ends at the
end time; a gradual change longer than half the window is shortened to fit.
While Owlfish is enabled with a schedule, the plugin updates the colour every
30 s while the display is on, and at once when it turns on (mce
`display_status_ind` on the system bus).

The dimming has its own `dim_when`:
- `always`;
- `fixed`: `dim_from` to `dim_to`, an `OwlfishSchedule` without transition,
  switching with the 1 s fade of the 30 s tick;
- `night_light`: the colour's times (fixed, or sunset to sunrise; on all day
  in polar night) without its transitions, so it switches at the start and
  end however gradual the colour is; all the time when the colour has no
  schedule.

The light sensor's cut-off applies on top.

### Sunset to sunrise

`SunTimes` is our own implementation of NOAA's public-domain solar
equations (declination and equation of time, as in NOAA's solar calculator
spreadsheet), refined at each event's own time, with the sun's centre 0.833°
below the horizon. NOAA gives the accuracy as about 1 min between ±72°
latitude and 10 min beyond. The tests compare it with the US Naval
Observatory's values (normal days, DST changes, the southern hemisphere,
sunsets after midnight, and the first days of midnight sun and polar
night).

The window is today's sunset to today's sunrise, in local minutes, fed into
the same `OwlfishSchedule` as fixed times: the sunrise moves by minutes a
day, so today's stands in for tomorrow's. Under the midnight sun the window
is empty (neutral); in polar night the colour is on all day.

The location is the time zone's reference city unless `location_manual` is
set with valid coordinates:

- **Zone:** `/etc/localtime` is followed one link at a time
  (`/etc/localtime` → `/var/lib/timed/localtime` →
  `/usr/share/zoneinfo/Europe/Berlin` on Sailfish OS), stopping at the first
  target inside a `zoneinfo/` directory. Resolving the whole chain would be
  wrong where tzdata installs aliases as symlinks (Europe/Oslo →
  Europe/Berlin). Qt 5.6's `QTimeZone::systemTimeZoneId()` follows only one
  link, so it is only the fallback.
- **Coordinates:** `zone.tab` first (one line per country, e.g.
  Europe/Oslo), then `zone1970.tab`, whose lines merge countries. No API
  provides a time zone's coordinates.

The plugin re-reads the links on every schedule tick and recalculates when
the date, zone or location changes. It publishes the result under
`/apps/owlfish/` (`sun_place`, `auto_latitude`, `auto_longitude`,
`sun_state`, `sun_set`, `sun_rise`, `sun_latitude`), so the settings page
reads no files.

### Light sensor

`QLightSensor`, event driven (sensorfw delivers a reading only when the value
changes), only while Owlfish is enabled with dimming and "only in the dark"
set, and the dimming's `dim_when` applies now (checked on every schedule
tick). sensorfw sends nothing when a session starts, so steady bright light
would count as dark until it changes; right after starting the sensor the
plugin asks sensorfw for the value it already has, as mce does
(`local.ALSSensor.lux` on `com.nokia.SensorService`
`/SensorManager/alssensor`, system bus). That value applies at once, and so
does the first reading after it, as it may be stale. The settings page's meter
does the same, and ignores its `LightSensor.reading` until a reading has
arrived: in Qt 5.6 its `illuminance` is uninitialised until then.

Sensors have a ceiling: the Jolla Phone's reports at most 65535 raw
(`AlsValueMultiplier` 0.0333333, so about 2184 lux), which overcast
daylight already reaches; the Xperia 10 III's goes to 50 000 lux. At start
the plugin reads the maximum (`local.ALSSensor.getAvailableDataRanges`),
counts readings at it as bright whatever the threshold, and publishes it in
lux as `/apps/owlfish/als_max_lux`. The settings page then offers no
threshold above it; a threshold stored earlier stays as it is and works
like the top of the scale. Above the threshold the dimming fades out after 1.5 s; below 75 % of it,
it fades back in after 5 s. Moving the threshold applies at once. Some
devices report raw values instead of lux; mce corrects them with
`[Sensors] AlsValueMultiplier` in `/etc/mce/NN*.ini`, and the plugin applies
the same factor (logged at startup, and published as
`/apps/owlfish/als_multiplier` for the Settings page).

## Settings (dconf)

```sh
dconf write /apps/owlfish/enabled true           # default false
dconf write /apps/owlfish/temperature 3400       # kelvin, 1900-6500, default 4500
dconf write /apps/owlfish/saturation 40          # percent, 0 (grey)-100, default 100; display hardware or fetch
dconf write /apps/owlfish/dim 50                 # percent, 0-75, default 0
dconf write /apps/owlfish/dim_when "'fixed'"     # always (default), fixed or night_light
dconf write /apps/owlfish/dim_from 1320          # with fixed: minutes after midnight, default 1260
dconf write /apps/owlfish/dim_to 390             # default 420
dconf write /apps/owlfish/dim_cutoff false       # default true
dconf write /apps/owlfish/dim_cutoff_lux 500     # 100-50000, default 1000
dconf write /apps/owlfish/schedule true          # warm only at night, default false
dconf write /apps/owlfish/schedule_sun true      # sunset to sunrise instead of fixed times, default false
dconf write /apps/owlfish/schedule_from 1320     # minutes after midnight (22:00), default 1260
dconf write /apps/owlfish/schedule_to 390        # 06:30, default 420
dconf write /apps/owlfish/schedule_transition 30 # minutes, 0-120, default 60
dconf write /apps/owlfish/location_manual true   # sun at the coordinates below, default false
dconf write /apps/owlfish/latitude 60.17         # degrees, north positive
dconf write /apps/owlfish/longitude 24.94        # degrees, east positive
dconf write /apps/owlfish/renderer "'blend'"    # auto (default), blend, fetch (GPU) or pq; not on the settings page
dconf read /apps/owlfish/sun_state               # written by the plugin, e.g. 'normal'
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
after the filter, and `fetch` (pixels with the saturation through gamma 2.0,
close to linear light; blend for the gain alone; it needs the EXT
extension in the host's OpenGL, which Mesa's llvmpipe has, otherwise those
tests are skipped after checking the blend fallback; Mesa 25.2's llvmpipe
reads only zeros through it in desktop GL and fails them, Mesa 26 works,
so the release workflow tests in an Ubuntu 26.04 container). They also
cover the colour gains against redshift's table,
tint and dimming combined, the schedule (window, transitions, only the
colour), sunset and sunrise against USNO reference values, the time zone
lookup (links, aliases, zone.tab), the ambient light cut-off, the crash
guard, the D-Bus status, `update-env` against sample environment files,
the display hardware with a fake PQ service (fixed point, device list,
selection, fades, resets, handover, failure, crash guard; without
libgbinder on the host, `pq` falls back to the GPU), the saturation
(matrix, with the schedule, through the fake PQ service and fetch, ignored
with `blend`), the dimming's `dim_when` (always, fixed, with Night light's times
but not its gradual change),
and loading through `QGenericPluginFactory`, including a sun schedule
at fake polar coordinates (`OWLFISH_LOCATION_MANUAL=1 OWLFISH_LATITUDE=89.9`).

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
dbus-send --session --print-reply --dest=io.github.ccontino84.owlfish \
    /owlfish io.github.ccontino84.owlfish.status        # string "active"
dbus-send --session --print-reply --dest=io.github.ccontino84.owlfish \
    /owlfish io.github.ccontino84.owlfish.diagnostics   # also on the settings page: double-tap the header
```

The plugin logs a line whenever a setting changes (e.g. `enabled true
temperature 4500 K dim 0 % ... renderer pq`) and the day's sun times (e.g.
`Sun for "2026-09-28" in "Europe/Berlin" sunset 19:04 sunrise 07:10`).
Sailfish OS keeps the journal in RAM and only 1 MB of it
(`RuntimeMaxUse=1M`), so other messages (on the Jolla Phone, the kernel's)
can push Owlfish's lines out before you look. Follow it live instead, then
restart the home screen or change a setting:

```sh
devel-su journalctl -f -n 0 --no-pager _COMM=lipstick   # e.g. "Renderer pq - device jp2601 verified"
```

## Making a release

1. Raise `Version:` in `rpm/owlfish.spec` (local test builds carry a
   `~alphaN`, `~betaN` or `~rcN` suffix; the release has none), add the
   version's entry at the top of `CHANGELOG.md` (`## <version> (date)`, then
   one bullet per user-visible change), and push.
2. On GitHub: **Actions > Release > Run workflow**. It checks that
   `v<Version>` does not exist yet and that `CHANGELOG.md` has its entry,
   runs the tests, builds the aarch64 and armv7hl RPMs against the Sailfish
   OS 5.1.0.11 SDK targets, and creates the release `v<Version>` with both
   RPMs attached. The release text is the changelog entry
   (`ci/release-notes.sh <version>`), then GitHub's generated notes. By
   default the release is a draft: review it, then publish it on the
   Releases page.

The builds use the community Sailfish SDK images
([coderus/sailfishos-platform-sdk](https://hub.docker.com/r/coderus/sailfishos-platform-sdk))
through `ci/build-rpm.sh`, which also runs locally with Docker:

```sh
ci/build-rpm.sh aarch64      # or armv7hl; RPM in out/ (the image is about 3 GB)
```

The SDK's package check reports a missing `%changelog` and does not know
the SPDX name `LGPL-2.1-only`; neither affects installing the package.

SailfishOS:Chum builds from a tag: its OBS package points at `v<Version>`
through `tar_git`, which takes the version from the tag and leaves `rpm/`
out of the sources (hence `OWLFISH_VERSION` in the spec's `%build`). The
package in `sailfishos:chum:testing` is moved to the new tag by its
maintainer after each release.
