# build sptree
mkdir release
cd release
cmake ..
make -j

cd ..
# build cceh
cd CCEH
make -j
mv bench_cceh ../release
mv bench_cceh_spoton_di ../release

cd ..
cd FAST_FAIR
make -j
mv bench_fastfair_original ../release

cd ..
# build pactree
cd pactree
mkdir release
cd release
cmake ..
make -j

