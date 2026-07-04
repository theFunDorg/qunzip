genData:{[n]
  ([] date:n#.z.d;
      time:asc .z.d+n?.z.t;
      sym:n?`AAPL`MSFT`GOOG`AMZN;
      price:0.01*100+n?5000;
      volume:100*1+n?100;
      bid:0.01*100+n?5000;
      ask:0.01*100+n?5000) }

big:genData `long$5e6

save `big.csv
system"gzip big.csv"