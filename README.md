RFLUSH: Rethink the Flush
=======================================
A FLUSH command has been used for decades to enforce persistence and ordering of updates in a storage device. The command forces all the data in the volatile buffer to non-volatile media to achieve persistency. This lumpsum approach to flushing has two performance consequences. First, it slows down non-volatile materialization of the writes that actually need to be flushed. Second, it deprives the writes that need not to be flushed of an opportunity for absorbing future writes and coalescing. We attempt to characterize the problems of this semantic gap of flushing in storage devices and propose RFLUSH that allows a fine-grained control over flushing in them. The RFLUSH command delivers a range of LBAs that need to be flushed and thus enables the storage device to force only a subset of data in its buffer. We implemented this fine-grained flush command in a storage device using an open-source flash development platform and modified the F2FS file system to make use of the command in processing fsync requests as a case study. Performance evaluation using the prototype implementation shows that the inclusion of RFLUSH improves the throughput by up to 5.6x; reduces the write traffic by up to 43%; and eliminates the long tail in the response time.


#### config setting
* ubuntu 16.04
* DRAM 64GB
* f2fs-tools
* bludbm capacity 38G

#### install
* make install kernel 4.7.2

<pre> 
cd linux-4.7.2
make-kpkg --initrd --revision=1.1 kernel_image -j4
cd ..
dpkg -i linux-image-4.7.2_1.1_amd64.deb
reboot
</pre>
 
* make bluedbm
<pre> 
cd devices/ramdrive
make
cd -
cd devices/ramdrive_timing
make 
cd -
cd frontend/kerenl
make
cd -
</pre>

#### run bludbm
* run bludbm
<pre>
cd frontend/kernel
./mount_f2fs_ram.sh
</pre>







