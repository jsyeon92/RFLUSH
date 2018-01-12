./umount.sh

dmesg -C

echo 1 > /proc/sys/vm/drop_caches

./mount_f2fs_ram.sh
#./umount.sh
