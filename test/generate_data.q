genData:{[n]
  ([] date:n#.z.d;
      time:asc .z.d+n?.z.t;
      sym:n?`AAPL`MSFT`GOOG`AMZN;
      price:0.01*100+n?5000;
      volume:100*1+n?100;
      bid:0.01*100+n?5000;
      ask:0.01*100+n?5000) }

/

{

// Generate data
// Append to row
// sleep before you repeat
 }
1_ (csv)0: genData first 1?10
system"sleep ",string 0.001*first 1?1000


appendCsv:{[file;t]
    lines:csv 0: t;                         / header + data rows as strings
    exists:not ()~key file;                 / does file already exist?
    file 0: $[exists; 1 _ lines; lines] }   / skip header if appending

  appendCsv[`:trades.csv;] t