# kicad_import.py — "downloaded, installed, used"

Imports UltraLibrarian / SnapEDA KiCad downloads into one shared local library,
so a downloaded part shows up in KiCad without any manual library juggling.

## Where things go

| what | path |
|---|---|
| symbols | `~/Documents/KiCad/10.0/symbols/Vendor.kicad_sym` |
| footprints | `~/Documents/KiCad/10.0/footprints/Vendor.pretty/` |
| 3D models | `~/Documents/KiCad/10.0/3dmodels/Vendor.3dshapes/` |

All three are registered once in the KiCad **global** tables, so every project
sees them. 3D models are referenced through the `${VENDOR_3DMODELS}` environment
variable, which the script defines in `kicad_common.json`.

## Watch folder (the one-button flow)

Drop downloads (zip or unpacked folder) into:

```
~/Downloads/kicad_models_watchfolder/
```

Then double-click **`IMPORT.command`** inside that folder. It imports everything
that has not been imported yet and moves each package into `imported/`
(failures go to `failed/`) — that move is what marks a package as done, so
nothing gets imported twice and there is no hidden state file. Loose files that
are not KiCad packages (`notes.txt`, …) are ignored.

Same thing from the terminal:

```sh
python3 tools/kicad_import.py --watch
```

## Other usage

```sh
# one-time: create the library and register it (KiCad must be closed)
python3 tools/kicad_import.py --setup

# a single package, anywhere
python3 tools/kicad_import.py                    # newest package in ~/Downloads
python3 tools/kicad_import.py ~/Downloads/x.zip  # a specific zip or folder
python3 tools/kicad_import.py --list             # what is in the library
python3 tools/kicad_import.py --force <pkg>      # overwrite existing parts
```

Handy alias (`~/.zshrc`):

```sh
alias kimport='python3 "$HOME/Documents/Claude/Projects/VAZ smart light/tools/kicad_import.py"'
```

## What the import does

1. Unpacks the zip (or reads the folder) and finds `*.kicad_sym`, `*.kicad_mod`
   and `*.step/.stp/.wrl` anywhere in the tree — this covers both the
   UltraLibrarian layout (`KiCADv6/…`) and the flat SnapEDA layout.
2. Runs `kicad-cli sym/fp upgrade` so old-format vendor files (most of them are
   still v6, `version 20211014`) match the installed KiCad.
3. Copies footprints into `Vendor.pretty` and rewrites their `(model …)` paths
   to `${VENDOR_3DMODELS}/<file>`.
4. Copies 3D models into `Vendor.3dshapes`.
5. Merges symbols into `Vendor.kicad_sym` and rewrites each symbol's `Footprint`
   property to `Vendor:<footprint>` so placing the symbol pulls the right
   footprint automatically.
6. Skips parts that already exist unless `--force` is given.

## Limits worth knowing

- **KiCad running:** once the library is registered, importing with KiCad open
  is fine (only the preference files are at risk, and they are no longer
  touched) — just reload libraries or restart KiCad to see new parts. If the
  library is *not* yet registered, the script refuses to run while KiCad is
  open, because KiCad rewrites its library tables on exit and would undo the
  registration. `--allow-running` overrides the refusal.
- Watch-folder paths are overridable with `KICAD_WATCH_DIR=…`.
- Vendor files are trusted as-is: no DRC/pin-mapping validation happens. Check
  new parts against the datasheet before routing.
- If a vendor ships several footprint variants, the symbol keeps the one the
  vendor named in its `Footprint` property; the other variants are still in the
  library and can be picked manually.
- Version/paths are `10.0`-specific; override with `KICAD_VERSION=…` and the
  library name with `KICAD_VENDOR_LIB=…`.
