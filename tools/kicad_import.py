#!/usr/bin/env python3
"""Import UltraLibrarian / SnapEDA KiCad downloads into one shared local library.

Usage:
    kicad_import.py --watch         # import everything new in the watch folder
    kicad_import.py                 # take the newest UL/SnapEDA download from ~/Downloads
    kicad_import.py <zip-or-dir>    # import a specific download
    kicad_import.py --setup         # only create the library + register it in KiCad
    kicad_import.py --list          # show what is already in the library

Watch folder: drop downloads in ~/Downloads/kicad_models_watchfolder and run
--watch (or double-click IMPORT.command inside it). Imported packages are moved
to the imported/ subfolder, which is what marks them as done.

Everything lands in a single symbol library, a single .pretty and a single
.3dshapes directory, all registered once in the KiCad global tables:

    ~/Documents/KiCad/<ver>/symbols/Vendor.kicad_sym
    ~/Documents/KiCad/<ver>/footprints/Vendor.pretty/
    ~/Documents/KiCad/<ver>/3dmodels/Vendor.3dshapes/

Close KiCad before running: KiCad rewrites its preferences on exit and would
overwrite the library-table changes made here.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

KICAD_VER = os.environ.get("KICAD_VERSION", "10.0")
LIB_NAME = os.environ.get("KICAD_VENDOR_LIB", "Vendor")
ENV_VAR_3D = "VENDOR_3DMODELS"

HOME = Path.home()
DOC_DIR = HOME / "Documents" / "KiCad" / KICAD_VER
PREF_DIR = HOME / "Library" / "Preferences" / "kicad" / KICAD_VER
CLI = Path("/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli")

SYM_FILE = DOC_DIR / "symbols" / f"{LIB_NAME}.kicad_sym"
FP_DIR = DOC_DIR / "footprints" / f"{LIB_NAME}.pretty"
MODEL_DIR = DOC_DIR / "3dmodels" / f"{LIB_NAME}.3dshapes"

WATCH_DIR = Path(os.environ.get(
    "KICAD_WATCH_DIR", str(HOME / "Downloads" / "kicad_models_watchfolder")))
DONE_DIR_NAME = "imported"
FAILED_DIR_NAME = "failed"

MODEL_EXT = {".step", ".stp", ".wrl", ".wings"}
EMPTY_LIB = '(kicad_symbol_lib (version 20211014) (generator kicad_symbol_editor)\n)\n'


def log(msg):
    print(msg, flush=True)


# --------------------------------------------------------------------------
# s-expression helpers (paren matching that respects quoted strings)
# --------------------------------------------------------------------------

def find_blocks(text, head):
    """Yield (start, end) of every top-level `(head ...)` block in text."""
    pat = re.compile(r"\(\s*" + re.escape(head) + r"[\s(]")
    pos = 0
    while True:
        m = pat.search(text, pos)
        if not m:
            return
        start = m.start()
        end = match_paren(text, start)
        yield start, end
        pos = end


def match_paren(text, start):
    """Return index just past the ')' closing the '(' at start."""
    depth = 0
    i = start
    in_str = False
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced parentheses")


def symbol_name(block):
    m = re.match(r'\(\s*symbol\s+"((?:[^"\\]|\\.)*)"', block)
    return m.group(1) if m else None


# --------------------------------------------------------------------------
# setup: create library files and register them with KiCad
# --------------------------------------------------------------------------

def ensure_library():
    SYM_FILE.parent.mkdir(parents=True, exist_ok=True)
    FP_DIR.mkdir(parents=True, exist_ok=True)
    MODEL_DIR.mkdir(parents=True, exist_ok=True)
    if not SYM_FILE.exists():
        SYM_FILE.write_text(EMPTY_LIB)
        log(f"created {SYM_FILE}")


def register_table(table_path, kind, uri):
    """Add our library to a global lib table if it is not there yet."""
    root = "sym_lib_table" if kind == "sym" else "fp_lib_table"
    if table_path.exists():
        text = table_path.read_text()
    else:
        text = f"({root}\n  (version 7)\n)\n"
    if re.search(r'\(name\s+"?%s"?\)' % re.escape(LIB_NAME), text):
        return False
    entry = ('\t(lib (name "%s") (type "KiCad") (uri "%s") (options "") '
             '(descr "UltraLibrarian / SnapEDA imports"))\n' % (LIB_NAME, uri))
    idx = text.rstrip().rfind(")")
    text = text[:idx] + entry + text[idx:]
    table_path.parent.mkdir(parents=True, exist_ok=True)
    table_path.write_text(text)
    log(f"registered {LIB_NAME} in {table_path.name}")
    return True


def register_env_var():
    cfg = PREF_DIR / "kicad_common.json"
    if not cfg.exists():
        log(f"warning: {cfg} not found, skipping {ENV_VAR_3D}")
        return
    data = json.loads(cfg.read_text())
    env = data.setdefault("environment", {})
    vars_ = env.get("vars") or {}
    if vars_.get(ENV_VAR_3D) == str(MODEL_DIR):
        return
    vars_[ENV_VAR_3D] = str(MODEL_DIR)
    env["vars"] = vars_
    cfg.write_text(json.dumps(data, indent=2))
    log(f"set ${ENV_VAR_3D} = {MODEL_DIR}")


def setup_needed():
    """True if setup() would still have to touch the KiCad preference files."""
    for name in ("sym-lib-table", "fp-lib-table"):
        p = PREF_DIR / name
        if not p.exists() or not re.search(r'\(name\s+"?%s"?\)' % re.escape(LIB_NAME),
                                           p.read_text()):
            return True
    cfg = PREF_DIR / "kicad_common.json"
    if cfg.exists():
        vars_ = (json.loads(cfg.read_text()).get("environment") or {}).get("vars") or {}
        if vars_.get(ENV_VAR_3D) != str(MODEL_DIR):
            return True
    return False


def setup():
    ensure_library()
    register_table(PREF_DIR / "sym-lib-table", "sym", str(SYM_FILE))
    register_table(PREF_DIR / "fp-lib-table", "fp", str(FP_DIR))
    register_env_var()


# --------------------------------------------------------------------------
# locating and unpacking a download
# --------------------------------------------------------------------------

def newest_download():
    dl = HOME / "Downloads"
    best = None
    for p in dl.iterdir():
        if p.name.startswith("."):
            continue
        if p.is_dir() or p.suffix.lower() == ".zip":
            if not looks_like_package(p):
                continue
            if best is None or p.stat().st_mtime > best.stat().st_mtime:
                best = p
    return best


def looks_like_package(path):
    """Cheap check that a zip/dir actually carries KiCad library files."""
    if path.is_dir():
        for p in path.rglob("*"):
            if p.suffix in (".kicad_sym", ".kicad_mod"):
                return True
        return False
    try:
        with zipfile.ZipFile(path) as z:
            return any(n.endswith((".kicad_sym", ".kicad_mod")) for n in z.namelist())
    except (zipfile.BadZipFile, OSError):
        return False


def unpack(src, workdir):
    if src.is_dir():
        return src
    dest = workdir / src.stem
    with zipfile.ZipFile(src) as z:
        z.extractall(dest)
    return dest


def collect(root):
    """Find symbol files, footprint files and 3D models anywhere in the tree.

    Prefers the newest KiCad export folder when a vendor ships several
    (UltraLibrarian puts KiCADv6/ next to older/other-EDA folders)."""
    syms = sorted(root.rglob("*.kicad_sym"))
    fps = sorted(root.rglob("*.kicad_mod"))
    models = sorted(p for p in root.rglob("*") if p.suffix.lower() in MODEL_EXT)
    return syms, fps, models


# --------------------------------------------------------------------------
# import steps
# --------------------------------------------------------------------------

def upgrade(paths, kind, workdir):
    """Run kicad-cli sym/fp upgrade so old-format files match KiCad 10."""
    if not CLI.exists():
        return paths
    out = []
    for p in paths:
        dest_dir = workdir / f"up_{kind}_{len(out)}"
        dest_dir.mkdir(parents=True, exist_ok=True)
        r = subprocess.run([str(CLI), kind, "upgrade", "-o", str(dest_dir), "--force", str(p)],
                           capture_output=True, text=True)
        if r.returncode == 0:
            produced = list(dest_dir.rglob(p.suffix and f"*{p.suffix}" or "*"))
            out.append(produced[0] if produced else p)
        else:
            log(f"  note: upgrade failed for {p.name}, using original")
            out.append(p)
    return out


def upgrade_in_place(kind, target):
    if not CLI.exists():
        return
    r = subprocess.run([str(CLI), kind, "upgrade", "--force", str(target)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        log(f"  note: could not normalize {target.name}: {r.stderr.strip()}")


def import_footprints(fp_paths, models_copied, force):
    """Copy footprints, rewriting their 3D model paths to the shared env var."""
    names = []
    for src in fp_paths:
        dest = FP_DIR / src.name
        if dest.exists() and not force:
            log(f"  footprint exists, skipped: {src.stem} (use --force to replace)")
            names.append(src.stem)
            continue
        text = src.read_text()
        text = rewrite_models(text, models_copied)
        dest.write_text(text)
        names.append(src.stem)
        log(f"  footprint: {src.stem}")
    return names


MODEL_RE = re.compile(r'(\(\s*model\s+)("?)([^")\s]+)\2')


def rewrite_models(text, name_map):
    """Repoint any (model ...) paths already present to our shared store."""
    lower = {k.lower(): v for k, v in name_map.items()}

    def sub(m):
        base = Path(m.group(3)).name
        real = lower.get(base.lower(), base)
        return f'{m.group(1)}"${{{ENV_VAR_3D}}}/{real}"'

    return MODEL_RE.sub(sub, text)


def clean_model_filename(path):
    """Drop SnapEDA export noise ("<part>--3DModel-STEP-<id>") from a model name."""
    stem = re.split(r'--?\s*3d[\s_-]*model', path.stem, flags=re.I)[0].strip("-_ ")
    return (stem or path.stem) + path.suffix.lower()


def import_models(model_paths, force):
    """Copy models into the shared store; return {original_name: stored_name}."""
    name_map = {}
    for src in model_paths:
        stored = clean_model_filename(src)
        dest = MODEL_DIR / stored
        if dest.exists() and not force:
            log(f"  3D model exists, skipped: {stored}")
        else:
            shutil.copy2(src, dest)
            log(f"  3D model: {stored}")
        name_map[src.name] = stored
    return name_map


# --------------------------------------------------------------------------
# binding 3D models to footprints by name
# --------------------------------------------------------------------------

# Common UltraLibrarian / SnapEDA footprint name prefixes (a functional group
# tag the part number is appended to, e.g. XCVR_ESP32-C6-WROOM-1-N8).
FP_PREFIXES = ("XCVR", "OST", "SW4", "SW", "CAP", "IND", "RES", "LED",
               "CONN", "DIO", "REL", "FID", "MOD", "CRY", "OSC", "FB")
MATCH_THRESHOLD = 0.6


def _norm(s):
    s = re.sub(r'--?\s*3d[\s_-]*model.*$', '', s, flags=re.I)
    s = s.lower()
    for junk in ("3dmodel", "model", "step", "stp", "wrl"):
        s = s.replace(junk, "")
    return re.sub(r'[^a-z0-9]', '', s)


def _fp_core(name):
    for p in FP_PREFIXES:
        if name.upper().startswith(p + "_"):
            return _norm(name[len(p) + 1:])
    return _norm(name)


def match_score(model_stem, fp_name):
    """0..1 similarity between a model file name and a footprint name."""
    from difflib import SequenceMatcher
    a, b = _norm(model_stem), _fp_core(fp_name)
    if not a or not b:
        return 0.0
    lcs = SequenceMatcher(None, a, b).find_longest_match(0, len(a), 0, len(b)).size
    if lcs < 5:                       # too short to trust (avoid coincidental hits)
        return 0.0
    return lcs / min(len(a), len(b))


def has_model_block(text):
    return MODEL_RE.search(text) is not None


def add_model_block(text, stored_name):
    """Insert a (model ...) block referencing our store before the final ')'."""
    block = (f'  (model "${{{ENV_VAR_3D}}}/{stored_name}"\n'
             f'    (offset (xyz 0 0 0))\n'
             f'    (scale (xyz 1 1 1))\n'
             f'    (rotate (xyz 0 0 0))\n'
             f'  )\n')
    idx = text.rstrip().rfind(")")
    return text[:idx] + block + text[idx:]


def relink_models(force=False):
    """Attach every stored 3D model to the best-matching footprint that lacks one.

    Idempotent: footprints that already carry a (model ...) block are left alone
    unless force is set."""
    if not MODEL_DIR.exists():
        return
    models = [p.name for p in sorted(MODEL_DIR.iterdir())
              if p.suffix.lower() in MODEL_EXT]
    if not models:
        return
    bound = 0
    for fp in sorted(FP_DIR.glob("*.kicad_mod")):
        text = fp.read_text()
        if has_model_block(text) and not force:
            continue
        best = max(models, key=lambda m: match_score(Path(m).stem, fp.stem))
        s = match_score(Path(best).stem, fp.stem)
        if s < MATCH_THRESHOLD:
            continue
        if has_model_block(text):     # force: repoint the existing block instead
            text = rewrite_models(text, {best: best})
            text = MODEL_RE.sub(
                lambda m, b=best: f'{m.group(1)}"${{{ENV_VAR_3D}}}/{b}"', text, count=1)
        else:
            text = add_model_block(text, best)
        fp.write_text(text)
        bound += 1
        log(f"  bound 3D model: {fp.stem} <- {best}  (match {s:.2f})")
    if bound:
        log(f"attached {bound} 3D model(s) to footprints")
    return bound


def import_loose_models(model_paths, force):
    """Copy standalone .step files dropped in the watch folder into the store."""
    for src in model_paths:
        stored = clean_model_filename(src)
        dest = MODEL_DIR / stored
        if dest.exists() and not force:
            log(f"  3D model exists, skipped: {stored}")
        else:
            shutil.copy2(src, dest)
            log(f"  3D model: {stored}")


def import_symbols(sym_paths, fp_names, force):
    lib = SYM_FILE.read_text()
    existing = {symbol_name(lib[a:b]) for a, b in find_blocks(lib, "symbol")}
    added = []
    for src in sym_paths:
        text = src.read_text()
        for a, b in list(find_blocks(text, "symbol")):
            block = text[a:b]
            name = symbol_name(block)
            if name is None:
                continue
            block = fix_footprint_property(block, fp_names)
            if name in existing:
                if not force:
                    log(f"  symbol exists, skipped: {name} (use --force to replace)")
                    continue
                lib = replace_symbol(lib, name, block)
                log(f"  symbol replaced: {name}")
            else:
                lib = insert_symbol(lib, block)
                existing.add(name)
                log(f"  symbol: {name}")
            added.append(name)
    SYM_FILE.write_text(lib)
    return added


FP_PROP_RE = re.compile(r'(\(\s*property\s+"Footprint"\s+)"((?:[^"\\]|\\.)*)"')


def fix_footprint_property(block, fp_names):
    """Point the symbol's Footprint property at our library."""
    def sub(m):
        val = m.group(2)
        if ":" in val:
            val = val.split(":", 1)[1]
        if not val and len(fp_names) == 1:
            val = fp_names[0]
        if not val:
            return m.group(0)
        return f'{m.group(1)}"{LIB_NAME}:{val}"'

    return FP_PROP_RE.sub(sub, block, count=1)


def insert_symbol(lib, block):
    idx = lib.rstrip().rfind(")")
    return lib[:idx] + block.rstrip() + "\n" + lib[idx:]


def replace_symbol(lib, name, block):
    for a, b in find_blocks(lib, "symbol"):
        if symbol_name(lib[a:b]) == name:
            return lib[:a] + block.rstrip() + lib[b:]
    return insert_symbol(lib, block)


# --------------------------------------------------------------------------

def do_import(src, force, run_setup=True):
    if run_setup:
        setup()
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        root = unpack(src, work)
        syms, fps, models = collect(root)
        if not syms and not fps:
            log(f"no KiCad library files found in {src}")
            return 1
        log(f"importing {src.name}: {len(syms)} symbol file(s), "
            f"{len(fps)} footprint(s), {len(models)} 3D model(s)")

        syms = upgrade(syms, "sym", work)

        name_map = import_models(models, force)
        fp_names = import_footprints(fps, name_map, force)
        import_symbols(syms, fp_names, force)

    # bind shipped models to footprints that arrive without a (model ...) block
    relink_models(force)

    # normalize both libraries to the current KiCad format
    upgrade_in_place("fp", FP_DIR)
    upgrade_in_place("sym", SYM_FILE)

    log("done. Restart KiCad (or reload libraries) to see the new parts.")
    return 0


def watch_candidates():
    """New items in the watch folder: (packages, loose 3D model files)."""
    pkgs, loose = [], []
    for p in sorted(WATCH_DIR.iterdir()):
        if p.name.startswith(".") or p.name in (DONE_DIR_NAME, FAILED_DIR_NAME):
            continue
        if p.suffix.lower() in (".crdownload", ".part", ".download"):
            continue  # still downloading
        if p.is_dir() or p.suffix.lower() == ".zip":
            pkgs.append(p)
        elif p.suffix.lower() in MODEL_EXT:
            loose.append(p)
    return pkgs, loose


def move_aside(pkg, sub):
    dest_dir = WATCH_DIR / sub
    dest_dir.mkdir(exist_ok=True)
    dest = dest_dir / pkg.name
    n = 1
    while dest.exists():
        dest = dest_dir / f"{pkg.stem}_{n}{pkg.suffix}"
        n += 1
    shutil.move(str(pkg), str(dest))
    return dest


def do_watch(force):
    WATCH_DIR.mkdir(parents=True, exist_ok=True)
    setup()
    pkgs, loose = watch_candidates()
    if not pkgs and not loose:
        log(f"nothing new in {WATCH_DIR}")
        return 0

    ok = failed = 0
    for pkg in pkgs:
        log("")
        if not looks_like_package(pkg):
            log(f"skipping {pkg.name}: no KiCad library files inside")
            move_aside(pkg, FAILED_DIR_NAME)
            failed += 1
            continue
        try:
            rc = do_import(pkg, force, run_setup=False)
        except Exception as exc:                      # a bad package must not stop the batch
            log(f"error importing {pkg.name}: {exc}")
            rc = 1
        if rc == 0:
            move_aside(pkg, DONE_DIR_NAME)
            ok += 1
        else:
            move_aside(pkg, FAILED_DIR_NAME)
            failed += 1

    if loose:
        log("")
        log(f"importing {len(loose)} loose 3D model file(s)")
        import_loose_models(loose, force)
        for m in loose:
            move_aside(m, DONE_DIR_NAME)
        ok += len(loose)

    # bind any unattached models to matching footprints (covers loose files and
    # earlier imports whose vendor footprint shipped without a model block)
    log("")
    relink_models(force)
    upgrade_in_place("fp", FP_DIR)

    log("")
    log(f"watch folder: {ok} item(s) imported, {failed} failed")
    log(f"imported items moved to {WATCH_DIR / DONE_DIR_NAME}")
    return 1 if failed else 0


def do_list():
    if not SYM_FILE.exists():
        log("library not created yet, run with --setup")
        return 1
    lib = SYM_FILE.read_text()
    names = sorted(n for n in (symbol_name(lib[a:b]) for a, b in find_blocks(lib, "symbol")) if n)
    log(f"{LIB_NAME}.kicad_sym: {len(names)} symbol(s)")
    for n in names:
        log(f"  {n}")
    fps = sorted(p.stem for p in FP_DIR.glob("*.kicad_mod")) if FP_DIR.exists() else []
    log(f"{LIB_NAME}.pretty: {len(fps)} footprint(s)")
    for n in fps:
        log(f"  {n}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", nargs="?", help="zip or folder from UltraLibrarian / SnapEDA")
    ap.add_argument("--watch", action="store_true",
                    help=f"import every new package in {WATCH_DIR}")
    ap.add_argument("--setup", action="store_true", help="only create and register the library")
    ap.add_argument("--list", action="store_true", help="list library contents")
    ap.add_argument("--relink", action="store_true",
                    help="(re)attach stored 3D models to matching footprints")
    ap.add_argument("--force", action="store_true", help="overwrite parts that already exist")
    ap.add_argument("--allow-running", action="store_true",
                    help="import even if KiCad is open (library tables may be overwritten)")
    args = ap.parse_args()

    if args.list:
        return do_list()

    kicad_running = subprocess.run(["pgrep", "-x", "kicad"], capture_output=True).returncode == 0
    if kicad_running and not args.allow_running:
        # Only the preference files are at risk; adding parts to an already
        # registered library while KiCad runs is safe.
        if setup_needed():
            log("KiCad is running and the library is not registered yet: KiCad rewrites its")
            log("preferences on exit and would undo the change. Quit KiCad and run again.")
            return 1
        log("note: KiCad is running - use Preferences > Manage Symbol Libraries or restart")
        log("      it to see the newly imported parts.")

    if args.setup:
        setup()
        return 0

    if args.relink:
        setup()
        n = relink_models(args.force)
        if not n:
            log("no footprints needed a 3D model attached")
        upgrade_in_place("fp", FP_DIR)
        return 0

    if args.watch:
        return do_watch(args.force)

    src = Path(args.source).expanduser() if args.source else newest_download()
    if src is None:
        log("no UltraLibrarian / SnapEDA package found in ~/Downloads")
        return 1
    if not src.exists():
        log(f"not found: {src}")
        return 1
    return do_import(src, args.force)


if __name__ == "__main__":
    sys.exit(main())
