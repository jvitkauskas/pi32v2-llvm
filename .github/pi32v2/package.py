#!/usr/bin/env python3
# Build the pi32v2 toolchain package on Linux, macOS or Windows: clang, ld.lld and the binary tools with only the
# PI32V2 target, compiler-rt's builtins with pi32v2-isa's runtime folded in (the driver finds them itself), and on
# Linux and macOS the pi32v2- prefixed names; installed in OUT/NAME and packed next to it as NAME.tar.xz (NAME.zip
# on Windows). pi32v2-isa's tools/llvm/package.sh is the same for a local Linux build.
#
#   package.py --src LLVM_PROJECT --isa PI32V2_ISA --build BUILD_DIR --out OUT_DIR --name NAME [--tests]
#
# --tests also builds the test tools and runs the target's LLVM lit tests in the build.
import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

TOOLS = ["llvm-ar", "llvm-ranlib", "llvm-nm", "llvm-objdump", "llvm-objcopy", "llvm-readobj", "llvm-readelf",
         "llvm-size", "llvm-strip", "llvm-mc", "llvm-addr2line", "llvm-symbolizer", "llvm-strings"]
TEST_TOOLS = ["llc", "opt", "llvm-mc", "FileCheck", "not", "count", "split-file", "llvm-objdump", "llvm-readobj",
              "llvm-readelf", "llvm-nm", "llvm-objcopy", "llvm-dis", "llvm-as", "yaml2obj", "llvm-config", "clang", "lld"]
WINDOWS = sys.platform == "win32"
EXE = ".exe" if WINDOWS else ""


def run(*cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], check=True, **kw)


def configure(src, build, prefix, tests, launcher):
    args = [
        "-G", "Ninja", "-S", src / "llvm", "-B", build,
        "-DCMAKE_BUILD_TYPE=Release", "-DLLVM_ENABLE_ASSERTIONS=OFF",
        "-DLLVM_TARGETS_TO_BUILD=", "-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=PI32V2",
        "-DLLVM_ENABLE_PROJECTS=clang;lld", "-DLLVM_DEFAULT_TARGET_TRIPLE=pi32v2",
        "-DLLVM_ENABLE_PLUGINS=OFF", "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF",
        "-DLLVM_ENABLE_LIBXML2=OFF", "-DLLVM_ENABLE_LIBEDIT=OFF", "-DLLVM_ENABLE_LIBPFM=OFF",
        "-DLLVM_ENABLE_HTTPLIB=OFF", "-DLLVM_ENABLE_CURL=OFF",
        f"-DLLVM_INCLUDE_TESTS={'ON' if tests else 'OFF'}", "-DLLVM_INCLUDE_EXAMPLES=OFF",
        "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_INCLUDE_DOCS=OFF",
        "-DCLANG_ENABLE_ARCMT=OFF", "-DCLANG_ENABLE_STATIC_ANALYZER=OFF", "-DCLANG_PLUGIN_SUPPORT=OFF",
        "-DLLVM_INSTALL_TOOLCHAIN_ONLY=ON", "-DLLVM_TOOLCHAIN_TOOLS=" + ";".join(TOOLS),
        "-DLLVM_DISTRIBUTION_COMPONENTS=" + ";".join(["clang", "lld", "clang-resource-headers"] + TOOLS),
        f"-DCMAKE_INSTALL_PREFIX={prefix}",
    ]
    if launcher:
        args += [f"-DCMAKE_C_COMPILER_LAUNCHER={launcher}", f"-DCMAKE_CXX_COMPILER_LAUNCHER={launcher}"]
    if sys.platform.startswith("linux"):
        # one static executable each: runs on any Linux of the architecture
        args += ["-DLLVM_BUILD_STATIC=ON"]
        if shutil.which("ld.lld"):
            args += ["-DLLVM_USE_LINKER=lld"]
    elif sys.platform == "darwin":
        args += ["-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"]
    elif WINDOWS:
        args += ["-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"]
    run("cmake", *args)


def builtins(src, isa, prefix, work):
    """compiler-rt's generic builtins and the runtime, in the archive the driver links."""
    bin_ = prefix / "bin"
    clang, ar = bin_ / f"clang{EXE}", bin_ / f"llvm-ar{EXE}"
    rt = src / "compiler-rt" / "lib" / "builtins"
    listing = re.search(r"set\(GENERIC_SOURCES(.*?)\)", (rt / "CMakeLists.txt").read_text(), re.S).group(1)
    jobs = [(rt / f, ["-fno-builtin"]) for f in listing.split() if f.endswith(".c")]
    jobs += [(f, []) for f in sorted((isa / "tools" / "llvm" / "runtime").glob("*.c"))]
    work.mkdir(parents=True, exist_ok=True)

    def compile_(job):
        f, extra = job
        obj = work / (f.parent.name + "-" + f.stem + ".o")
        subprocess.run([str(clang), "--target=pi32v2", "-O2", "-ffreestanding", *extra, "-c", str(f), "-o",
                        str(obj)], check=True)
        return obj

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        objs = list(pool.map(compile_, jobs))
    resource = Path(subprocess.run([str(clang), "-print-resource-dir"], check=True, capture_output=True,
                                   text=True).stdout.strip())
    lib = resource / "lib" / "pi32v2"
    lib.mkdir(parents=True, exist_ok=True)
    out = lib / "libclang_rt.builtins.a"
    out.unlink(missing_ok=True)
    run(ar, "rcs", out, *objs)
    print(f"builtins: {len(objs)} objects in {out}")


def prune(prefix):
    """the drivers for other platforms' formats; on Windows also the plain names, each a full copy there"""
    names = ["clang-cl", "clang-cpp", "lld-link", "ld64.lld", "wasm-ld"] + (["lld"] if WINDOWS else [])
    for n in names:
        (prefix / "bin" / f"{n}{EXE}").unlink(missing_ok=True)


def aliases(prefix):
    """pi32v2- names (the driver takes the target from the prefix); not on Windows, which has no symlinks."""
    if WINDOWS:
        return
    bin_ = prefix / "bin"
    names = {t: f"pi32v2-{t}" for t in ("clang", "clang++", "ld.lld")}
    names.update({t: "pi32v2-" + t.removeprefix("llvm-") for t in TOOLS})
    names["ld.lld"] = "pi32v2-ld.lld"
    for target, name in list(names.items()) + [("ld.lld", "pi32v2-ld")]:
        link = bin_ / name
        link.unlink(missing_ok=True)
        link.symlink_to(target)


def describe(path):
    r = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True)
    return r.stdout.strip() or "unknown"


def notes(src, isa, prefix, name):
    clang = prefix / "bin" / f"clang{EXE}"
    version = subprocess.run([str(clang), "--version"], check=True, capture_output=True, text=True).stdout
    (prefix / "VERSION").write_text(
        f"{name}\nllvm-project (pi32v2-llvm): {describe(src)}\npi32v2-isa: {describe(isa)}\n{version.splitlines()[0]}\n")
    shutil.copy(src / "llvm" / "LICENSE.TXT", prefix / "LICENSE.TXT")
    usage = "pi32v2-clang / clang" if not WINDOWS else "clang"
    (prefix / "README.txt").write_text(f"""pi32v2 LLVM toolchain ({name})

clang, ld.lld and LLVM's binary tools for pi32v2, the CPU core of JieLi's AC79 chips (the AC791N in the M-VAVE
FM-1 and SMK-37), from https://github.com/jvitkauskas/pi32v2-llvm. Unofficial; not affiliated with JieLi or M-VAVE.

bin/ has {usage}, ld.lld and the llvm- tools{'' if WINDOWS else ' (also as pi32v2-objcopy, pi32v2-objdump, ...)'}.
clang targets pi32v2 by default, links with ld.lld and finds compiler-rt's builtins (lib/clang/*/lib/pi32v2) by
itself. There is no C library: link firmware with -nostartfiles -nolibc and its own start-up code and linker
script. Documentation: https://github.com/jvitkauskas/pi32v2-isa/blob/main/docs/TOOLCHAIN.md

Licence: LLVM's, Apache-2.0 with LLVM exceptions (LICENSE.TXT). The runtime functions from pi32v2-isa
(tools/llvm/runtime) are MIT-0.
""")


def smoke(prefix, work):
    """compile and link a program that needs the builtins with the installed tools"""
    bin_ = prefix / "bin"
    work.mkdir(parents=True, exist_ok=True)
    c = work / "smoke.c"
    c.write_text("volatile long long a = 1234567890123LL, b = 97;\nvolatile float x = 3.5f, y = 1.25f;\n"
                 "int v;\nint main(void) {\n  long long q = a / b;\n  float f = x / y;\n"
                 "  __sync_fetch_and_add(&v, 1);\n  return (int)q + (int)f;\n}\n")
    elf = work / "smoke.elf"
    run(bin_ / f"clang{EXE}", "-O2", "-nostartfiles", "-nolibc", "-Wl,-e,main", c, "-o", elf)
    syms = run(bin_ / f"llvm-nm{EXE}", elf, capture_output=True, text=True).stdout
    for s in ("main", "__divdi3"):
        if not re.search(rf"\s{re.escape(s)}$", syms, re.M):
            sys.exit(f"smoke test: {s} missing from the linked program:\n{syms}")
    run(bin_ / f"llvm-objdump{EXE}", "-d", "--disassemble-symbols=main", elf)
    if not WINDOWS:
        run(bin_ / "pi32v2-clang", "-O2", "-c", c, "-o", work / "smoke-prefixed.o")
        run(bin_ / "pi32v2-objcopy", "-O", "binary", elf, work / "smoke.bin")
    print("smoke test passed")


def lit(src, build):
    run("cmake", "--build", build, "--target", *TEST_TOOLS)
    # (clang's own lit configuration needs a full default triple; the release's is pi32v2, so its few PI32V2
    # tests run in a build for the host)
    tests = [src / "llvm" / "test" / d for d in ("CodeGen/PI32V2", "MC/PI32V2", "MC/Disassembler/PI32V2")]
    lit_ = build / "bin" / ("llvm-lit.py" if WINDOWS else "llvm-lit")
    run(sys.executable, lit_, "-sv", *tests)


def pack(prefix, out, name):
    if WINDOWS:
        archive = out / f"{name}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
            for f in sorted(prefix.rglob("*")):
                z.write(f, Path(name) / f.relative_to(prefix))
    else:
        archive = out / f"{name}.tar.xz"
        with tarfile.open(archive, "w:xz") as t:
            t.add(prefix, arcname=name)
    print(f"package: {archive} ({archive.stat().st_size // (1 << 20)} MiB)")
    return archive


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, required=True)
    ap.add_argument("--isa", type=Path, required=True)
    ap.add_argument("--build", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--tests", action="store_true")
    ap.add_argument("--launcher", default=None, help="compiler launcher (ccache, sccache)")
    a = ap.parse_args()
    src, isa, build, out = a.src.resolve(), a.isa.resolve(), a.build.resolve(), a.out.resolve()
    prefix = out / a.name
    if prefix.exists():
        shutil.rmtree(prefix)
    out.mkdir(parents=True, exist_ok=True)
    configure(src, build, prefix, a.tests, a.launcher)
    run("cmake", "--build", build, "--target", "install-distribution-stripped")
    prune(prefix)
    builtins(src, isa, prefix, build / "pi32v2-runtime")
    aliases(prefix)
    notes(src, isa, prefix, a.name)
    smoke(prefix, build / "pi32v2-smoke")
    if a.tests:
        lit(src, build)
    pack(prefix, out, a.name)


if __name__ == "__main__":
    main()
