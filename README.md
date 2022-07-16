# Software version 

Fast&Fair  https://github.com/DICL/FAST_FAIR.git
branch: 0f047e8

CCEH https://github.com/DICL/CCEH
branch: 46771e3

skiplist https://github.com/facebook/rocksdb/blob/main/memtable/inlineskiplist.h
branch: fdf882d

pactree https://github.com/cosmoss-vt/pactree
branch: f173a0f

# Download the submodules

Use following command to download all submodules during git clone.

```
git clone --recurse-submodules `repo address`
```

# Folder

`CCEH`: Source code of cceh

`FAST_FAIR`: Source code of fast&fair

`Skiplist`: Source code of Skiplist

`data`: all the data in the paper. Go to the sub-folders and run `bash analysis_plot.sh` to re-produce the figures.

`lib`: Third-party libraries.

`spoton-tree`: SPTree source code.

`src`: Source code for case studies.

`testbench`: Benchmarks used in the paper.

# configure PMEM 

mount the pmem device to /mnt/pmem directory. create two folder under /mnt/pmem then link them as pmem0 and pmem1

```
mkdir /mnt/pmem
mkdir /mnt/pmem/numa0
mkdir /mnt/pmem/numa1
ln -s /mnt/pmem/numa0 /mnt/pmem0
ln -s /mnt/pmem/numa1 /mnt/pmem1
```

# install packages

```
sudo apt install libgtest-dev clang-10 clang gcc-10 g++-10
sudo apt reinstall g++
```

# compile the code

run the following command to build all binaries.

```
bash build.sh
```


