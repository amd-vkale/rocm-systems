| bench | range | config | valid | hash=native | status | dispatches | range ms (median) | x native | pass0 ms | replay pass ms | SQ_WAVES per pass equal |
|---|---|---|---|---|---|---|---|---|---|---|---|
| c-chase | full | native | 5/5 | 5/5 | - x5 | - | 3.5 | 1.00 | - | - | - |
| c-chase | full | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 201.4 | 56.87 | 171.7 | 8.1 | yes |
| c-chase | full | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 212.4 | 59.96 | 167.2 | 12.4 | NO |
| c-chase | full | v2-host | 5/5 | 5/5 | REPLAYED x5 | 8 | 159.9 | 45.15 | 136.5 | 5.3 | yes |
| c-chase | full | v2-host-notrack | 0/2 | 0/2 | REPLAYED x2 | 8 | 158.1 | 44.64 | 137.6 | 5.3 | yes |
| c-chase | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-chase | kernel | native | 5/5 | 5/5 | - x5 | - | 3.5 | 1.00 | - | - | - |
| c-chase | kernel | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 200.5 | 57.28 | 171.8 | 7.9 | NO |
| c-chase | kernel | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 208.7 | 59.64 | 166.9 | 12.4 | yes |
| c-chase | kernel | v2-host | 5/5 | 5/5 | REPLAYED x5 | 8 | 160.8 | 45.94 | 138.3 | 5.4 | yes |
| c-chase | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-gather | full | native | 5/5 | 5/5 | - x5 | - | 15.3 | 1.00 | - | - | - |
| c-gather | full | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 687.8 | 45.09 | 538.8 | 35.0 | yes |
| c-gather | full | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 705.2 | 46.23 | 538.1 | 40.0 | yes |
| c-gather | full | v2-host | 5/5 | 5/5 | REPLAYED x5 | 8 | 554.5 | 36.35 | 403.2 | 30.9 | yes |
| c-gather | full | v2-host-notrack | 0/2 | 0/2 | REPLAYED x2 | 8 | 428.6 | 28.10 | 329.7 | 23.2 | yes |
| c-gather | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-gather | kernel | native | 5/5 | 5/5 | - x5 | - | 9.9 | 1.00 | - | - | - |
| c-gather | kernel | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 675.9 | 68.37 | 532.6 | 34.0 | yes |
| c-gather | kernel | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 704.2 | 71.24 | 529.6 | 39.9 | yes |
| c-gather | kernel | v2-host | 5/5 | 5/5 | REPLAYED x5 | 8 | 425.4 | 43.04 | 329.6 | 18.8 | yes |
| c-gather | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-gups | full | native | 5/5 | 5/5 | - x5 | - | 22.7 | 1.00 | - | - | - |
| c-gups | full | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 317.8 | 14.02 | 235.7 | 24.5 | yes |
| c-gups | full | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 327.5 | 14.45 | 233.2 | 28.5 | NO |
| c-gups | full | v2-host | 5/5 | 5/5 | REPLAYED x5 | 8 | 367.5 | 16.21 | 247.1 | 32.4 | yes |
| c-gups | full | v2-host-notrack | 0/2 | 0/2 | REPLAYED x2 | 8 | 313.7 | 13.84 | 227.9 | 25.2 | yes |
| c-gups | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-gups | kernel | native | 5/5 | 5/5 | - x5 | - | 17.3 | 1.00 | - | - | - |
| c-gups | kernel | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 318.0 | 18.39 | 230.4 | 24.1 | NO |
| c-gups | kernel | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 324.6 | 18.77 | 227.2 | 28.9 | NO |
| c-gups | kernel | v2-host | 5/5 | 5/5 | REPLAYED x5 | 8 | 246.4 | 14.25 | 173.5 | 20.6 | NO |
| c-gups | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-hist | full | native | 5/5 | 5/5 | - x5 | - | 292.6 | 1.00 | - | - | - |
| c-hist | full | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 1534.3 | 5.24 | 613.7 | 301.2 | NO |
| c-hist | full | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 1543.7 | 5.28 | 608.6 | 305.6 | NO |
| c-hist | full | v2-host | 5/5 | 5/5 | REPLAYED x5 | 8 | 1402.5 | 4.79 | 504.5 | 293.5 | NO |
| c-hist | full | v2-host-notrack | 0/2 | 0/2 | REPLAYED x2 | 8 | 1463.4 | 5.00 | 563.9 | 294.8 | NO |
| c-hist | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-hist | kernel | native | 5/5 | 5/5 | - x5 | - | 292.0 | 1.00 | - | - | - |
| c-hist | kernel | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 1536.2 | 5.26 | 610.7 | 301.9 | NO |
| c-hist | kernel | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 8 | 1546.2 | 5.29 | 609.1 | 306.3 | NO |
| c-hist | kernel | v2-host | 5/5 | 5/5 | REPLAYED x5 | 8 | 1406.5 | 4.82 | 502.7 | 296.4 | NO |
| c-hist | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-rsbench | full | native | 5/5 | 5/5 | - x5 | - | 108.0 | 1.00 | - | - | - |
| c-rsbench | full | v1-packet | 5/5 | 5/5 | MEMORY_COPY_IN_RANGE x5 | 1 | 202.0 | 1.87 | 198.7 | - | - |
| c-rsbench | full | v2-packet | 5/5 | 5/5 | MEMORY_COPY_IN_RANGE x5 | 1 | 202.0 | 1.87 | 198.7 | - | - |
| c-rsbench | full | v2-host | 5/5 | 5/5 | ALLOCATION_CHANGED_IN_RANGE x5 | 2 | 200.0 | 1.85 | 196.9 | - | - |
| c-rsbench | full | v2-host-notrack | 2/2 | 2/2 | ALLOCATION_CHANGED_IN_RANGE x2 | 2 | 199.5 | 1.85 | 197.1 | - | - |
| c-rsbench | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-rsbench | full | native-nomm | 3/3 | 3/3 | - x3 | - | 97.0 | 0.90 | - | - | - |
| c-rsbench | full | v1-packet-nomm | 3/3 | 3/3 | MEMORY_COPY_IN_RANGE x3 | 1 | 204.0 | 1.89 | 201.5 | - | - |
| c-rsbench | full | v2-packet-nomm | 3/3 | 3/3 | MEMORY_COPY_IN_RANGE x3 | 1 | 204.0 | 1.89 | 200.6 | - | - |
| c-rsbench | full | v2-host-nomm | 3/3 | 3/3 | REPLAYED x3 | 2 | 420.0 | 3.89 | 196.4 | 74.0 | NO |
| c-rsbench | kernel | native | 5/5 | 5/5 | - x5 | - | 97.0 | 1.00 | - | - | - |
| c-rsbench | kernel | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 1 | 440.0 | 4.54 | 229.5 | 52.6 | NO |
| c-rsbench | kernel | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 1 | 455.0 | 4.69 | 227.7 | 57.5 | yes |
| c-rsbench | kernel | v2-host | 5/5 | 5/5 | ALLOCATION_CHANGED_IN_RANGE x5 | 1 | 242.0 | 2.49 | 183.4 | - | - |
| c-rsbench | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-rsbench | kernel | native-nomm | 3/3 | 3/3 | - x3 | - | 97.0 | 1.00 | - | - | - |
| c-rsbench | kernel | v1-packet-nomm | 3/3 | 3/3 | ALLOCATION_CHANGED_IN_RANGE x3 | 1 | 349.0 | 3.60 | 285.6 | - | - |
| c-rsbench | kernel | v2-packet-nomm | 3/3 | 3/3 | ALLOCATION_CHANGED_IN_RANGE x3 | 1 | 244.0 | 2.52 | 185.3 | - | - |
| c-rsbench | kernel | v2-host-nomm | 3/3 | 3/3 | REPLAYED x3 | 1 | 414.0 | 4.27 | 204.6 | 49.7 | NO |
| c-spmv | full | native | 5/5 | 5/5 | - x5 | - | 9.0 | 1.00 | - | - | - |
| c-spmv | full | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 16 | 364.1 | 40.30 | 289.6 | 20.8 | yes |
| c-spmv | full | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 16 | 379.1 | 41.97 | 286.9 | 24.4 | yes |
| c-spmv | full | v2-host | 5/5 | 5/5 | REPLAYED x5 | 16 | 258.6 | 28.62 | 203.0 | 14.2 | yes |
| c-spmv | full | v2-host-notrack | 0/2 | 0/2 | REPLAYED x2 | 16 | 255.1 | 28.24 | 202.6 | 12.7 | yes |
| c-spmv | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-spmv | kernel | native | 5/5 | 5/5 | - x5 | - | 8.6 | 1.00 | - | - | - |
| c-spmv | kernel | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 16 | 378.5 | 43.78 | 290.1 | 21.3 | yes |
| c-spmv | kernel | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 16 | 377.8 | 43.69 | 287.3 | 25.4 | yes |
| c-spmv | kernel | v2-host | 5/5 | 5/5 | REPLAYED x5 | 16 | 258.5 | 29.90 | 199.2 | 13.5 | yes |
| c-spmv | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-xsbench | full | native | 5/5 | 5/5 | - x5 | - | 100.0 | 1.00 | - | - | - |
| c-xsbench | full | v1-packet | 5/5 | 5/5 | MEMORY_COPY_IN_RANGE x5 | 1 | 204.0 | 2.04 | 201.4 | - | - |
| c-xsbench | full | v2-packet | 5/5 | 5/5 | MEMORY_COPY_IN_RANGE x5 | 1 | 204.0 | 2.04 | 201.1 | - | - |
| c-xsbench | full | v2-host | 5/5 | 5/5 | ALLOCATION_CHANGED_IN_RANGE x5 | 2 | 205.0 | 2.05 | 202.3 | - | - |
| c-xsbench | full | v2-host-notrack | 2/2 | 2/2 | ALLOCATION_CHANGED_IN_RANGE x2 | 2 | 203.5 | 2.04 | 201.1 | - | - |
| c-xsbench | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-xsbench | full | native-nomm | 3/3 | 3/3 | - x3 | - | 103.0 | 1.03 | - | - | - |
| c-xsbench | full | v1-packet-nomm | 3/3 | 3/3 | MEMORY_COPY_IN_RANGE x3 | 1 | 213.0 | 2.13 | 209.4 | - | - |
| c-xsbench | full | v2-packet-nomm | 3/3 | 3/3 | MEMORY_COPY_IN_RANGE x3 | 1 | 205.0 | 2.05 | 203.6 | - | - |
| c-xsbench | full | v2-host-nomm | 3/3 | 3/3 | REPLAYED x3 | 2 | 407.0 | 4.07 | 207.9 | 63.0 | NO |
| c-xsbench | kernel | native | 5/5 | 5/5 | - x5 | - | 99.0 | 1.00 | - | - | - |
| c-xsbench | kernel | v1-packet | 5/5 | 5/5 | REPLAYED x5 | 1 | 636.0 | 6.42 | 433.5 | 33.8 | NO |
| c-xsbench | kernel | v2-packet | 5/5 | 5/5 | REPLAYED x5 | 1 | 653.0 | 6.60 | 431.9 | 37.8 | NO |
| c-xsbench | kernel | v2-host | 5/5 | 5/5 | ALLOCATION_CHANGED_IN_RANGE x5 | 1 | 376.0 | 3.80 | 270.3 | - | - |
| c-xsbench | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| c-xsbench | kernel | native-nomm | 3/3 | 3/3 | - x3 | - | 103.0 | 1.04 | - | - | - |
| c-xsbench | kernel | v1-packet-nomm | 3/3 | 3/3 | ALLOCATION_CHANGED_IN_RANGE x3 | 1 | 372.0 | 3.76 | 272.7 | - | - |
| c-xsbench | kernel | v2-packet-nomm | 3/3 | 3/3 | ALLOCATION_CHANGED_IN_RANGE x3 | 1 | 383.0 | 3.87 | 271.4 | - | - |
| c-xsbench | kernel | v2-host-nomm | 3/3 | 3/3 | REPLAYED x3 | 1 | 471.0 | 4.76 | 285.9 | 22.8 | NO |
| f-babel-triad | kernel | native | 5/5 | - | - x5 | - | 4.8 | 1.00 | - | - | - |
| f-babel-triad | kernel | v1-packet | 5/5 | - | REPLAYED x5 | 20 | 940.5 | 197.46 | 749.3 | 42.7 | yes |
| f-babel-triad | kernel | v2-packet | 5/5 | - | REPLAYED x5 | 20 | 932.3 | 195.73 | 747.6 | 43.3 | NO |
| f-babel-triad | kernel | v2-host | 5/5 | - | REPLAYED x5 | 20 | 542.3 | 113.86 | 435.0 | 18.2 | yes |
| f-babel-triad | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-babelstream | kernel | native | 5/5 | - | - x5 | - | 63.8 | 1.00 | - | - | - |
| f-babelstream | kernel | v1-packet | 5/5 | - | MEMORY_COPY_IN_RANGE x5 | 4 | 545.4 | 8.54 | 476.3 | - | - |
| f-babelstream | kernel | v2-packet | 5/5 | - | MEMORY_COPY_IN_RANGE x5 | 4 | 544.1 | 8.52 | 477.7 | - | - |
| f-babelstream | kernel | v2-host | 5/5 | - | ALLOCATION_CHANGED_IN_RANGE x5 | 100 | 558.4 | 8.75 | 488.5 | - | - |
| f-babelstream | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-babelstream | kernel | native-nomm | 3/3 | - | - x3 | - | 64.7 | 1.01 | - | - | - |
| f-babelstream | kernel | v1-packet-nomm | 3/3 | - | MEMORY_COPY_IN_RANGE x3 | 4 | 528.8 | 8.28 | 481.3 | - | - |
| f-babelstream | kernel | v2-packet-nomm | 3/3 | - | MEMORY_COPY_IN_RANGE x3 | 4 | 531.1 | 8.32 | 479.2 | - | - |
| f-babelstream | kernel | v2-host-nomm | 3/3 | - | REPLAYED x3 | 100 | 690.5 | 10.82 | 514.5 | 35.3 | yes |
| f-chase | full | native | 5/5 | - | - x5 | - | 3.8 | 1.00 | - | - | - |
| f-chase | full | v1-packet | 5/5 | - | REPLAYED x5 | 8 | 197.7 | 52.62 | 169.1 | 8.2 | yes |
| f-chase | full | v2-packet | 5/5 | - | REPLAYED x5 | 8 | 212.2 | 56.48 | 168.0 | 13.2 | yes |
| f-chase | full | v2-host | 5/5 | - | REPLAYED x5 | 8 | 156.8 | 41.74 | 134.7 | 5.8 | yes |
| f-chase | full | v2-host-notrack | 0/2 | - | REPLAYED x2 | 8 | 162.4 | 43.22 | 140.5 | 5.8 | yes |
| f-chase | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-chase | kernel | native | 5/5 | - | - x5 | - | 3.5 | 1.00 | - | - | - |
| f-chase | kernel | v1-packet | 5/5 | - | REPLAYED x5 | 8 | 203.6 | 58.26 | 171.1 | 8.7 | yes |
| f-chase | kernel | v2-packet | 5/5 | - | REPLAYED x5 | 8 | 213.2 | 61.01 | 168.8 | 13.0 | yes |
| f-chase | kernel | v2-host | 5/5 | - | REPLAYED x5 | 8 | 162.8 | 46.58 | 138.0 | 5.9 | yes |
| f-chase | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-gather | full | native | 5/5 | - | - x5 | - | 15.8 | 1.00 | - | - | - |
| f-gather | full | v1-packet | 5/5 | - | REPLAYED x5 | 8 | 685.0 | 43.45 | 545.3 | 34.5 | yes |
| f-gather | full | v2-packet | 5/5 | - | REPLAYED x5 | 8 | 708.2 | 44.92 | 542.1 | 42.2 | NO |
| f-gather | full | v2-host | 5/5 | - | REPLAYED x5 | 8 | 558.5 | 35.42 | 405.5 | 32.3 | NO |
| f-gather | full | v2-host-notrack | 0/2 | - | REPLAYED x2 | 8 | 429.6 | 27.25 | 327.1 | 23.8 | yes |
| f-gather | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-gather | kernel | native | 5/5 | - | - x5 | - | 10.1 | 1.00 | - | - | - |
| f-gather | kernel | v1-packet | 5/5 | - | REPLAYED x5 | 8 | 690.4 | 68.19 | 538.6 | 36.2 | yes |
| f-gather | kernel | v2-packet | 5/5 | - | REPLAYED x5 | 8 | 697.2 | 68.86 | 537.1 | 40.5 | yes |
| f-gather | kernel | v2-host | 5/5 | - | REPLAYED x5 | 8 | 426.0 | 42.07 | 331.9 | 20.0 | yes |
| f-gather | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-gups | full | native | 5/5 | - | - x5 | - | 22.8 | 1.00 | - | - | - |
| f-gups | full | v1-packet | 5/5 | - | REPLAYED x5 | 8 | 319.9 | 14.01 | 237.6 | 24.8 | yes |
| f-gups | full | v2-packet | 5/5 | - | REPLAYED x5 | 8 | 333.8 | 14.61 | 237.2 | 29.3 | yes |
| f-gups | full | v2-host | 5/5 | - | REPLAYED x5 | 8 | 366.8 | 16.06 | 247.1 | 33.1 | yes |
| f-gups | full | v2-host-notrack | 0/2 | - | REPLAYED x2 | 8 | 265.5 | 11.62 | 179.9 | 25.8 | yes |
| f-gups | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-gups | kernel | native | 5/5 | - | - x5 | - | 17.6 | 1.00 | - | - | - |
| f-gups | kernel | v1-packet | 5/5 | - | REPLAYED x5 | 8 | 314.9 | 17.93 | 232.2 | 24.8 | yes |
| f-gups | kernel | v2-packet | 5/5 | - | REPLAYED x5 | 8 | 332.1 | 18.91 | 230.3 | 29.8 | NO |
| f-gups | kernel | v2-host | 5/5 | - | REPLAYED x5 | 8 | 243.6 | 13.87 | 174.0 | 20.8 | NO |
| f-gups | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-hist | full | native | 5/5 | - | - x5 | - | 291.8 | 1.00 | - | - | - |
| f-hist | full | v1-packet | 5/5 | - | REPLAYED x5 | 8 | 1536.7 | 5.27 | 610.2 | 302.6 | NO |
| f-hist | full | v2-packet | 5/5 | - | REPLAYED x5 | 8 | 1550.0 | 5.31 | 607.5 | 306.8 | NO |
| f-hist | full | v2-host | 5/5 | - | REPLAYED x5 | 8 | 1402.1 | 4.80 | 500.8 | 294.8 | NO |
| f-hist | full | v2-host-notrack | 0/2 | - | REPLAYED x2 | 8 | 1400.6 | 4.80 | 502.2 | 294.0 | NO |
| f-hist | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-hist | kernel | native | 5/5 | - | - x5 | - | 292.0 | 1.00 | - | - | - |
| f-hist | kernel | v1-packet | 5/5 | - | REPLAYED x5 | 8 | 1536.8 | 5.26 | 610.0 | 302.5 | NO |
| f-hist | kernel | v2-packet | 5/5 | - | REPLAYED x5 | 8 | 1547.8 | 5.30 | 606.3 | 307.7 | NO |
| f-hist | kernel | v2-host | 5/5 | - | REPLAYED x5 | 8 | 1398.1 | 4.79 | 501.0 | 294.7 | NO |
| f-hist | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-jacobi | full | native | 5/5 | - | - x5 | - | 7.3 | 1.00 | - | - | - |
| f-jacobi | full | v1-packet | 5/5 | - | REPLAYED x5 | 16 | 397.0 | 54.25 | 335.7 | 15.0 | yes |
| f-jacobi | full | v2-packet | 5/5 | - | REPLAYED x5 | 16 | 416.3 | 56.89 | 336.5 | 20.2 | yes |
| f-jacobi | full | v2-host | 5/5 | - | REPLAYED x5 | 16 | 380.7 | 52.02 | 293.2 | 19.6 | yes |
| f-jacobi | full | v2-host-notrack | 0/2 | - | REPLAYED x2 | 16 | 269.5 | 36.83 | 217.2 | 11.8 | yes |
| f-jacobi | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-jacobi | kernel | native | 5/5 | - | - x5 | - | 1.9 | 1.00 | - | - | - |
| f-jacobi | kernel | v1-packet | 5/5 | - | REPLAYED x5 | 16 | 402.0 | 214.86 | 330.3 | 17.6 | yes |
| f-jacobi | kernel | v2-packet | 5/5 | - | REPLAYED x5 | 16 | 411.3 | 219.82 | 330.9 | 21.2 | yes |
| f-jacobi | kernel | v2-host | 5/5 | - | REPLAYED x5 | 16 | 262.4 | 140.24 | 219.2 | 8.3 | yes |
| f-jacobi | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-spmv | full | native | 5/5 | - | - x5 | - | 9.0 | 1.00 | - | - | - |
| f-spmv | full | v1-packet | 5/5 | - | REPLAYED x5 | 16 | 377.2 | 41.91 | 295.0 | 22.4 | yes |
| f-spmv | full | v2-packet | 5/5 | - | REPLAYED x5 | 16 | 396.0 | 44.00 | 293.7 | 26.8 | yes |
| f-spmv | full | v2-host | 5/5 | - | REPLAYED x5 | 16 | 267.2 | 29.68 | 206.7 | 15.0 | yes |
| f-spmv | full | v2-host-notrack | 0/2 | - | REPLAYED x2 | 16 | 251.3 | 27.92 | 198.1 | 13.0 | yes |
| f-spmv | full | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |
| f-spmv | kernel | native | 5/5 | - | - x5 | - | 8.4 | 1.00 | - | - | - |
| f-spmv | kernel | v1-packet | 5/5 | - | REPLAYED x5 | 16 | 378.1 | 44.80 | 294.4 | 22.3 | yes |
| f-spmv | kernel | v2-packet | 5/5 | - | REPLAYED x5 | 16 | 389.2 | 46.12 | 293.0 | 27.0 | NO |
| f-spmv | kernel | v2-host | 5/5 | - | REPLAYED x5 | 16 | 262.6 | 31.12 | 201.7 | 14.3 | yes |
| f-spmv | kernel | v1-host | 0/1 | - | NO-SDK-HOST x1 | - | - | - | - | - | - |

| bench | range | v1 packet dispatches | v2 packet dispatches | v2 host dispatches | SQ_WAVES v1 / v2 packet / v2 host |
|---|---|---|---|---|---|
| c-chase | full | [8] | [8] | [8] | [8192] / [8192] / [8192] |
| c-chase | kernel | [8] | [8] | [8] | [8192] / [8192] / [8192] |
| c-gather | full | [8] | [8] | [8] | [2097150.0] / [2097150.0] / [2097150.0] |
| c-gather | kernel | [8] | [8] | [8] | [2097150.0] / [2097150.0] / [2097150.0] |
| c-gups | full | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| c-gups | kernel | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| c-hist | full | [8] | [8] | [8] | [8388610.0, 8394560.0, 8401430.0] / [8388610.0, 8401100.0, 8408860.0] / [8388610.0, 8407050.0] |
| c-hist | kernel | [8] | [8] | [8] | [8388610.0, 8395950.0] / [8388610.0, 8389910.0, 8392130.0, 8393040.0] / [8388610.0, 8400260.0] |
| c-rsbench | full | [1] | [1] | [2] | [] / [] / [] |
| c-rsbench | kernel | [1] | [1] | [1] | [4864, 14592] / [4864] / [] |
| c-spmv | full | [16] | [16] | [16] | [262144] / [262144] / [262144] |
| c-spmv | kernel | [16] | [16] | [16] | [262144] / [262144] / [262144] |
| c-xsbench | full | [1] | [1] | [2] | [] / [] / [] |
| c-xsbench | kernel | [1] | [1] | [1] | [4864] / [4864] / [] |
| f-babel-triad | kernel | [20] | [20] | [20] | [97280] / [97280] / [97280] |
| f-babelstream | kernel | [4] | [4] | [100] | [] / [] / [] |
| f-chase | full | [8] | [8] | [8] | [8192] / [8192] / [8192] |
| f-chase | kernel | [8] | [8] | [8] | [8192] / [8192] / [8192] |
| f-gather | full | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| f-gather | kernel | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| f-gups | full | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| f-gups | kernel | [8] | [8] | [8] | [38912] / [38912, 43776] / [38912] |
| f-hist | full | [8] | [8] | [8] | [38912] / [38912, 47424] / [38912] |
| f-hist | kernel | [8] | [8] | [8] | [38912, 44992] / [38912] / [38912, 45732] |
| f-jacobi | full | [16] | [16] | [16] | [77824] / [77824] / [77824] |
| f-jacobi | kernel | [16] | [16] | [16] | [77824] / [77824] / [77824] |
| f-spmv | full | [16] | [16] | [16] | [77824] / [77824] / [77824] |
| f-spmv | kernel | [16] | [16] | [16] | [77824] / [77824] / [77824] |

Condensed: median range wall ms (app-measured, all passes); copy-decl / alloc-decl = declined, ran once

| bench | range | native | v1 packet | v2 packet | v2 host | v2 host, pool off |
|---|---|---|---|---|---|---|
| c-chase | full | 3.5 | 201.4 | 212.4 | 159.9 | - |
| c-chase | kernel | 3.5 | 200.5 | 208.7 | 160.8 | - |
| c-gather | full | 15.3 | 687.8 | 705.2 | 554.5 | - |
| c-gather | kernel | 9.9 | 675.9 | 704.2 | 425.4 | - |
| c-gups | full | 22.7 | 317.8 | 327.5 | 367.5 | - |
| c-gups | kernel | 17.3 | 318.0 | 324.6 | 246.4 | - |
| c-hist | full | 292.6 | 1534.3 | 1543.7 | 1402.5 | - |
| c-hist | kernel | 292.0 | 1536.2 | 1546.2 | 1406.5 | - |
| c-rsbench | full | 108.0 | 202.0 copy-decl | 202.0 copy-decl | 200.0 alloc-decl | 420.0 |
| c-rsbench | kernel | 97.0 | 440.0 | 455.0 | 242.0 alloc-decl | 414.0 |
| c-spmv | full | 9.0 | 364.1 | 379.1 | 258.6 | - |
| c-spmv | kernel | 8.6 | 378.5 | 377.8 | 258.5 | - |
| c-xsbench | full | 100.0 | 204.0 copy-decl | 204.0 copy-decl | 205.0 alloc-decl | 407.0 |
| c-xsbench | kernel | 99.0 | 636.0 | 653.0 | 376.0 alloc-decl | 471.0 |
| f-babel-triad | kernel | 4.8 | 940.5 | 932.3 | 542.3 | - |
| f-babelstream | kernel | 63.8 | 545.4 copy-decl | 544.1 copy-decl | 558.4 alloc-decl | 690.5 |
| f-chase | full | 3.8 | 197.7 | 212.2 | 156.8 | - |
| f-chase | kernel | 3.5 | 203.6 | 213.2 | 162.8 | - |
| f-gather | full | 15.8 | 685.0 | 708.2 | 558.5 | - |
| f-gather | kernel | 10.1 | 690.4 | 697.2 | 426.0 | - |
| f-gups | full | 22.8 | 319.9 | 333.8 | 366.8 | - |
| f-gups | kernel | 17.6 | 314.9 | 332.1 | 243.6 | - |
| f-hist | full | 291.8 | 1536.7 | 1550.0 | 1402.1 | - |
| f-hist | kernel | 292.0 | 1536.8 | 1547.8 | 1398.1 | - |
| f-jacobi | full | 7.3 | 397.0 | 416.3 | 380.7 | - |
| f-jacobi | kernel | 1.9 | 402.0 | 411.3 | 262.4 | - |
| f-spmv | full | 9.0 | 377.2 | 396.0 | 267.2 | - |
| f-spmv | kernel | 8.4 | 378.1 | 389.2 | 262.6 | - |

| bench | range | v1 packet pass1 / pass2 ms | v2 packet pass1 / pass2 ms | v2 host pass1 / pass2 ms |
|---|---|---|---|---|
| c-chase | full | 6.2 / 6.0 | 18.6 / 6.2 | 6.1 / 6.1 |
| c-chase | kernel | 6.2 / 5.9 | 19.0 / 6.2 | 6.2 / 6.1 |
| c-gather | full | 23.0 / 22.8 | 35.1 / 23.1 | 39.1 / 38.3 |
| c-gather | kernel | 23.0 / 22.8 | 36.3 / 23.1 | 23.0 / 23.0 |
| c-gups | full | 21.4 / 21.1 | 33.4 / 21.3 | 37.4 / 37.1 |
| c-gups | kernel | 21.4 / 21.1 | 35.6 / 21.4 | 21.3 / 22.2 |
| c-hist | full | 295.4 / 295.1 | 309.5 / 295.5 | 295.6 / 295.6 |
| c-hist | kernel | 297.0 / 296.7 | 308.5 / 296.2 | 300.3 / 295.6 |
| c-rsbench | kernel | 50.8 / 49.8 | 65.6 / 50.2 | - |
| c-spmv | full | 15.8 / 15.4 | 28.7 / 15.6 | 16.5 / 16.4 |
| c-spmv | kernel | 15.9 / 15.3 | 28.8 / 15.6 | 15.5 / 15.5 |
| c-xsbench | kernel | 25.6 / 25.5 | 38.7 / 26.0 | - |
| f-babel-triad | kernel | 25.1 / 24.5 | 31.3 / 24.6 | 24.5 / 24.5 |
| f-chase | full | 6.9 / 6.7 | 21.4 / 6.9 | 6.8 / 6.8 |
| f-chase | kernel | 6.9 / 6.7 | 19.6 / 6.9 | 6.9 / 6.8 |
| f-gather | full | 24.8 / 24.6 | 38.5 / 24.9 | 40.9 / 40.1 |
| f-gather | kernel | 24.8 / 24.6 | 36.8 / 24.8 | 24.7 / 24.8 |
| f-gups | full | 22.2 / 21.9 | 35.1 / 22.2 | 38.2 / 38.1 |
| f-gups | kernel | 22.2 / 21.9 | 35.3 / 22.2 | 22.2 / 22.2 |
| f-hist | full | 297.4 / 297.2 | 310.2 / 297.5 | 297.6 / 297.6 |
| f-hist | kernel | 297.4 / 297.2 | 312.2 / 297.5 | 297.5 / 297.5 |
| f-jacobi | full | 11.5 / 10.9 | 23.9 / 9.4 | 25.8 / 25.6 |
| f-jacobi | kernel | 11.6 / 11.1 | 24.9 / 11.1 | 11.1 / 11.2 |
| f-spmv | full | 17.2 / 16.7 | 29.8 / 16.9 | 17.9 / 17.8 |
| f-spmv | kernel | 17.2 / 16.7 | 31.2 / 16.9 | 16.9 / 16.7 |
