# fastp for Windows: Unofficial Community Build

This repository provides an unofficial Windows build of
[fastp](https://github.com/OpenGene/fastp) 1.3.3.

fastp is a command-line FASTQ preprocessor for next-generation sequencing data.
The upstream project is primarily built for Unix-like environments. This
repository vendors the fastp 1.3.3 source tree and applies a small
MSYS2-UCRT64 compatibility patch so that `fastp.exe` can be built and used on
Windows.

These builds are not produced, endorsed, or supported by the upstream fastp
project. For fastp itself, see the upstream repository:

https://github.com/OpenGene/fastp

## Download

Download Windows ZIP files from the release page:

https://github.com/win-ngs/fastp-windows-build/releases

| File | Recommended for |
|---|---|
| `fastp-1.3.3-ucrt64.zip` | Windows users processing FASTQ or gzip-compressed FASTQ files |

If no release is available yet, build from source using the instructions below.

## How to Use

This Windows build uses the same command-line options as upstream fastp. For
detailed usage, options, and interpretation of reports, refer to the upstream
fastp documentation:

https://github.com/OpenGene/fastp

1. Download the ZIP file.
2. Extract the ZIP file.
3. Open PowerShell.
4. Move into the extracted folder.
5. Run `fastp.exe`.

Example:

```powershell
cd C:\Users\you\Downloads\fastp-1.3.3-ucrt64
.\fastp.exe --version
.\fastp.exe --help
```

Process paired-end FASTQ files:

```powershell
.\fastp.exe `
  -i C:\data\sample_R1.fastq.gz `
  -I C:\data\sample_R2.fastq.gz `
  -o C:\data\sample_R1.fastp.fastq.gz `
  -O C:\data\sample_R2.fastp.fastq.gz `
  -h C:\data\fastp.html `
  -j C:\data\fastp.json `
  -w 8
```

Keep the extracted files together. Do not move only `fastp.exe` to another
folder, because the `.dll` files in the ZIP are needed for the program to start.

## Files in the ZIP

After extracting the ZIP file, you will see `fastp.exe` and several `.dll`
files.

| File type | What it is | What you should do |
|---|---|---|
| `fastp.exe` | The fastp program | Run this file from PowerShell or Command Prompt |
| `*.dll` files | Runtime libraries needed by `fastp.exe` | Keep them in the same folder as `fastp.exe` |
| `LICENSE.md` and `THIRD_PARTY_NOTICES.txt` | License and third-party notices | Keep them with the extracted files |

The MSYS2-UCRT64 build currently depends on these runtime DLLs:

```text
libdeflate.dll
libgcc_s_seh-1.dll
libhwy.dll
libisal-2.dll
libstdc++-6.dll
libwinpthread-1.dll
```

There is no installer. To remove this Windows build, delete the extracted
folder.

## Source Tree

The patched source tree is included in this repository:

```text
fastp-1.3.3-ucrt64-patch/
```

The upstream fastp README and license are kept inside that directory:

```text
fastp-1.3.3-ucrt64-patch/README.md
fastp-1.3.3-ucrt64-patch/LICENSE
```

Build outputs such as `fastp.exe` and `fastp-1.3.3-ucrt64-patch/obj/` are not meant to be
committed to git. Release ZIP files should be published through GitHub Releases.

## Building from Source

You do not need to build fastp yourself if you only want to use the released
Windows binary. This section is for maintainers or users who want to recreate
the build.

Install [MSYS2](https://www.msys2.org/) first. Open the MSYS2-UCRT64 shell and
install the build tools and fastp dependencies:

```sh
pacman -S --needed \
  base-devel \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-isa-l \
  mingw-w64-ucrt-x86_64-libdeflate \
  mingw-w64-ucrt-x86_64-highway
```

Build fastp:

```sh
cd /c/path/to/fastp-windows-build/fastp-1.3.3-ucrt64-patch
make -j$(nproc)
```

The executable is created as:

```text
fastp-1.3.3-ucrt64-patch/fastp.exe
```

If GCC reports that it cannot create temporary files under the MSYS2 temporary
directory, use a temporary directory inside the source tree:

```sh
mkdir -p .tmp
export TMPDIR="$PWD/.tmp" TMP="$PWD/.tmp" TEMP="$PWD/.tmp"
make -j$(nproc)
```

## Smoke Test

The upstream test FASTQ files are kept in `fastp-1.3.3-ucrt64-patch/testdata/`. After
building, you can run a small paired-end gzip-output test:

```sh
cd /c/path/to/fastp-windows-build/fastp-1.3.3-ucrt64-patch
mkdir -p report-test

./fastp \
  -i testdata/R1.fq \
  -I testdata/R2.fq \
  -o report-test/R1.out.fq.gz \
  -O report-test/R2.out.fq.gz \
  -w 2 \
  -h report-test/fastp.html \
  -j report-test/fastp.json

gzip -t report-test/R1.out.fq.gz report-test/R2.out.fq.gz
```

The HTML and JSON reports are written to:

```text
fastp-1.3.3-ucrt64-patch/report-test/fastp.html
fastp-1.3.3-ucrt64-patch/report-test/fastp.json
```

## Validation Performed

This patched build was checked with MSYS2-UCRT64 using:

```text
g++ 16.1.0
isa-l 2.31.1
libdeflate 1.25
Highway 1.4.0
```

The following checks were run:

```text
make -B -j$(nproc)
./fastp --version
paired-end testdata -> gzip-compressed output
gzip -t output files
single-thread output compared with 8-thread output after decompression
```

The single-thread and 8-thread decompressed FASTQ outputs matched byte-for-byte
in the validation run.

## MSYS2-UCRT64 Compatibility Patch

The upstream fastp 1.3.3 source did not compile unchanged in MSYS2-UCRT64
because the MinGW UCRT64 environment does not provide POSIX `pwrite()`.

The compatibility patch is limited to the parallel gzip writer:

| File | Change | Reason |
|---|---|---|
| `fastp-1.3.3-ucrt64-patch/src/writerthread.cpp` | Added Windows implementations for offset-addressed gzip-block writes using `CreateFileA(FILE_FLAG_OVERLAPPED)`, `WriteFile()` with `OVERLAPPED`, and `SetFilePointerEx()` plus `SetEndOfFile()` | UCRT64 does not provide POSIX `pwrite()`, and shared-file-pointer writes are not safe for the original parallel writer model |
| `fastp-1.3.3-ucrt64-patch/src/writerthread.cpp` | Reuses one thread-local manual-reset event for overlapped writes | Avoids creating and closing a Windows kernel event for every gzip block |
| `fastp-1.3.3-ucrt64-patch/src/writerthread.cpp` | Maps `ERROR_OPERATION_ABORTED` to `EIO` instead of `EINTR` | Avoids an infinite retry loop on an aborted Windows I/O operation |

The modified source locations include comments explaining the Windows/UCRT64
change and keep the previous POSIX or earlier Windows form as a commented-out
reference.

## License

fastp is distributed under the MIT License. See [LICENSE.md](LICENSE.md) and
[fastp-1.3.3-ucrt64-patch/LICENSE](fastp-1.3.3-ucrt64-patch/LICENSE).

Runtime DLLs included in release ZIP files come from MSYS2 packages and retain
their respective upstream licenses.
