| bench | range | config | valid | hash=native | status | dispatches | range ms (median) | x native | pass0 ms | replay pass ms | SQ_WAVES per pass equal |
|---|---|---|---|---|---|---|---|---|---|---|---|
| c-chase | full | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 191.7 | - | 165.5 | 7.4 | yes |
| c-chase | full | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 204.8 | - | 163.7 | 12.4 | yes |
| c-chase | full | v2-host | 2/2 | 0/2 | REPLAYED x2 | 8 | 153.2 | - | 133.2 | 5.4 | yes |
| c-chase | kernel | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 193.4 | - | 167.0 | 7.4 | yes |
| c-chase | kernel | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 203.9 | - | 163.6 | 12.2 | yes |
| c-chase | kernel | v2-host | 2/2 | 0/2 | REPLAYED x2 | 8 | 153.7 | - | 133.3 | 5.3 | yes |
| c-gather | full | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 644.4 | - | 529.1 | 30.3 | yes |
| c-gather | full | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 671.7 | - | 524.7 | 37.1 | yes |
| c-gather | full | v2-host | 2/2 | 0/2 | REPLAYED x2 | 8 | 529.7 | - | 393.0 | 30.7 | yes |
| c-gather | kernel | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 655.9 | - | 520.2 | 31.9 | yes |
| c-gather | kernel | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 647.4 | - | 518.7 | 34.4 | yes |
| c-gather | kernel | v2-host | 2/2 | 0/2 | REPLAYED x2 | 8 | 401.3 | - | 320.0 | 18.1 | yes |
| c-gups | full | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 312.6 | - | 234.6 | 23.7 | yes |
| c-gups | full | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 321.5 | - | 229.9 | 28.3 | yes |
| c-gups | full | v2-host | 2/2 | 0/2 | REPLAYED x2 | 8 | 356.1 | - | 242.1 | 32.7 | yes |
| c-gups | kernel | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 382.1 | - | 298.0 | 24.5 | yes |
| c-gups | kernel | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 325.3 | - | 231.0 | 29.0 | yes |
| c-gups | kernel | v2-host | 2/2 | 0/2 | REPLAYED x2 | 8 | 242.0 | - | 173.0 | 20.1 | yes |
| c-hist | full | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 1509.5 | - | 598.8 | 299.6 | yes |
| c-hist | full | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 1523.9 | - | 597.3 | 304.1 | yes |
| c-hist | full | v2-host | 2/2 | 0/2 | REPLAYED x2 | 8 | 1387.6 | - | 494.7 | 293.4 | yes |
| c-hist | kernel | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 1510.7 | - | 600.0 | 299.5 | yes |
| c-hist | kernel | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 8 | 1523.6 | - | 596.8 | 303.9 | yes |
| c-hist | kernel | v2-host | 2/2 | 0/2 | REPLAYED x2 | 8 | 1387.5 | - | 494.4 | 293.5 | yes |
| c-rsbench | full | v1-packet | 2/2 | 0/2 | MEMORY_COPY_IN_RANGE x2 | 1 | 193.0 | - | 192.3 | - | - |
| c-rsbench | full | v2-packet | 2/2 | 0/2 | MEMORY_COPY_IN_RANGE x2 | 1 | 192.0 | - | 190.8 | - | - |
| c-rsbench | full | v2-host | 2/2 | 0/2 | ALLOCATION_CHANGED_IN_RANGE x2 | 2 | 191.5 | - | 190.2 | - | - |
| c-rsbench | kernel | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 1 | 424.0 | - | 223.1 | 50.5 | yes |
| c-rsbench | kernel | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 1 | 438.5 | - | 221.0 | 56.0 | yes |
| c-rsbench | kernel | v2-host | 2/2 | 0/2 | ALLOCATION_CHANGED_IN_RANGE x2 | 1 | 232.5 | - | 181.2 | - | - |
| c-spmv | full | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 16 | 348.9 | - | 283.5 | 18.1 | yes |
| c-spmv | full | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 16 | 360.1 | - | 282.1 | 22.5 | yes |
| c-spmv | full | v2-host | 2/2 | 0/2 | REPLAYED x2 | 16 | 250.6 | - | 198.7 | 13.3 | yes |
| c-spmv | kernel | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 16 | 348.3 | - | 283.1 | 18.2 | yes |
| c-spmv | kernel | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 16 | 362.1 | - | 281.1 | 22.6 | yes |
| c-spmv | kernel | v2-host | 2/2 | 0/2 | REPLAYED x2 | 16 | 242.6 | - | 193.7 | 12.5 | yes |
| c-xsbench | full | v1-packet | 2/2 | 0/2 | MEMORY_COPY_IN_RANGE x2 | 1 | 199.0 | - | 197.8 | - | - |
| c-xsbench | full | v2-packet | 2/2 | 0/2 | MEMORY_COPY_IN_RANGE x2 | 1 | 198.0 | - | 196.6 | - | - |
| c-xsbench | full | v2-host | 2/2 | 0/2 | ALLOCATION_CHANGED_IN_RANGE x2 | 2 | 197.0 | - | 195.9 | - | - |
| c-xsbench | kernel | v1-packet | 2/2 | 0/2 | REPLAYED x2 | 1 | 624.5 | - | 429.4 | 33.5 | yes |
| c-xsbench | kernel | v2-packet | 2/2 | 0/2 | REPLAYED x2 | 1 | 635.5 | - | 427.1 | 37.3 | yes |
| c-xsbench | kernel | v2-host | 2/2 | 0/2 | ALLOCATION_CHANGED_IN_RANGE x2 | 1 | 361.0 | - | 265.1 | - | - |
| f-babel-triad | kernel | v1-packet | 2/2 | - | REPLAYED x2 | 20 | 883.6 | - | 719.8 | 36.9 | yes |
| f-babel-triad | kernel | v2-packet | 2/2 | - | REPLAYED x2 | 20 | 889.5 | - | 718.2 | 40.7 | yes |
| f-babel-triad | kernel | v2-host | 2/2 | - | REPLAYED x2 | 20 | 517.8 | - | 423.1 | 17.1 | yes |
| f-babelstream | kernel | v1-packet | 2/2 | - | MEMORY_COPY_IN_RANGE x2 | 4 | 525.8 | - | 470.4 | - | - |
| f-babelstream | kernel | v2-packet | 2/2 | - | MEMORY_COPY_IN_RANGE x2 | 4 | 526.4 | - | 466.2 | - | - |
| f-babelstream | kernel | v2-host | 2/2 | - | ALLOCATION_CHANGED_IN_RANGE x2 | 100 | 525.1 | - | 468.0 | - | - |
| f-chase | full | v1-packet | 2/2 | - | REPLAYED x2 | 8 | 196.5 | - | 168.2 | 8.1 | yes |
| f-chase | full | v2-packet | 2/2 | - | REPLAYED x2 | 8 | 207.3 | - | 166.2 | 12.3 | yes |
| f-chase | full | v2-host | 2/2 | - | REPLAYED x2 | 8 | 156.3 | - | 134.5 | 5.8 | yes |
| f-chase | kernel | v1-packet | 2/2 | - | REPLAYED x2 | 8 | 197.8 | - | 169.3 | 8.2 | yes |
| f-chase | kernel | v2-packet | 2/2 | - | REPLAYED x2 | 8 | 210.0 | - | 167.4 | 12.8 | yes |
| f-chase | kernel | v2-host | 2/2 | - | REPLAYED x2 | 8 | 199.8 | - | 178.2 | 5.8 | yes |
| f-gather | full | v1-packet | 2/2 | - | REPLAYED x2 | 8 | 649.8 | - | 529.3 | 31.1 | yes |
| f-gather | full | v2-packet | 2/2 | - | REPLAYED x2 | 8 | 657.5 | - | 525.7 | 35.5 | yes |
| f-gather | full | v2-host | 2/2 | - | REPLAYED x2 | 8 | 525.7 | - | 394.5 | 31.6 | yes |
| f-gather | kernel | v1-packet | 2/2 | - | REPLAYED x2 | 8 | 641.4 | - | 523.0 | 30.5 | yes |
| f-gather | kernel | v2-packet | 2/2 | - | REPLAYED x2 | 8 | 654.6 | - | 521.9 | 35.4 | yes |
| f-gather | kernel | v2-host | 2/2 | - | REPLAYED x2 | 8 | 412.4 | - | 322.9 | 18.9 | yes |
| f-gups | full | v1-packet | 2/2 | - | REPLAYED x2 | 8 | 314.2 | - | 234.6 | 24.4 | yes |
| f-gups | full | v2-packet | 2/2 | - | REPLAYED x2 | 8 | 327.8 | - | 233.2 | 29.3 | yes |
| f-gups | full | v2-host | 2/2 | - | REPLAYED x2 | 8 | 357.1 | - | 244.2 | 33.3 | yes |
| f-gups | kernel | v1-packet | 2/2 | - | REPLAYED x2 | 8 | 309.4 | - | 229.0 | 24.7 | yes |
| f-gups | kernel | v2-packet | 2/2 | - | REPLAYED x2 | 8 | 354.9 | - | 262.6 | 28.6 | yes |
| f-gups | kernel | v2-host | 2/2 | - | REPLAYED x2 | 8 | 290.6 | - | 220.9 | 20.8 | yes |
| f-hist | full | v1-packet | 2/2 | - | REPLAYED x2 | 8 | 1524.3 | - | 609.0 | 300.9 | yes |
| f-hist | full | v2-packet | 2/2 | - | REPLAYED x2 | 8 | 1534.1 | - | 599.0 | 305.1 | yes |
| f-hist | full | v2-host | 2/2 | - | REPLAYED x2 | 8 | 1387.5 | - | 493.7 | 293.9 | yes |
| f-hist | kernel | v1-packet | 2/2 | - | REPLAYED x2 | 8 | 1515.3 | - | 602.1 | 300.2 | yes |
| f-hist | kernel | v2-packet | 2/2 | - | REPLAYED x2 | 8 | 1530.9 | - | 599.2 | 305.0 | yes |
| f-hist | kernel | v2-host | 2/2 | - | REPLAYED x2 | 8 | 1389.0 | - | 495.0 | 293.8 | yes |
| f-jacobi | full | v1-packet | 2/2 | - | REPLAYED x2 | 16 | 377.4 | - | 323.3 | 13.0 | yes |
| f-jacobi | full | v2-packet | 2/2 | - | REPLAYED x2 | 16 | 385.2 | - | 320.4 | 16.8 | yes |
| f-jacobi | full | v2-host | 2/2 | - | REPLAYED x2 | 16 | 363.6 | - | 283.5 | 18.4 | yes |
| f-jacobi | kernel | v1-packet | 2/2 | - | REPLAYED x2 | 16 | 375.4 | - | 316.4 | 14.1 | yes |
| f-jacobi | kernel | v2-packet | 2/2 | - | REPLAYED x2 | 16 | 381.1 | - | 315.2 | 17.6 | yes |
| f-jacobi | kernel | v2-host | 2/2 | - | REPLAYED x2 | 16 | 246.9 | - | 210.7 | 6.8 | yes |
| f-spmv | full | v1-packet | 2/2 | - | REPLAYED x2 | 16 | 354.4 | - | 285.3 | 18.5 | yes |
| f-spmv | full | v2-packet | 2/2 | - | REPLAYED x2 | 16 | 364.1 | - | 284.1 | 22.9 | yes |
| f-spmv | full | v2-host | 2/2 | - | REPLAYED x2 | 16 | 253.8 | - | 200.8 | 13.7 | yes |
| f-spmv | kernel | v1-packet | 2/2 | - | REPLAYED x2 | 16 | 351.6 | - | 284.6 | 18.6 | yes |
| f-spmv | kernel | v2-packet | 2/2 | - | REPLAYED x2 | 16 | 365.2 | - | 283.4 | 23.7 | yes |
| f-spmv | kernel | v2-host | 2/2 | - | REPLAYED x2 | 16 | 245.0 | - | 194.5 | 12.8 | yes |

| bench | range | v1 packet dispatches | v2 packet dispatches | v2 host dispatches | SQ_WAVES v1 / v2 packet / v2 host |
|---|---|---|---|---|---|
| c-chase | full | [8] | [8] | [8] | [8192] / [8192] / [8192] |
| c-chase | kernel | [8] | [8] | [8] | [8192] / [8192] / [8192] |
| c-gather | full | [8] | [8] | [8] | [2097150.0] / [2097150.0] / [2097150.0] |
| c-gather | kernel | [8] | [8] | [8] | [2097150.0] / [2097150.0] / [2097150.0] |
| c-gups | full | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| c-gups | kernel | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| c-hist | full | [8] | [8] | [8] | [8388610.0] / [8388610.0] / [8388610.0] |
| c-hist | kernel | [8] | [8] | [8] | [8388610.0] / [8388610.0] / [8388610.0] |
| c-rsbench | full | [1] | [1] | [2] | [] / [] / [] |
| c-rsbench | kernel | [1] | [1] | [1] | [4864] / [4864] / [] |
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
| f-gups | kernel | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| f-hist | full | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| f-hist | kernel | [8] | [8] | [8] | [38912] / [38912] / [38912] |
| f-jacobi | full | [16] | [16] | [16] | [77824] / [77824] / [77824] |
| f-jacobi | kernel | [16] | [16] | [16] | [77824] / [77824] / [77824] |
| f-spmv | full | [16] | [16] | [16] | [77824] / [77824] / [77824] |
| f-spmv | kernel | [16] | [16] | [16] | [77824] / [77824] / [77824] |

Condensed: median range wall ms (app-measured, all passes); copy-decl / alloc-decl = declined, ran once

| bench | range | native | v1 packet | v2 packet | v2 host | v2 host, pool off |
|---|---|---|---|---|---|---|
| c-chase | full | - | 191.7 | 204.8 | 153.2 | - |
| c-chase | kernel | - | 193.4 | 203.9 | 153.7 | - |
| c-gather | full | - | 644.4 | 671.7 | 529.7 | - |
| c-gather | kernel | - | 655.9 | 647.4 | 401.3 | - |
| c-gups | full | - | 312.6 | 321.5 | 356.1 | - |
| c-gups | kernel | - | 382.1 | 325.3 | 242.0 | - |
| c-hist | full | - | 1509.5 | 1523.9 | 1387.6 | - |
| c-hist | kernel | - | 1510.7 | 1523.6 | 1387.5 | - |
| c-rsbench | full | - | 193.0 copy-decl | 192.0 copy-decl | 191.5 alloc-decl | - |
| c-rsbench | kernel | - | 424.0 | 438.5 | 232.5 alloc-decl | - |
| c-spmv | full | - | 348.9 | 360.1 | 250.6 | - |
| c-spmv | kernel | - | 348.3 | 362.1 | 242.6 | - |
| c-xsbench | full | - | 199.0 copy-decl | 198.0 copy-decl | 197.0 alloc-decl | - |
| c-xsbench | kernel | - | 624.5 | 635.5 | 361.0 alloc-decl | - |
| f-babel-triad | kernel | - | 883.6 | 889.5 | 517.8 | - |
| f-babelstream | kernel | - | 525.8 copy-decl | 526.4 copy-decl | 525.1 alloc-decl | - |
| f-chase | full | - | 196.5 | 207.3 | 156.3 | - |
| f-chase | kernel | - | 197.8 | 210.0 | 199.8 | - |
| f-gather | full | - | 649.8 | 657.5 | 525.7 | - |
| f-gather | kernel | - | 641.4 | 654.6 | 412.4 | - |
| f-gups | full | - | 314.2 | 327.8 | 357.1 | - |
| f-gups | kernel | - | 309.4 | 354.9 | 290.6 | - |
| f-hist | full | - | 1524.3 | 1534.1 | 1387.5 | - |
| f-hist | kernel | - | 1515.3 | 1530.9 | 1389.0 | - |
| f-jacobi | full | - | 377.4 | 385.2 | 363.6 | - |
| f-jacobi | kernel | - | 375.4 | 381.1 | 246.9 | - |
| f-spmv | full | - | 354.4 | 364.1 | 253.8 | - |
| f-spmv | kernel | - | 351.6 | 365.2 | 245.0 | - |

| bench | range | v1 packet pass1 / pass2 ms | v2 packet pass1 / pass2 ms | v2 host pass1 / pass2 ms |
|---|---|---|---|---|
| c-chase | full | 6.2 / 6.0 | 20.8 / 6.2 | 6.1 / 6.1 |
| c-chase | kernel | 6.2 / 6.0 | 20.1 / 6.2 | 6.1 / 6.0 |
| c-gather | full | 22.1 / 21.9 | 35.0 / 22.2 | 38.6 / 38.3 |
| c-gather | kernel | 22.1 / 21.9 | 33.4 / 22.2 | 22.2 / 22.1 |
| c-gups | full | 21.4 / 21.1 | 35.2 / 21.3 | 37.7 / 37.5 |
| c-gups | kernel | 21.5 / 21.2 | 34.4 / 21.4 | 21.4 / 21.3 |
| c-hist | full | 295.4 / 295.1 | 307.8 / 295.8 | 295.5 / 295.5 |
| c-hist | kernel | 295.3 / 295.1 | 308.1 / 295.5 | 295.9 / 295.5 |
| c-rsbench | kernel | 49.6 / 48.3 | 65.2 / 48.8 | - |
| c-spmv | full | 14.6 / 14.2 | 27.4 / 14.3 | 15.4 / 15.3 |
| c-spmv | kernel | 14.6 / 14.1 | 27.4 / 14.4 | 14.3 / 14.3 |
| c-xsbench | kernel | 25.5 / 25.5 | 36.3 / 25.8 | - |
| f-babel-triad | kernel | 23.3 / 22.7 | 29.6 / 23.0 | 23.1 / 23.0 |
| f-chase | full | 6.9 / 6.7 | 19.3 / 6.9 | 6.8 / 6.8 |
| f-chase | kernel | 6.9 / 6.7 | 20.7 / 6.8 | 6.9 / 6.7 |
| f-gather | full | 23.1 / 22.8 | 35.7 / 23.1 | 39.9 / 39.6 |
| f-gather | kernel | 23.1 / 22.9 | 35.8 / 23.2 | 23.4 / 23.0 |
| f-gups | full | 22.2 / 22.0 | 34.2 / 22.2 | 38.6 / 38.4 |
| f-gups | kernel | 22.2 / 22.1 | 34.5 / 22.2 | 22.2 / 22.2 |
| f-hist | full | 296.0 / 295.7 | 308.8 / 296.0 | 296.1 / 296.1 |
| f-hist | kernel | 296.3 / 295.7 | 309.6 / 296.1 | 296.1 / 296.1 |
| f-jacobi | full | 9.5 / 9.0 | 21.6 / 9.0 | 24.1 / 23.9 |
| f-jacobi | kernel | 9.5 / 9.0 | 21.9 / 9.0 | 9.2 / 9.0 |
| f-spmv | full | 15.2 / 14.7 | 28.2 / 14.9 | 16.1 / 15.9 |
| f-spmv | kernel | 15.2 / 14.7 | 29.4 / 15.0 | 14.9 / 14.9 |
