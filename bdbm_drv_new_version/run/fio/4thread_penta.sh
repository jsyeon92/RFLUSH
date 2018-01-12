##fio
./penta_copy1.sh > $1/1_fio_$2.txt & \
./penta_copy2.sh > $1/2_fio_$2.txt & \
./penta_copy3.sh > $1/3_fio_$2.txt & \
./penta_seqw.sh  > $1/4_fsy_$2.txt 
#./write_bytes /media/robusta/res_time1 5242880000 4096   > log/res_time_$2.txt
