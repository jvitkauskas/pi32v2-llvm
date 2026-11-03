The pi32v2 LLVM toolchain @VERSION@: clang, `ld.lld` and LLVM's binary tools for **pi32v2**, the CPU core of
JieLi's AC79 chips (the AC791N in the M-VAVE FM-1 and SMK-37 Pro / Elite), built from this repository's `pi32v2`
branch (@COMMIT@). Unofficial; not affiliated with JieLi or M-VAVE.

| Package | For |
| --- | --- |
| `pi32v2-llvm-@VERSION@-linux-x64.tar.xz` | Linux x86-64 (static executables, any distribution) |
| `pi32v2-llvm-@VERSION@-linux-arm64.tar.xz` | Linux AArch64 (static executables, any distribution) |
| `pi32v2-llvm-@VERSION@-windows-x64.zip` | Windows x64 |
| `pi32v2-llvm-@VERSION@-macos-arm64.tar.xz` | macOS 11 or later on Apple silicon |
| `pi32v2-llvm-@VERSION@-macos-x64.tar.xz` | macOS 11 or later on Intel |

Each unpacks to one directory, which can live anywhere: `bin/` has `clang`, `ld.lld` and the `llvm-` tools (on
Linux and macOS also `pi32v2-clang`, `pi32v2-ld`, `pi32v2-objcopy`, ...). clang targets pi32v2 by default and
links compiler-rt's builtins, with [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa)'s runtime
(@ISA@), by itself. There is no C library: firmware brings its own start-up code and linker script and links with
`-nostartfiles -nolibc`. How to use it:
[TOOLCHAIN.md](https://github.com/jvitkauskas/pi32v2-isa/blob/main/docs/TOOLCHAIN.md).

macOS marks downloads from a browser as quarantined; `xattr -dr com.apple.quarantine pi32v2-llvm-@VERSION@-macos-*`
lets the unsigned tools run.

Check a download with `sha256sum -c SHA256SUMS --ignore-missing` (macOS: `shasum -a 256 -c SHA256SUMS
--ignore-missing`).

In GitHub Actions, pi32v2-isa's `setup-pi32v2` action downloads the package for the runner, checks it and puts
`bin/` on `PATH`:

```yaml
- uses: jvitkauskas/pi32v2-isa/.github/actions/setup-pi32v2@main
  with:
    version: @TAG@
```

Licence: LLVM's, Apache-2.0 with LLVM exceptions (`LICENSE.TXT` in each package); the runtime functions from
pi32v2-isa are MIT-0.
