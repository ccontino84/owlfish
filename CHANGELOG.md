# Changelog

## 1.4.0 (2026-10-06)
- New Accessibility section with colour correction: Red-weak (protan), Green-weak (deutan) and Blue-weak (tritan) make colours that are hard to tell apart differ in brightness, with a Strength slider (for red- and green-weak, how strong the weakness is). Greyscale shows everything in shades of grey, all day. The correction works on the same devices as Saturation and applies all the time, also outside Night light's hours.
- Report a bug from the settings page's pull-down menu: it opens GitHub's bug report form in the browser with your Sailfish OS version and Owlfish's diagnostics filled in.
- Reset to defaults, also in the pull-down menu: every setting back to its default, including hidden ones.
- "Only in the dark" can now be set as low as 1 lux (was 100), for dimming only in a really dark room: new steps 1, 10 and 50 lux.

## 1.3.1 (2026-10-04)
- Saturation now works on other devices too, not only the Jolla Phone (tested on the Xperia 10 III). The graphics processor desaturates the screen while Saturation is below 100 %; at 100 % nothing changes. Devices whose graphics processor can't do it keep the slider greyed out.
- The display hardware is recognised by what it is, not by the phone's model name, so future devices with the same hardware as the Jolla Phone use it too.

## 1.3.0 (2026-10-03)
- Bedtime: a new Saturation slider in Night light fades the colours down to grey (0 %). It follows Night light's schedule and gradual change together with the warmth, so the screen turns a warm grey at night. Jolla Phone only: other devices can't change the saturation.
- Dimming can have its own schedule: all the time (as before), fixed times, or with Night light's times. It switches on and off at the start and end, without a gradual change. "Only in the dark" still applies on top.
- Tidier settings page: the sections are now "Night light" and "Dimming". Options appear only when the choice above them needs them; what your phone can't do is greyed out, with the reason.

## 1.2.1 (2026-10-01)
- No changes for users: packaging for SailfishOS:Chum.

## 1.2.0 (2026-10-01)
- Jolla Phone: the display hardware applies the filter instead of the graphics processor, so animations stay smooth (90 fps instead of about 80). Screenshots no longer include the tint.
- "Only in the dark" is more reliable: it no longer stays dimmed in bright light after being switched on, or after the home screen restarts.
- The light threshold slider ends at the most your phone's sensor can measure (about 2200 lux on the Jolla Phone).
- For bug reports: double-tap the title of the settings page, tap Copy and paste the text into the report.

## 1.1.1 (2026-09-30)
- Safer on devices that load their own Qt plugins the same way (for example the PineTab2): Owlfish stays inactive instead of switching off the device's plugins, and the installer prints a warning.
- The settings page shows the running version, and a warning when Owlfish isn't running: not supported on this device, restart needed, or turned off after the home screen failed to start (with a Reset button).

## 1.1.0 (2026-09-28)
- Night light can follow the sun: "When" offers all the time, fixed times, or sunset to sunrise.
- Sunset and sunrise are calculated on the phone for your time zone's city, with no location services or network, or for coordinates you type. Under the midnight sun the colour stays neutral; in polar night it stays warm all day.

## 1.0.0 (2026-09-26)
- First release: warm tint (6500 K to 1900 K) and extra dimming (up to 75 %), optionally only at night (fixed times) and only in the dark (light sensor), with one switch in Settings and the top menu.
