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

## Downloading fastp for Windows

Prebuilt Windows binaries are available from the
[Releases](https://github.com/win-ngs/fastp-windows-build/releases) page
of this repository.

Download the latest release archive, for example:

```text
fastp-1.3.3-windows-ucrt64.zip
```

After extracting the archive, you should see:

```text
fastp-1.3.3-windows-ucrt64/
  fastp.exe
  libdeflate.dll
  libgcc_s_seh-1.dll
  libhwy.dll
  libisal-2.dll
  libstdc++-6.dll
  libwinpthread-1.dll
```

Keep the DLL files in the same folder as `fastp.exe`.

## How to Use

This Windows build uses the same command-line options as upstream fastp. For
detailed usage, options, and interpretation of reports, refer to the upstream
[fastp documentation](https://github.com/OpenGene/fastp).

1. Download the ZIP file.
2. Extract the ZIP file.
3. Open PowerShell.
4. Move into the extracted folder.
5. Run `fastp.exe`.

Example:

```powershell
cd C:\Users\you\Downloads\fastp-1.3.3-windows-ucrt64
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
make
```

The executable is created as:

```text
fastp-1.3.3-ucrt64-patch/fastp.exe
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
make -B
./fastp --version
paired-end testdata -> gzip-compressed output
gzip -t output files
single-thread output compared with 8-thread output after decompression
--stdout output compared byte-for-byte with -o file output
```

The single-thread and 8-thread decompressed FASTQ outputs matched byte-for-byte
in the validation run.

With the `--stdout` binary-mode fix, `fastp --stdout` output matched the
corresponding `-o` file output byte-for-byte, confirming LF (not CRLF) line
endings for streamed FASTQ on native Windows. Note that the `.json` and `.html`
reports are still written through text-mode `ofstream`, so on Windows they use
CRLF line endings; this is cosmetic (JSON parsers and browsers accept it) and is
left as-is.

## MSYS2-UCRT64 Compatibility Patch

The upstream fastp 1.3.3 source did not build and run correctly unchanged in
MSYS2-UCRT64: the MinGW UCRT64 environment does not provide POSIX `pwrite()`,
and on native Windows `stdout` defaults to text mode, which would corrupt
`--stdout` FASTQ output with CRLF line endings.

The compatibility patch covers the parallel gzip writer and the `--stdout`
writer. Paths below are relative to the patched source directory
`fastp-1.3.3-ucrt64-patch/`.

| File | Change | Reason |
|---|---|---|
| `src/writerthread.cpp` | Added Windows implementations for offset-addressed gzip-block writes using `CreateFileA(FILE_FLAG_OVERLAPPED)`, `WriteFile()` with `OVERLAPPED`, and `SetFilePointerEx()` plus `SetEndOfFile()` | UCRT64 does not provide POSIX `pwrite()`, and shared-file-pointer writes are not safe for the original parallel writer model |
| `src/writerthread.cpp` | Wraps the `OVERLAPPED` event in a `thread_local` object that calls `CreateEventA` once per worker thread and `ResetEvent` before each block write, instead of calling `CreateEvent`/`CloseHandle` around every write | Avoids allocating and freeing a Windows kernel event object on every gzip-block write in the parallel writer hot path |
| `src/writerthread.cpp` | Maps `ERROR_OPERATION_ABORTED` to `EIO` instead of `EINTR` | Avoids an infinite retry loop on an aborted Windows I/O operation |
| `src/writer.cpp` | Sets `stdout` to binary mode (`_setmode(_fileno(stdout), _O_BINARY)`) on Windows before `--stdout` streaming | Native Windows `stdout` defaults to text mode, which would translate every `\n` into `\r\n` and corrupt the piped FASTQ; file output already uses `fopen("wb")`, so `-o`/`-O` output was unaffected |

The modified source locations include comments explaining the Windows/UCRT64
change and keep the previous POSIX or earlier Windows form as a commented-out
reference.

## License

fastp is distributed under the MIT License. See [LICENSE.md](LICENSE.md) and
[fastp-1.3.3-ucrt64-patch/LICENSE](fastp-1.3.3-ucrt64-patch/LICENSE).

Runtime DLLs included in release ZIP files come from MSYS2 packages and retain
their respective upstream licenses. See
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) for package and license
details.
