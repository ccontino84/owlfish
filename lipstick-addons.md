# lipstick add-ons: loading contract, version 1

Several independent packages can add Qt generic plugins to the Sailfish OS
home screen (lipstick). Qt starts generic plugins from one environment
variable, `QT_QPA_GENERIC_PLUGINS`, set in
`/var/lib/environment/compositor/*.conf`, and systemd lets the file that
sorts last win. If each package wrote its own list, the last one would hide
the others. This contract avoids that.

## The loader

- A tiny generic plugin with the key **`lipstickaddons`**. Each add-on
  package ships an identical copy, under its own file name:
  `liblipstickaddons-<package>.so` in the Qt generic plugin directory. Qt
  creates only the first library found for a key (by file name), so one copy
  runs whichever packages are installed.
- When created, it starts every installed generic plugin that declares a key
  **`lipstickaddon-<name>`**, sorted by key, through
  `QGenericPluginFactory::create()`, with the loader's own specification.
  Plugins without such a key (Qt's own input plugins, a port's) are never
  touched. Installing an add-on's library registers it; removing it
  unregisters it.
- Only in the `lipstick` process (`process=<name or *>` in the specification
  overrides that, for tests), and only once per process.
- The copies must behave the same, so the loader's source does not change
  within a contract version. A change of behaviour gets a new key.

## An add-on

- Declares `lipstickaddon-<name>` in its plugin metadata, next to its own
  key, which it may keep for direct loading.
- Creates itself **once per process**: the same add-on may be named by the
  environment variable and found by the loader. A second `create()` returns
  an empty `QObject` (not null, which makes Qt log "No such plugin").
- Does nothing outside the `lipstick` process.
- Ships the loader copy and writes its environment file so that its value
  is made only of add-on keys and `lipstickaddons`. Its install script
  leaves the file out (and warns) if the device's own files set the variable
  to anything else, rather than replacing the device's plugins.

## Transition

Add-ons that predate the contract load only by their own key (Owlfish up to
1.5: `00-owlfish.conf`, `owlfish`). Frogfish's file, `01-frogfish.conf`,
sorts after it and names both: `owlfish,lipstickaddons`. Once every such
add-on carries a `lipstickaddon-` key, the value can be just
`lipstickaddons`.
