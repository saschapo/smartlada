#!/bin/zsh
# Double-click me: imports every new UltraLibrarian / SnapEDA package
# sitting in the watch folder into the shared KiCad Vendor library.
SCRIPT="/Users/saschapo/Documents/Claude/Projects/VAZ smart light/tools/kicad_import.py"
echo "KiCad import - watch folder"
echo
/usr/bin/python3 "$SCRIPT" --watch
echo
echo "Press any key to close."
read -k 1 -s
