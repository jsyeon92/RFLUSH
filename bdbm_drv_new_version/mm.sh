cd devices/ramdrive; make clean; make
cd -
cd devices/ramdrive_timing; make clean; make
cd -
cd frontend/kernel; make clean; make

./penta_reset.sh

