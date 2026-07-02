// test.q -- end-to-end verification of qunzip.
//
// Expects, in the current working directory:
//     qunzip.so   (built from qunzip.cpp)
//     sample.gz   (produced by make_sample.sh: seq 1..1000000 | gzip)
//
// Run, e.g. (piping via stdin so -1/-2 console output is shown):
//     cd q/build && cp ../test/sample.gz . && q -q < ../test/test.q
//
// sample.gz's uncompressed form is exactly 1000000 lines / 6888896 bytes.

expectedLines:1000000;
expectedBytes:6888896;

assert:{[name;cond] if[not cond; -2 "FAIL: ",name; exit 1]; -1 "ok: ",name;};

qunzip:`:qunzip 2:(`qunzip;4);

// --- happy path: stream the file, counting newlines across chunks -------------
lines:0;
total:qunzip[`:sample.gz; {[c] lines+::sum c="\n"}; 1024*1024; 4];

assert["returns long total bytes"; -7h=type total];
assert["total decompressed bytes match"; total=expectedBytes];
assert["newline count matches"; lines=expectedLines];

// --- chunk size of 64 still works (stress the loop / boundaries) --------------
small:0;
t2:qunzip[`:sample.gz; {[c] small+::count c}; 64; 0];   / nThreads 0 = auto
assert["small chunks sum to same total"; t2=expectedBytes];
assert["small-chunk byte tally matches"; small=expectedBytes];

// --- error path: a missing file must raise, caught by trap -------------------
r:.[qunzip; (`:/no/such/file.gz; {[c]}; 1024; 1); {[e] (`error;e)}];
assert["missing file raises an error"; (first r)~`error];

-1 "ALL TESTS PASSED";
exit 0;
