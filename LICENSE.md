# License

This repository vendors fastp 1.3.3 and adds Windows/MSYS2-UCRT64 build
compatibility changes.

The upstream fastp source code is distributed under the MIT License:

```text
Copyright (c) 2021 Shifu Chen <chen@haplox.com>
```

The Windows build notes, packaging metadata, and compatibility patches in this
repository are also distributed under the MIT License unless otherwise noted:

```text
Copyright (c) 2026 fastp-windows-build contributors
```

## MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Third-Party Runtime Files

Release ZIP files may include runtime DLLs from MSYS2-UCRT64 packages, such as
`libdeflate.dll`, `libhwy.dll`, `libisal-2.dll`, `libstdc++-6.dll`,
`libgcc_s_seh-1.dll`, and `libwinpthread-1.dll`.

Those DLLs are not owned by this repository. They retain their respective
upstream licenses.
