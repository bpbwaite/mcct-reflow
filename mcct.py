"""Build, test and flash the MCCT hot plate firmware (MS51FB9AE, SDCC).

    uv run mcct.py build                    generate tables, build -> mcct-firmware/build/mcct.{hex,bin}
    uv run mcct.py build --sim              bench build: boots into the simulated hot plate (no heating)
    uv run mcct.py build --cal              calibration firmware (mcct-cal)
    uv run mcct.py test [--cal]             unit tests in the 8051 simulator
    uv run mcct.py flash [--sim | --cal]    build, then write firmware + CONFIG via the Arduino bridge
    uv run mcct.py flash --bin FILE         write a prebuilt binary + CONFIG (no SDCC needed)
    uv run mcct.py status                   read the chip ID and CONFIG
    uv run mcct.py monitor                  serial console through the bridge (Ctrl+] to quit)
    uv run mcct.py bridge [--probe]         compile + upload the bridge (or icp_probe) to the Uno
    uv run mcct.py isp ARGS...              run nuvoispy directly

Commands that use the serial port find a single connected Arduino Uno by its
USB ID; otherwise pass --port (COM5, /dev/ttyACM0, ...).
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parent
TOOLS = REPO / "tools"
ROOT = REPO / "mcct-firmware"
SRC, TEST, CONFIG, SCRIPTS = ROOT / "src", ROOT / "test", ROOT / "config", ROOT / "scripts"
OUT = ROOT / "build"
GEN = OUT / "generated"
USER_CONFIG = CONFIG / "user_config.h"
CHIP_CONFIG = CONFIG / "chip_config.json"
# Nuvoton MS51 BSP (submodule, unmodified): only its headers (SFR names) are
# used; the firmware has its own drivers.
BSP = TOOLS / "MS51_BSP" / "MS51FB9AE_MS51XB9AE_MS51XB9BE" / "Library"
PROG = TOOLS / "NuMicro-8051-prog"
PROG_PATCH = TOOLS / "nuvo51icp-local.patch"

WINDOWS = os.name == "nt"
SDCC_VERSION = "4.6.0"
# The bridge's USB side always runs at 115200; it converts to the target's
# console baud (CFG_CONSOLE_BAUD) itself.
BRIDGE_BAUD = 115200
UNO_FQBN = "arduino:avr:uno"
UNO_USB_IDS = {(0x2341, 0x0043), (0x2341, 0x0001), (0x2341, 0x0243), (0x2341, 0x006A), (0x2A03, 0x0043)}

# MS51FB9AE memory map (no LDROM -> 16 KB APROM). The last two 128-byte
# pages hold the settings (src/storage/settings.c), so code stops below them.
SETTINGS_BASE = 0x3F00
CODE_SIZE = SETTINGS_BASE
IRAM_SIZE = 256
XRAM_SIZE = 1024
# Small model: globals default to fast internal RAM; bulky state is placed
# in XRAM explicitly with __xdata.
MODEL = "--model-small"

# Modules that touch hardware registers. Everything else is pure logic and is
# linked into the simulator unit tests (test/fakes.c stands in for these).
HARDWARE_SOURCES = ("main.c", "cal/cal_main.c", "board/", "display/display.c", "sensor/adc.c",
                    "storage/flash.c", "debug/console.c", "debug/cli.c")
# The calibration firmware has its own main() plus only the drivers it uses.
CAL_SOURCES = ("cal/", "common/", "board/", "display/", "input/", "sensor/",
               "control/heater.c", "debug/console.c")


class Error(Exception):
    pass


def run(cmd, what=None, **kw):
    sys.stdout.flush()
    code = subprocess.run([str(c) for c in cmd], **kw).returncode
    if code:
        raise Error(f"{what or pathlib.Path(cmd[0]).name} failed (exit {code})")


def user_config_int(name):
    m = re.search(rf"^#define\s+{name}\s+(\d+)", USER_CONFIG.read_text(encoding="utf-8"), re.M)
    if not m:
        raise Error(f"{name} not found in {USER_CONFIG}")
    return int(m.group(1))


# ---------------------------------------------------------------- SDCC

def sdcc_tool(*names):
    """An SDCC program from tools/sdcc, else from PATH."""
    local = TOOLS / "sdcc" / "bin"
    for name in names:
        found = shutil.which(name, path=str(local)) or shutil.which(name)
        if found:
            return found
    raise Error(f"{names[0]} not found: install SDCC {SDCC_VERSION} to tools/sdcc or on PATH")


def setup_sdcc():
    local = TOOLS / "sdcc" / "bin"
    if local.is_dir():
        # sdcpp finds cc1 via PATH (this process and its children only).
        os.environ["PATH"] = str(local) + os.pathsep + os.environ["PATH"]
        if WINDOWS and (local / "cc1").is_file() and not (local / "cc1.exe").exists():
            raise Error(f"rename {local / 'cc1'} to cc1.exe")
    version = subprocess.run([sdcc_tool("sdcc"), "--version"], capture_output=True, text=True).stdout
    if not re.search(rf"\b{re.escape(SDCC_VERSION)}\b", version):
        found = re.search(r"\b\d+\.\d+\.\d+\b", version)
        print(f"WARNING: SDCC {found.group(0) if found else '(unknown version)'} found, "
              f"the firmware is developed with {SDCC_VERSION}")
    if not BSP.is_dir():
        raise Error(f"MS51 BSP not found at {BSP} (run: git submodule update --init)")


def cflags(sim):
    flags = ["-mmcs51", MODEL, "--std-c11", "--opt-code-size",
             # Nuvoton headers select their SDCC variant with __SDCC__, which
             # SDCC itself doesn't define (it uses __SDCC).
             "-D__SDCC__", f"-DSETTINGS_FLASH_BASE=0x{SETTINGS_BASE:04X}"]
    for d in (SRC, CONFIG, GEN, BSP / "Device" / "Include", BSP / "StdDriver" / "inc"):
        flags.append(f"-I{d}")
    if sim:
        flags.append("-DCFG_PLANT_SIM=2")       # simulated hot plate from power-up
    return flags


def generate():
    """Calibration polynomial and reflow profile tables."""
    GEN.mkdir(parents=True, exist_ok=True)
    py = [sys.executable, "-I"]
    run(py + [SCRIPTS / "gen_calibration.py", ROOT / "calibration" / "adc_to_temperature.csv",
              GEN / "calibration_gen.h"], "code generation")
    run(py + [SCRIPTS / "gen_profiles.py", ROOT / "profiles", USER_CONFIG, SRC / "display" / "font.c",
              GEN / "profiles_gen.c"], "code generation")


def matches(path, patterns):
    rel = path.relative_to(SRC).as_posix()
    return any(rel.startswith(p) if p.endswith("/") else rel == p for p in patterns)


def c_files(top):
    """All .c files in a fixed order: the link order decides the image layout."""
    found = []
    for folder, dirs, files in os.walk(top):
        dirs.sort(key=str.upper)
        found += [pathlib.Path(folder) / f for f in sorted(files, key=str.upper) if f.endswith(".c")]
    return found


def sources(cal):
    if cal:
        return [c for c in c_files(SRC) if matches(c, CAL_SOURCES)]
    return [c for c in c_files(SRC) if not matches(c, ("cal/",))] + [GEN / "profiles_gen.c"]


def build_objects(files, obj_dir, flags):
    """Compile what is out of date. Any changed header, this script, or
    different flags rebuild everything (a full build takes seconds)."""
    obj_dir.mkdir(parents=True, exist_ok=True)
    stamp, text = obj_dir / "cflags.txt", " ".join(flags)
    if not stamp.exists() or stamp.read_text(encoding="utf-8") != text:
        for old in obj_dir.glob("*.rel"):
            old.unlink()
        stamp.write_text(text, encoding="utf-8")
    headers = [h for d in (SRC, CONFIG, GEN, TEST) for h in d.rglob("*.h")] + [pathlib.Path(__file__)]
    newest = max(h.stat().st_mtime for h in headers)
    rels = []
    for c in files:
        rel = obj_dir / (c.relative_to(ROOT).as_posix().replace("/", "_")[:-2] + ".rel")
        if not rel.exists() or rel.stat().st_mtime < max(newest, c.stat().st_mtime):
            print(f"CC  {c.relative_to(ROOT)}")
            run([sdcc_tool("sdcc"), *flags, "-c", c, "-o", rel])
        rels.append(rel)
    return rels


def first(rels, name):
    """The file containing main() must be linked first."""
    return sorted(rels, key=lambda r: r.name != name)


def link(rels, ihx, code, iram, xram):
    print(f"LD  {ihx.name}")
    ihx.unlink(missing_ok=True)
    run([sdcc_tool("sdcc"), "-mmcs51", MODEL, "--code-size", code, "--iram-size", iram,
         "--xram-size", xram, "--xram-loc", 0, "-o", ihx, *rels])


def build(sim=False, cal=False, clean=False):
    """Build the firmware; returns the path of the .bin."""
    setup_sdcc()
    if clean and OUT.exists():
        shutil.rmtree(OUT)
    generate()
    target = "mcct-cal" if cal else "mcct"
    obj = OUT / ("cal" if cal else "obj")
    rels = build_objects(sources(cal), obj, cflags(sim))
    ihx, hexfile, binfile = obj / f"{target}.ihx", OUT / f"{target}.hex", OUT / f"{target}.bin"
    for old in (hexfile, binfile):
        old.unlink(missing_ok=True)     # a failed build must not leave an older image to flash
    link(first(rels, "src_cal_cal_main.rel" if cal else "src_main.rel"), ihx, CODE_SIZE, IRAM_SIZE, XRAM_SIZE)

    # SDCC copies initialised __xdata variables at startup with MOVX @R0 paged
    # by P2, which the MS51 does not do: such variables would silently start as
    # garbage. Refuse to build if any exist (initialise them in code instead).
    m = re.search(r"^\s*C:\s+([0-9A-F]+)\s+l_XINIT\s*$", (obj / f"{target}.map").read_text(errors="replace"), re.M)
    if m and int(m.group(1), 16):
        raise Error(f"initialised __xdata variables found (l_XINIT = 0x{m.group(1)}): "
                    "initialise them in code (see AGENTS.md)")

    with open(hexfile, "wb") as f:
        run([sdcc_tool("packihx"), ihx], stdout=f, stderr=subprocess.DEVNULL)
    run([sdcc_tool("makebin"), "-p", ihx, binfile])     # -p: trim to last used byte

    used = binfile.stat().st_size
    if not 0 < used <= CODE_SIZE:
        raise Error(f"{binfile.name} is {used} bytes; code must fit in {CODE_SIZE}")
    print(f"OK  {binfile.name}  {used} / {CODE_SIZE} bytes code ({used / CODE_SIZE:.1%}); "
          f"settings at 0x{SETTINGS_BASE:04X}")
    mem = obj / f"{target}.mem"
    if mem.exists():
        for line in mem.read_text(errors="replace").splitlines():
            if re.search("Stack starts|Other memory|ROM/EPROM|EXTERNAL RAM", line):
                print(f"    {line.strip()}")
    return binfile


def run_tests(cal):
    setup_sdcc()
    generate()
    defs = ["-DUNIT_TEST", "-DCFG_PLANT_SIM=1"]         # the closed-loop tests need the plant
    obj = OUT / "test"
    if cal:     # own small image: the full one has no internal RAM left
        obj = obj / "cal"
        defs.append("-DTEST_CAL")
        files = [SRC / "cal" / "cal_hold.c", TEST / "test_cal_hold.c", TEST / "test_main.c"]
    else:
        files = [c for c in sources(False) if c.parent == GEN or not matches(c, HARDWARE_SOURCES)]
        files += [c for c in c_files(TEST) if c.name != "test_cal_hold.c"]
    rels = build_objects(files, obj, cflags(False) + defs)
    ihx = obj / "tests.ihx"
    link(first(rels, "test_test_main.rel"), ihx, 65536, 256, 0xF000)

    # 8052 core; the program prints and stops through ucsim's simulator
    # interface at XRAM 0xFFFF (see test/test_main.c). On Windows ucsim runs ~8x
    # slower attached to our console, so it gets a hidden one of its own.
    print("RUN tests (ucsim, about a minute)")
    kw = {}
    if WINDOWS:
        kw["creationflags"] = subprocess.CREATE_NEW_CONSOLE
        kw["startupinfo"] = subprocess.STARTUPINFO(dwFlags=subprocess.STARTF_USESHOWWINDOW,
                                                   wShowWindow=subprocess.SW_HIDE)
    start = time.time()
    log = obj / "tests.log"
    with open(log, "wb") as out, open(obj / "tests.err", "wb") as err:
        try:
            subprocess.run([sdcc_tool("ucsim_51", "s51"), "-q", "-t", "8052", "-I", "if=xram[0xffff]",
                            "-G", ihx.name], cwd=obj, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                           timeout=15 * 60, **kw)
        except subprocess.TimeoutExpired:
            raise Error("unit tests timed out")
    lines = log.read_text(errors="replace").splitlines()
    for line in lines:
        if line.strip() and not re.match("uCsim|This is free|under certain", line):
            print(line)
    print(f"    ({time.time() - start:.0f} s in the simulator)")
    if not any(line.startswith("ALL TESTS PASSED") for line in lines):
        raise Error("unit tests failed")


# ---------------------------------------------------------- bridge / ISP

def find_port(port):
    if port:
        return port
    from serial.tools import list_ports
    ports = list(list_ports.comports())
    unos = [p.device for p in ports if (p.vid, p.pid) in UNO_USB_IDS]
    if len(unos) == 1:
        print(f"Arduino Uno on {unos[0]}")
        return unos[0]
    why = f"{len(unos)} Arduino Unos found" if unos else "no Arduino Uno found (clones have other USB IDs)"
    names = ", ".join(p.device for p in ports) or "none"
    raise Error(f"{why}: pass --port (serial ports: {names})")


def setup_programmer():
    """Make sure the programmer submodule is there and has the local patch."""
    if not (PROG / "nuvo51icp").is_dir():
        raise Error("NuMicro-8051-prog not found (run: git submodule update --init)")

    def git_apply(*opts):
        return subprocess.run(["git", "apply", *opts, str(PROG_PATCH)], cwd=PROG,
                              capture_output=True, text=True)
    # cwd must be the submodule's own repository, or git would silently skip
    # every file of the patch.
    if not (PROG / ".git").exists():
        raise Error(f"{PROG} is not a git submodule checkout (run: git submodule update --init)")
    if git_apply("--check", "--reverse").returncode == 0:
        return
    if git_apply("--check").returncode == 0 and git_apply().returncode == 0:
        print(f"Applied {PROG_PATCH.name} to {PROG.name}")
        return
    raise Error(f"{PROG.name} has neither its original nor its patched contents. If its changes are not "
                f"yours to keep, reset it (git -C tools/{PROG.name} checkout .) and try again; otherwise "
                f"regenerate {PROG_PATCH.name} (see AGENTS.md).")


def nuvoispy(args):
    setup_programmer()
    env = dict(os.environ, PYTHONPATH=str(PROG))
    run([sys.executable, "-m", "nuvoprogpy.nuvoispy", *args], "nuvoispy", env=env)


def check_image(path):
    path = path.resolve()
    if not path.is_file():
        raise Error(f"{path} not found")
    data = path.read_bytes()
    if path.suffix.lower() != ".bin" or data[:1] == b":":
        raise Error(f"{path.name} is not a raw .bin image")
    if not 0 < len(data) <= CODE_SIZE:
        raise Error(f"{path.name} is {len(data)} bytes; firmware must fit in {CODE_SIZE}")
    return path


def flash(port, image):
    print(f"ISP write {image} + {CHIP_CONFIG.name} via Arduino bridge on {port}")
    nuvoispy(["-p", port, "-c", CHIP_CONFIG, "-w", image])


def monitor(port):
    # Opening the port resets the Uno, whose bootloader pulses D13 = nRESET,
    # so the target restarts and you see its output from the beginning.
    print(f"Console on {port} (bridge passthrough). Ctrl+] to quit.")
    run([sys.executable, "-m", "serial.tools.miniterm", "--eol", "LF", "--quiet", port, BRIDGE_BAUD], "monitor")


def arduino_cli():
    local = TOOLS / "arduino-cli"
    cli = shutil.which("arduino-cli", path=str(local)) or shutil.which("arduino-cli")
    if not cli:
        raise Error("arduino-cli not found: unzip it to tools/arduino-cli or put it on PATH")
    config = local / "arduino-cli.yaml"
    if pathlib.Path(cli).parent == local and config.exists():
        return [cli, "--config-file", config]
    return [cli]


def bridge(port, baud, probe, compile_only):
    cli = arduino_cli()
    setup_programmer()
    if not compile_only:
        port = find_port(port)
    lib = PROG / "nuvo51icp"
    if probe:
        # icp_probe reuses the ICP library from the submodule; Arduino needs the
        # files inside the sketch folder, so copy them in (they are git-ignored).
        sketch, flags = TOOLS / "icp_probe", ""
        for name in ("arduino.cpp", "delay.h", "n51_icp.c", "n51_icp.h", "n51_pgm.h"):
            shutil.copy(lib / name, sketch)
    else:
        sketch = lib
        # CACHED_ROM_READ=0: the 64 KB read cache doesn't fit in the Uno's 2 KB RAM.
        # SERIAL_RX_BUFFER_SIZE=256: headroom for 64-byte ISP packets and for USB data
        # arriving while SoftwareSerial has interrupts disabled.
        baud = baud or user_config_int("CFG_CONSOLE_BAUD")
        flags = f"-DCACHED_ROM_READ=0 -DSERIAL_RX_BUFFER_SIZE=256 -DPASSTHROUGH_BAUD={baud}"
    out = OUT / "arduino" / sketch.name
    run(cli + ["compile", "-b", UNO_FQBN, "--build-path", out,
               "--build-property", f"compiler.cpp.extra_flags={flags}",
               "--build-property", f"compiler.c.extra_flags={flags}", sketch], "bridge compile")
    if not compile_only:
        run(cli + ["upload", "-p", port, "-b", UNO_FQBN, "--input-dir", out], "bridge upload")


# ---------------------------------------------------------- command line

def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def image_options(p):
        g = p.add_mutually_exclusive_group()
        g.add_argument("--sim", action="store_true", help="bench build: simulated hot plate, no heating")
        g.add_argument("--cal", action="store_true", help="calibration firmware")
        p.add_argument("--clean", action="store_true", help="remove mcct-firmware/build first")
        return g

    image_options(sub.add_parser("build", help="build the firmware"))
    p = sub.add_parser("test", help="unit tests in the 8051 simulator")
    p.add_argument("--cal", action="store_true", help="the calibration firmware's tests")
    p = sub.add_parser("flash", help="build (or take --bin) and write firmware + CONFIG")
    image_options(p).add_argument("--bin", type=pathlib.Path, help="write this prebuilt .bin instead of building")
    p.add_argument("--port")
    p.add_argument("--monitor", action="store_true", help="open the console afterwards")
    for name, text in (("status", "read the chip ID and CONFIG"), ("monitor", "serial console")):
        sub.add_parser(name, help=text).add_argument("--port")
    p = sub.add_parser("bridge", help="compile + upload the Arduino bridge")
    p.add_argument("--port")
    p.add_argument("--baud", type=int, help="target-side passthrough baud (default: CFG_CONSOLE_BAUD)")
    p.add_argument("--probe", action="store_true", help="upload icp_probe instead (replaces the bridge)")
    p.add_argument("--compile-only", action="store_true")
    sub.add_parser("isp", help="run nuvoispy directly (e.g. isp -p PORT -r dump.bin)", add_help=False)
    args, rest = ap.parse_known_args()
    if rest and args.cmd != "isp":
        ap.error(f"unrecognized arguments: {' '.join(rest)}")
    args.rest = rest
    if args.cmd == "flash" and args.bin and args.clean:
        ap.error("--clean does nothing with --bin")
    return args


def main():
    args = parse_args()
    if args.cmd == "build":
        build(args.sim, args.cal, args.clean)
    elif args.cmd == "test":
        run_tests(args.cal)
    elif args.cmd == "flash":
        port = find_port(args.port)
        image = check_image(args.bin) if args.bin else build(args.sim, args.cal, args.clean)
        flash(port, image)
        if args.monitor:
            monitor(port)
    elif args.cmd == "status":
        nuvoispy(["-p", find_port(args.port), "-u"])
    elif args.cmd == "monitor":
        monitor(find_port(args.port))
    elif args.cmd == "bridge":
        bridge(args.port, args.baud, args.probe, args.compile_only)
    elif args.cmd == "isp":
        nuvoispy(args.rest)


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(line_buffering=True)
        main()
    except Error as e:
        sys.exit(f"error: {e}")
    except KeyboardInterrupt:
        sys.exit(130)
