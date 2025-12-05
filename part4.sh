make clean; make

sudo rmmod ./ko/part3.ko
sudo insmod ./ko/part3.ko

ls /proc/pmu_stats /proc/pmu_control

rm -rf bin
mkdir bin

gcc -O0 ./src/part4_random_access.c -o ./bin/random_access_phases
gcc -O0 ./src/part4_matrix.c -o ./bin/matrix_phases

python3 ./src/part4.py