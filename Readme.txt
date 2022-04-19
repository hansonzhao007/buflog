# Software version 

Fast&Fair  https://github.com/DICL/FAST_FAIR.git
branch: 0f047e8

CCEH https://github.com/DICL/CCEH
branch: 46771e3

skiplist https://github.com/facebook/rocksdb/blob/main/memtable/inlineskiplist.h
branch: fdf882d

pactree https://github.com/cosmoss-vt/pactree
branch: f173a0f

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

