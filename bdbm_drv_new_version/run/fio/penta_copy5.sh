#!/bin/bash

sudo fio --randrepeat=1 \
	--ioengine=libaio \
	--name=fio5\
	--filename=/media/robusta/fio5 \
	--bs=4k \
	--iodepth=128 \
	--size=500M \
	--readwrite=write \
	--rwmixwrite=100 \
	--overwrite=1 \
	--numjobs=1 \
	--direct=1 \
	--sync=0	\
	--buffered=0 \
	#--fsync=10000

	#--end_fsync=1 \
	#--fsync_on_close=1 \
