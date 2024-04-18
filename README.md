Code for the paper "Distance based prefetching algorithms for mining of the sporadic requests associations".

# DBSP


This library contains an implementation of the following prefetching algorithms: DBSP.

Library contents:

| Library Component | Description |
| ---- | --- |
| Data prefetching algorithms | DBSP |
| 
| LRU Simulator | Required for measuring target metrics. |

The following usage scenarios for the library are assumed:
- Conducting measurements on a ready-made cache implementation together with prefetching algorithms. (Required for obtaining target metrics.)


# Library Assembly

## Installing Dependencies
```sh
sudo apt-get install -y cmake libboost-all-dev libglib2.0-dev g++
```
## Assembly
```sh
  mkdir -p build && cd build
  cmake ..
  make -j8
```



## Manual Testing

Manual testing involves running the benchmark.cpp file from the examples folder.

Command line arguments:
- ``--cache`` - cache size in bytes; (Default value is 200 MB.)
- ``--block`` - block size in bytes;
- ``--predictor`` - algorithm for predicting future associations;
- ``--prefetch`` - algorithm's working policy;
- ``--rtable size`` - number of rows in the record table of the Mithril algorithm;
- ``--ptable_size`` - number of rows in the found associations table of the Mithril algorithm.
```sh
./examples/benchmark --i <path to csv file> --cache 1048576 --shards 2 --page 4096 --block 512
```


## References
<a id="1">[1]</a> 
Yang J. et al. Mithril: mining sporadic associations for cache prefetching //Proceedings of the 2017 Symposium on Cloud Computing. – 2017. – С. 66-79.