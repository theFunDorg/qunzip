# qunzip — rapidgzip from q/kdb+

A small shared object (`qunzip.so`) that lets a q process stream-decompress a gzip
file with [rapidgzip](https://github.com/mxmlnkn/rapidgzip)'s fast, multi-threaded
decoder and feed the decompressed text to a q callback as lists of lines.

## What it does

One C function, loaded with `2:`:

```q
qunzip:`:qunzip 2:(`qunzip;4);
total:qunzip[file; callback; chunkSizeBytes; nThreads];
```

| argument         | type                         | meaning                                                              |
|------------------|------------------------------|----------------------------------------------------------------------|
| `file`           | symbol path (or char vector) | gzip file to read, e.g. `` `:/data/big.gz ``                         |
| `callback`       | monadic function             | called once per chunk with a **mixed list (`0h`) of char vectors**, one item per line |
| `chunkSizeBytes` | long / int                   | target decompressed bytes handed to the callback per call            |
| `nThreads`       | long / int                   | decode threads; `0` = auto (all cores)                               |
| **returns**      | long                         | total decompressed byte count (including newlines)                   |

A single call decompresses the **whole file**, invoking `callback` repeatedly until
EOF. Each chunk is split on `"\n"` with the separators removed, `read0`-style, and
only ever contains **complete lines**: a trailing partial line is carried over and
prepended to the next chunk (so `chunkSizeBytes` is a target, not an exact size —
a chunk may be slightly larger or smaller depending on where newlines fall).
Joining the chunks with `raze` reconstructs the exact line list of the whole file.

The callback runs on the q main thread, so it may freely mutate globals, upsert
into tables, etc. Its return value is ignored. Errors (missing file, corrupt stream,
or a signal raised inside the callback) surface as ordinary q errors.

## Build

Requires CMake ≥ 3.17 and a C++17 compiler. rapidgzip is a git submodule; the build
pulls in its header-only library (`rapidgzip/librapidarchive/`) via `FetchContent`.

Clone with submodules (or initialise them in an existing clone):

```bash
git clone --recurse-submodules <this-repo-url>
# or, in an existing clone:
git submodule update --init --recursive
```

Then build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# -> build/qunzip.so   (no "lib" prefix, so 2: finds it by name)
```

The kdb+ C API header `k.h` (MIT licensed, from KxSystems/kdb `c/c/k.h`) is vendored
in this repo next to `qunzip.cpp`, so no separate download is needed.

rapidgzip's ISA-L acceleration (on by default) needs the NASM assembler. If NASM
isn't installed, either install it or configure with ISA-L off (rapidgzip falls back
to its own inflate / zlib-ng):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLIBRAPIDARCHIVE_WITH_ISAL=OFF
```

## Use

```q
\l qunzip.q                       / or: qunzip:`:qunzip 2:(`qunzip;4)

/ count lines in a huge gzip without materialising it:
n:0; qunzip[`:/data/big.gz; {[c] n+::count c}; 1024*1024; 8]; n

/ accumulate all lines (equivalent to read0 on the uncompressed file):
lines:(); qunzip[`:/data/tbl.csv.gz; {[c] lines,:c}; 1024*1024; 4]
```

## Test

```bash
bash test/make_sample.sh                 # writes test/sample.gz (1e6 lines)
cd build && cp ../test/sample.gz .
q -q < ../test/test.q                     # asserts byte/line totals + error path
```

Pipe the script via stdin (`q -q < …`) rather than passing it as a file argument —
some q builds suppress `-1`/`-2` console output for the file-argument form.

## Notes

- `k.h` is vendored from KxSystems/kdb (`c/c/k.h`), MIT licensed. Compiled with
  `KXVER 3` (v3.0+/64-bit ABI).
- rapidgzip is included as a git submodule (`rapidgzip/`), pinned to a release tag.
  It is header-only C++17; the CMake target `rapidgzip::rapidgzip` wires up its
  include paths and optional acceleration deps (zlib-ng / ISA-L).
- The call blocks the (single-threaded) q process until EOF, like any blocking FFI.
