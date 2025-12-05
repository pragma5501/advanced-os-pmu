
rm -f stream.c
rm -f stream


wget https://www.cs.virginia.edu/stream/FTP/Code/stream.c
gcc -O3 -fopenmp stream.c -o stream

rm -rf data
mkdir data
dd if=/dev/zero of=data/small.dat  bs=1M count=10     # 10MB
dd if=/dev/zero of=data/medium.dat bs=1M count=100    # 100MB
dd if=/dev/zero of=data/large.dat  bs=1M count=500    # 500MB

make
sudo rmmod ./ko/part1.ko
# sudo insmod ./ko/part1.ko
sudo rmmod ./ko/part3.ko
sudo insmod ./ko/part3.ko
cat /proc/pmu_stats   # 잘 나오면 OK
