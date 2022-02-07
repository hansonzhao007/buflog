# build our code
mkdir release
cd release
cmake ..
make -j

cd ..

# build cceh
cd CCEH
make -j
mv bench_cceh_pmem ../release
