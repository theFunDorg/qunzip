// qunzip.q -- load the rapidgzip-backed decompressor into a q process.

// Requires qunzip.so (built from qunzip.cpp) to be findable by 2:.
// 2: looks relative to the current working directory (and absolute paths work too),
// so either run q from the directory containing qunzip.so or give a full path below.

qunzip:`:/home/fund/repos/qunzip/build/qunzip 2:(`qunzip;4);   / 4-argument C function

// -----------------------------------------------------------------------------
// Usage:
//   qunzip[file; callback; chunkSizeBytes; nThreads]
//
//   file            symbol path, e.g. `:/data/big.gz  (or a char-vector path)
//   callback        monadic function; receives each decompressed chunk as a
//                   mixed list (type 0h) of char vectors, one item per line,
//                   split on "\n" with the separators removed (read0-style).
//                   Its return value is ignored.
//   chunkSizeBytes  decompressed bytes passed to the callback per call (long)
//   nThreads        number of decode threads; 0 = auto (all cores)
//
//   returns         total decompressed byte count (long)
//
// The callback runs on the q main thread, so it may freely mutate globals,
// upsert into tables, etc. Chunks are split on newline boundaries: every
// chunk holds only complete lines (the trailing partial line is carried over
// to the next chunk), so joining the chunks with raze reconstructs the exact
// line list of the whole file. Note chunkSizeBytes is therefore a target, not
// an exact size -- a chunk may be larger or smaller depending on where
// newlines fall.
// -----------------------------------------------------------------------------

// Example: stream a file and count lines without materialising it in memory.
//   n:0; qunzip[`:/data/big.gz; {[c] n+::count c}; 1024*1024; 4]; n
/n:0; qunzip[`:/home/fund/repos/qunzip/file.csv.gz; {[c] n+::count c}; 1024*1024; 8]; n

path:`:/home/fund/repos/qunzip/tbl.csv.gz;
inp:();
//\ts r:qunzip[path; {[x]`a set x }; 1024*1024*10; 1]

/
q)\ts r:qunzip[path; {[x]`inp upsert enlist x }; 1024*1024; 2]
2252 1360033168 
q)\ts r:qunzip[path; {[x]`inp upsert enlist x }; 1024*1024; 4]
1525 1360033168
q)
1608 1360033168\ts r:qunzip[path; {[x]`inp upsert enlist x }; 1024*1024; 8]