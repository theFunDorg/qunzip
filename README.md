# qunzip — rapidgzip in q/kdb+

A shared object (`qunzip.so`) using  [rapidgzip](https://github.com/mxmlnkn/rapidgzip)'s fast, multi-threaded decoder and feed the decompressed text to a q callback as lists of lines. Handy for writing down EOD CSVs of vendor data received as zipped files.

A single call decompresses the whole file, invoking a `callback` function on each chunk until EOF. 

The callback runs on the q main thread, so it may freely mutate globals, upsert
into tables, etc. Its return value is ignored. Errors (missing file, corrupt stream,
or a signal raised inside the callback) surface as ordinary q errors.

## Build

Requires CMake ≥ 3.17 and a C++17 compiler. rapidgzip is a git submodule; the build
pulls in its header-only library (`rapidgzip/librapidarchive/`) via `FetchContent`.

Clone with submodules (or initialise them in an existing clone):

```bash
# OPTIONAL: Install nasm for ISA-L acceleration in rapidgzip
sudo apt-get install -y nasm

# Make sure you have the submodule loaded
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release # If you choose to skip ISA-L, add: -DLIBRAPIDARCHIVE_WITH_ISAL=OFF
cmake --build build -j
# -> build/qunzip.so   (no "lib" prefix, so 2: finds it by name)
```


## Use
```q
total:qunzip[file; callback; chunkSizeBytes; nThreads];
```
| argument         | type                         | meaning                                                              |
|------------------|------------------------------|----------------------------------------------------------------------|
| `file`           | symbol path (or char vector) | gzip file to read, e.g. `` `:/data/big.gz ``                         |
| `callback`       | monadic function             | called once per chunk with a **mixed list (`0h`) of char vectors**, one item per line |
| `chunkSizeBytes` | long / int                   | target decompressed bytes handed to the callback per call            |
| `nThreads`       | long / int                   | decode threads; `0` = auto (all cores)                               |
| **returns**      | long                         | total decompressed byte count (including newlines)                   |



```q
$ ## Generate test data
$ q test/generate_data.q # Feel free to bump up the rowcount in the file here
$ ls -lh big.csv.gz
-rw-r--r-- 1 fund fund 14M Jul  4 16:03 big.csv.gz
$ q
q)\l qunzip.q
q) // Load the table into memory; Can be used in same way as a .Q.fs/.Q.fp, but on zipped csv
q)lines:(); qunzip[`:big.csv.gz; {[c] lines,:c}; 1024*1024; 4]
```
## Brief comparison with gunzip
Faster than just using a simple gunzip -c
```q
q)\l qunzip.q
q)\ts new:(); qunzip[`:big.csv.gz; {[c] new,:c}; 1024*1024; 4]
282 136520976
q)\ts old:system "gunzip -c ./big.csv.gz"
954 270607056
q)new~old
1b
```
## Notes

- rapidgzip is included as a git submodule (`rapidgzip/`), pinned to a release tag.
  It is header-only C++17; the CMake target `rapidgzip::rapidgzip` wires up its
  include paths and optional acceleration deps (zlib-ng / ISA-L).
