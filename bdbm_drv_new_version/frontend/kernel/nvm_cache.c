/*
The MIT License (MIT)

Copyright (c) 2014-2015 CSAIL, MIT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <linux/module.h> /* uint64_t */

#include "bdbm_drv.h"
#include "debug.h"
#include "params.h"
#include "umemory.h"
#include "blkio.h"
//#include "abm.h"
#include "ufile.h"
#include "utime.h"

#ifdef RFLUSH
#include <linux/bio.h>
#include <linux/delay.h>
#endif

#include "nvm_cache.h"
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
//#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

/* interface for nvm_dev */
bdbm_nvm_inf_t _nvm_dev = {
	.ptr_private = NULL,
	.create = bdbm_nvm_create,
	.destroy = bdbm_nvm_destroy,
	.make_req = bdbm_nvm_make_req,
	.rflush = bdbm_nvm_rflush_data,
	.flush = bdbm_nvm_flush_data,
//	.end_req = nvm_end_req,
};

static struct timeval start;
static LIST_HEAD(sync_time_head);
uint32_t time_us (void)
{
	struct timeval tv;
	uint32_t sec=0;
	int32_t usec=0;
	do_gettimeofday(&tv);
	sec = tv.tv_sec-start.tv_sec;
	usec = tv.tv_usec-start.tv_usec;
	if(sec==0)
		return usec;
	else 
		return sec * 1000000 + usec;
}


static void __display_hex_values (uint8_t* host, uint8_t* back)
{
	bdbm_msg (" * HOST: %x %x %x %x %x != FLASH: %x %x %x %x %x", 
		host[0], host[1], host[2], host[3], host[4], 
		back[0], back[1], back[2], back[3], back[4]);
}
static void __display_hex_values_all (uint8_t* host, uint8_t* back)
{
	int i = 0;
	for (i = 0; i < KPAGE_SIZE; i+=4) {
		bdbm_msg (" * HOST: %x %x %x %x != FLASH: %x %x %x %x", 
			host[i+0], host[i+1], host[i+2], host[i+3],
			back[i+0], back[i+1], back[i+2], back[i+3]);
	}
}
static void __display_hex_values_all_host (uint8_t* host)
{
	int i = 0;
	for (i = 0; i < KPAGE_SIZE; i+=4) {
		bdbm_msg (" * HOST: %x %x %x %x", 
			host[i+0], host[i+1], host[i+2], host[i+3]);
	}
}
static void __display_hex_values_all_range (uint8_t* host, uint8_t* back, int size)
{
	int i = 0;
	for (i = 0; i < size; i+=4) {
		bdbm_msg (" * HOST: %x %x %x %x != FLASH: %x %x %x %x", 
			host[i+0], host[i+1], host[i+2], host[i+3],
			back[i+0], back[i+1], back[i+2], back[i+3]);
	}
}
static void __display_hex_values_all_host_range (uint8_t* host, int size)
{
	int i = 0;
	for (i = 0; i < size; i+=4) {
		bdbm_msg (" * HOST: %x %x %x %x", 
			host[i+0], host[i+1], host[i+2], host[i+3]);
	}
}


static void* __nvm_alloc_nvmram (bdbm_device_params_t* ptr_np) 
{
	void* ptr_nvmram = NULL;
	
	uint64_t page_size_in_bytes = ptr_np->nvm_page_size; 
	uint64_t nvm_size_in_bytes;

	nvm_size_in_bytes = 
		page_size_in_bytes * ptr_np->nr_nvm_pages;

	if((ptr_nvmram = (void*) bdbm_malloc
		(nvm_size_in_bytes * sizeof(uint8_t))) == NULL) {
		bdbm_error("bdbm_malloc failed (nvm size = %llu bytes)", nvm_size_in_bytes);
		return NULL;
	}
	bdbm_memset ((uint8_t*) ptr_nvmram, 0x00, nvm_size_in_bytes * sizeof (uint8_t));
	bdbm_msg("nvm cache addr = %p", ptr_nvmram);

	return (void*) ptr_nvmram;

}


static void* __nvm_alloc_nvmram_tbl (bdbm_device_params_t* np) 
{
	bdbm_nvm_page_t* me; 
	uint64_t i, j;

	/* allocate mapping entries */
	if ((me = (bdbm_nvm_page_t*) bdbm_zmalloc 
		(sizeof (bdbm_nvm_page_t) * np->nr_nvm_pages)) == NULL) {
		return NULL;
	}

	/* initialize a mapping table */
	for (i = 0; i < np->nr_nvm_pages; i++){
		// index 
		me[i].index = i;
///jsyeon
		me[i].status = CLEAR;
		// logaddr 
		me[i].logaddr.ofs = -1;
		for (j = 0; j < np->nr_subpages_per_page; j ++){
			me[i].logaddr.lpa[j] = -1;
		}
	}

	return me;
}

static void* __lba_cnt_alloc(bdbm_device_params_t* np) 
{
	struct lba_struct* me; 
	uint64_t i;

	/* allocate mapping entries */
	if ((me = (struct lba_struct*) bdbm_zmalloc 
		(sizeof (struct lba_struct) * np->nr_subpages_per_ssd)) == NULL) {
		return NULL;
	}

	/* initialize a mapping table */
	for (i = 0; i < np->nr_subpages_per_ssd; i++){
		// index 
#ifdef	RFLUSH
		me[i].cnt = 0;
#endif
	}

	return me;
}
static void* __nvm_alloc_nvmram_lookup_tbl (bdbm_device_params_t* np) 
{
	bdbm_nvm_lookup_tbl_entry_t* me; 
	uint64_t i;

	/* allocate mapping entries */
	if ((me = (bdbm_nvm_lookup_tbl_entry_t*) bdbm_zmalloc 
		(sizeof (bdbm_nvm_lookup_tbl_entry_t) * np->nr_subpages_per_ssd)) == NULL) {
		return NULL;
	}

	/* initialize a mapping table */
	for (i = 0; i < np->nr_subpages_per_ssd; i++){
		// index 
		me[i].tbl_idx = -1;
#ifdef	RFLUSH
		me[i].ptr_page = NULL;
#endif
	}

	return me;
}




uint32_t bdbm_nvm_create (bdbm_drv_info_t* bdi){
	
	bdbm_nvm_dev_private_t* p = NULL;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	uint64_t i;

	if (!bdi->ptr_nvm_inf)
		return 1;

	/* create a private data structure */
	if ((p = (bdbm_nvm_dev_private_t*)bdbm_zmalloc 
			(sizeof (bdbm_nvm_dev_private_t))) == NULL) {
		bdbm_error ("bdbm_malloc failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	/* assign p into ptr_private to destroy it properly upon a fail */
	_nvm_dev.ptr_private = (void*)p;

	p->nr_total_pages = np->nr_nvm_pages;
	p->nr_free_pages = p->nr_total_pages;
	p->nr_inuse_pages = 0;
	p->np = np;

	p->nr_total_access = 0;
	p->nr_total_write = 0;
	p->nr_total_read = 0;
	p->nr_write = 0;
	p->nr_nh_write = 0;
	p->nr_read = 0;	
	p->nr_nh_read = 0;	
	p->nr_total_hit = 0;
	p->nr_evict = 0;


	/* alloc ptr_nvmram_data: ptr_nvm_data */
	if((p->ptr_nvmram = __nvm_alloc_nvmram (np)) == NULL) {
		bdbm_error ("__alloc_nvmram failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	/* alloc page table: ptr_nvm_tbl */	
	if((p->ptr_nvm_tbl = __nvm_alloc_nvmram_tbl (np)) == NULL) {
		bdbm_error ("__alloc_nvmram table failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	/* alloc lookup page tabl: ptr_nvm_lookup_tbl */
	if((p->ptr_nvm_lookup_tbl = __nvm_alloc_nvmram_lookup_tbl (np)) == NULL) {
		bdbm_error ("__alloc_nvmram table failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	/* initialize lock and list */
	bdbm_sema_init (&p->nvm_lock);

	if((p->lru_list = (struct list_head*) bdbm_zmalloc (sizeof(struct list_head))) == NULL) {
		bdbm_error ("__alloc nvmram lru_list failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}
	if((p->free_list = (struct list_head*) bdbm_zmalloc (sizeof(struct list_head))) == NULL) {
		bdbm_error ("__alloc nvmram free_list failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}
//jsyeon0111
	if((p->hash_list = (struct list_head*) bdbm_zmalloc (sizeof(struct list_head))) == NULL) {
		bdbm_error ("__alloc nvmram clear_list failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	if((p->lba_struct  = __lba_cnt_alloc(np)) == NULL) {
		bdbm_error ("__alloc_nvmram table failed");
		bdbm_nvm_destroy(bdi);
		return 1;
	}

	INIT_LIST_HEAD(p->lru_list);
	INIT_LIST_HEAD(p->free_list);
	INIT_LIST_HEAD(p->hash_list);
	//jsyeon
	do_gettimeofday(&start);
	/* add all pages into free list */
	for (i=0; i<p->nr_total_pages; i++)
		list_add (&p->ptr_nvm_tbl[i].list, p->free_list);		
	//jsyeon
	evict_cache_pages=(1-cache_threshold)*p->nr_total_pages;
	bdbm_msg("==========================================================");
	bdbm_msg("NVM CONFIGURATION");
	bdbm_msg("==========================================================");
	bdbm_msg("total size = %llu, nr_nvm_pages = %llu, nvm_page_size	= %llu",
		np->nr_nvm_pages * np->nvm_page_size, np->nr_nvm_pages, np->nvm_page_size);

	return 0;
}
uint32_t file_write(bdbm_drv_info_t* bdi, const char* fn)
{
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	/*struct file* fp = NULL;*/
	bdbm_file_t fp = 0;
	uint64_t i, pos = 0;
	char bufs[100];
	int size=0;
	struct time_entry *a;
	struct time_entry *time_entry;

	if ((fp = bdbm_fopen (fn,  O_CREAT | O_WRONLY, 0777)) == 0) {
		bdbm_error ("bdbm_fopen failed");
		return 1;
	}

	list_for_each_entry(a, &sync_time_head, list){
		size = sprintf(bufs, "%d %d\n", a->time, a->type);
		pos +=bdbm_fwrite(fp, pos, bufs, size);
	}
	list_for_each_entry_safe(time_entry, a, &sync_time_head, list){
		list_del(&time_entry->list);
		kfree(time_entry);
	}

/*
	for(i=0; i < np->nr_subpages_per_ssd; i++){
		size=sprintf(bufs, "%d\n", p->lba_struct[i].cnt);
		pos +=bdbm_fwrite(fp, pos, bufs,size);
	}
*/

	bdbm_fclose (fp);

	return 0;
}


void bdbm_nvm_destroy (bdbm_drv_info_t* bdi)
{

	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;  
	
	file_write(bdi, "/home/js/time");

	printk(KERN_NOTICE "[bdbm]total_write_traffic  : %lu\n", total_write_traffic);
	printk(KERN_NOTICE "[bdbm]evict_write_traffic  : %lu\n", evict_write_traffic);
	printk(KERN_NOTICE "[bdbm]rflush_write_traffic : %lu\n", rflush_write_traffic);
	printk(KERN_NOTICE "[bdbm]flush_write_traffic  : %lu\n", flush_write_traffic);
	printk(KERN_NOTICE "[bdbm]total_write_count: %lu\n", p->nr_total_write);
	printk(KERN_NOTICE "[bdbm]evict_count      : %lu\n", evict_cnt);
	printk(KERN_NOTICE "[bdbm]buffering_count  : %lu\n", p->nr_write);
	printk(KERN_NOTICE "[bdbm]buffering_count2 : %lu\n", atomic64_read(&bdi->pm.nvm_wh_cnt));
	printk(KERN_NOTICE "[bdbm]rflush_count     : %lu\n", rflush_cnt);
	printk(KERN_NOTICE "[bdbm]flush _count     : %lu\n", flush_cnt);
	printk(KERN_NOTICE "[bdbm]rflush_num       : %lu\n", rflush_num);
	printk(KERN_NOTICE "[bdbm]flush _num       : %lu\n", flush_num);
	printk(KERN_NOTICE "[bdbm]max_evict_flush_num  :        : %lu\n", max_evict_flush_num);
	printk(KERN_NOTICE "[bdbm]max_evict_rflush_num :        : %lu\n", max_evict_rflush_num);

	printk(KERN_NOTICE "[bdbm]gc inocation     : %ld\n", atomic64_read(&bdi->pm.gc_cnt));
	printk(KERN_NOTICE "[bdbm]block erase      : %ld\n", atomic64_read(&bdi->pm.gc_erase_cnt));
	printk(KERN_NOTICE "[bdbm]yield_cnt        : %ld\n", atomic64_read(&bdi->pm.yield_cnt));


	if(!p)
		return;

	if(p->ptr_nvmram) {
		bdbm_free (p->ptr_nvmram);
	}

	if(p->ptr_nvm_tbl) {
		bdbm_free (p->ptr_nvm_tbl);
	}
	
	if(p->lru_list) {
		bdbm_free (p->lru_list);
	}

	if(p->free_list) {
		bdbm_free (p->free_list);
	}
	if(p->lba_struct){
		bdbm_free (p->lba_struct);
	}


	bdbm_free(p);

	return;
}

int64_t bdbm_nvm_find_data (bdbm_drv_info_t* bdi, bdbm_llm_req_t* lr)
{ 
	/* search nvm cache */
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_nvm_page_t* nvm_tbl = p->ptr_nvm_tbl;
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;
	uint64_t i = 0;
	int64_t found = -1;
	int64_t lpa;

	lpa = lr->logaddr.lpa[0];

	found = nvm_lookup_tbl[lpa].tbl_idx; 

#if 0
	for(i = 0; i < p->nr_total_pages; i++){
//		bdbm_msg("nvm_tbl[%llu] lpa = %d, tlpa = %d", i, nvm_tbl[i].logaddr.lpa[0], lpa);
		if(nvm_tbl[i].logaddr.lpa[0] == lpa){
//			bdbm_msg("hit: lpa = %llu", lpa);
			p->nr_total_hit++;
			atomic64_inc(&bdi->pm.nvm_h_cnt);
			found = i;	
			break;
		}
	}
#endif
	return found;
}
#ifdef FLUSH
uint64_t bdbm_nvm_flush_data (bdbm_drv_info_t* bdi, uint64_t ino)
{ 
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;	// have lists lru_list and free_list
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;
	bdbm_hlm_req_t* hr = NULL;
	bdbm_nvm_page_t* fpage = NULL;
	bdbm_nvm_page_t* fpage_temp = NULL;
	uint8_t* fdata_ptr = NULL;
	int64_t findex = -1;
	int64_t flpa = -1;
	uint32_t fsize =0;	
	uint32_t loop =0;

	uint32_t hit =0;
	uint32_t avg=0;
	uint64_t temp=0;
	uint32_t cnt=1;
	uint32_t i=0;

	LIST_HEAD(fw_list);
	struct fw_entry * fw_entry;
	struct fw_entry * temp_entry;
	uint32_t flag=0;
	list_for_each_entry_safe(fpage, fpage_temp, p->lru_list, list){	
		if(fpage->status == CLEAR)
			continue;
		findex = fpage->index;
		fdata_ptr = p->ptr_nvmram + (findex * np->nvm_page_size);
		flpa = fpage->logaddr.lpa[0];
		fsize = fpage->size;
		/* get a free hlm_req from the hlm_reqs_pool */

		if ((hr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL) {
			bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
			goto fail;
		}
	
		/* build hlm_req with nvm_info */
		if (bdbm_hlm_reqs_pool_build_wb_req (hr, &fpage->logaddr, fdata_ptr) != 0) {
			bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
			goto fail;
		}

		/* send req */
		bdbm_sema_lock (&bp->host_lock);
		if(bdi->ptr_hlm_inf->make_wb_req (bdi, hr) != 0) {
			bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
		}
		bdbm_sema_unlock (&bp->host_lock);
		flush_write_traffic += fsize;
		fpage->status=CLEAR;
		fsize =0;
		loop++;
		if(loop >= max_evict_flush_num)
			max_evict_flush_num=loop;
	}
	flush_cnt += loop;	
	flush_num++;
#ifdef penta
	if(ino !=0)
		user_flush_cnt++;
	else
		sys_flush_cnt++; 
		
#endif
	return 1; 

fail:
	if (hr)
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, hr);
	bdbm_bug_on(1);

	return -1;
}
#endif

#ifdef	FLUSH_msjung
uint64_t bdbm_nvm_flush_data (bdbm_drv_info_t* bdi, uint64_t ino)
{ 
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;	// have lists lru_list and free_list
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;
	bdbm_hlm_req_t* hr = NULL;
	bdbm_nvm_page_t* fpage = NULL;
	uint8_t* fdata_ptr = NULL;
	int64_t findex = -1;
	int64_t flpa = -1;
	uint32_t fsize = 0;

	uint32_t loop =0;
	uint32_t hit =0;
	uint32_t avg=0;
	uint64_t temp=0;
	uint32_t cnt=1;
	uint32_t i=0;

	if(ino !=0)
		printk(KERN_NOTICE "[bdbm_%lu] ----------------------------------\n", user_flush_cnt,ino);
	if(ino !=0)
		printk(KERN_NOTICE "[bdbm_%lu] flush-ino : %lu\n", user_flush_cnt,ino);
	
//	printk(KERN_NOTICE "[bdbm]FLUSH while start\n");
	while (!list_empty(p->lru_list)) {
		bdbm_bug_on(list_empty(p->lru_list));
		fpage = list_last_entry(p->lru_list, bdbm_nvm_page_t, list);
		if(fpage->stats == CLEAR)
			continue;
		bdbm_bug_on(fpage == NULL);
		findex = fpage->index;
		fdata_ptr = p->ptr_nvmram + (findex * np->nvm_page_size);
		flpa = fpage->logaddr.lpa[0];
		
		if(ino !=0){
			if(ino== fpage->ino)
				hit++;
			if(temp == fpage->ino){
				cnt++;
			}else if(loop != 0){
				printk(KERN_NOTICE "[bdbm_%lu] %lu : %lu \n", user_flush_cnt, temp, cnt);
				temp= fpage->ino;
				cnt =1;
			}else {
				temp = fpage->ino;
			}
			//printk(KERN_NOTICE "[bdbm_%lu] %lu\n",flush_cnt, fpage->ino);
			loop++;
		}
		/* get a free hlm_req from the hlm_reqs_pool */
		if ((hr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL) {
			bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
			goto fail;
		}
	
		/* build hlm_req with nvm_info */
		if (bdbm_hlm_reqs_pool_build_wb_req (hr, &fpage->logaddr, fdata_ptr) != 0) {
			bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
			goto fail;
		}

		/* send req */
		bdbm_sema_lock (&bp->host_lock);

		if(bdi->ptr_hlm_inf->make_wb_req (bdi, hr) != 0) {
			bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
		}

		bdbm_sema_unlock (&bp->host_lock);
/*	
		nvm_lookup_tbl[flpa].tbl_idx = -1;
		nvm_lookup_tbl[flpa].ptr_page = NULL;
	
		list_del(&fpage->list);
		p->nr_inuse_pages--;
		list_add(&fpage->list, p->free_list);
		p->nr_free_pages++;
*/
		flush_write_traffic += fsize;
//		printk(KERN_NOTICE "[bdbm_flush ing %lu]\n", flush_write_traffic);
		fsize=0;
	}
	
	printk(KERN_INFO "[bdbm]----------------------------------\n", ino);
	if(ino !=0){//thread flush eception
		printk(KERN_NOTICE "[bdbm_%lu] %lu : %lu \n", user_flush_cnt, temp, cnt);
		printk(KERN_NOTICE "[flush_cnt] %lu\n", loop);
		if(hit !=0){
			avg = (hit * 1000 / loop* 1000) / 10000;
			printk(KERN_NOTICE "[hit_ratio] %lu\n",avg);
			sum += avg;
			if(sum !=0){
				avg=(sum * 1000 / user_flush_cnt * 1000) /1000000;
				printk(KERN_NOTICE "[flush_write_avg] %lu", avg);
			}
		}else{
			printk(KERN_NOTICE "[flush_write_hit_ratio] 0\n");
		}
		hit_addr[avg]++;
/*		i=hit_addr[avg];
		i++;
		hit_addr[avg]=i;
*/
/*
		for( i=0; i < 101 ; i++)
		{
			printk(KERN_NOTICE "%lu ", hit_addr[i]);
		}
*/
		printk(KERN_NOTICE"\n");
	}else{
		printk(KERN_NOTICE "[sys_flush %lu]", sys_flush_cnt);
		printk(KERN_NOTICE "\n");
	}
	printk(KERN_NOTICE "[bdbm]total write : %lu\n", p->nr_total_write);

	//printk(KERN_NOTICE "[bdbm]flush_write_traffic : %lu\n", flush_write_traffic);
#ifdef penta
	if(ino !=0)
		user_flush_cnt++;
	else
		sys_flush_cnt++; 
		
#endif
	return 1; 

fail:
	if (hr)
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, hr);
	bdbm_bug_on(1);

	return -1;
}
#endif

uint64_t bdbm_nvm_read_data (bdbm_drv_info_t* bdi, bdbm_llm_req_t* lr)
{ 
	/* search nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	int64_t nvm_idx = -1;
	uint8_t* ptr_nvmram_addr = NULL; 

	p->nr_total_read++;
	atomic64_inc(&bdi->pm.nvm_r_cnt);

	/* search data */
	nvm_idx = bdbm_nvm_find_data(bdi, lr);

	if(nvm_idx < 0){
//		bdbm_msg("not found in nvm");
		return 0; // not found 
	}

	p->nr_read++;
	atomic64_inc(&bdi->pm.nvm_rh_cnt);

	/* get data addr */
	bdbm_bug_on(np->nr_subpages_per_page != 1);	
	bdbm_bug_on(np->page_main_size / KERNEL_PAGE_SIZE != 1);
	bdbm_bug_on(np->page_main_size != np->nvm_page_size);

	ptr_nvmram_addr = p->ptr_nvmram + (nvm_idx * np->nvm_page_size); 

	//bdbm_msg("nvm_idx =%d, ptr_nvm = %p, dst_addr= %p", 
	//	nvm_idx, ptr_nvmram_addr, lr->fmain.kp_ptr[0]);

	bdbm_bug_on(ptr_nvmram_addr == NULL);

	/* copy data */
	bdbm_bug_on(!lr);
	bdbm_bug_on(!lr->fmain.kp_ptr[0]);

	bdbm_memcpy(lr->fmain.kp_ptr[0], ptr_nvmram_addr, KERNEL_PAGE_SIZE);

	/* update lr req's status */
	lr->serviced_by_nvm = 1;
	return 1;
}

static int64_t bdbm_nvm_alloc_slot (bdbm_drv_info_t* bdi, bdbm_llm_req_t* lr)
{
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;

	bdbm_nvm_page_t* npage = NULL;
	bdbm_nvm_page_t* epage = NULL;
	uint8_t* edata_ptr = NULL;
	bdbm_hlm_req_t* hr = NULL;
	int64_t nindex = -1;
	int64_t eindex = -1;
	uint64_t esize = 0;
	int64_t elpa = -1;
	int64_t lpa = -1;
	uint32_t i=0;
#ifdef NVM_CACHE_DEBUG
	uint8_t* ptr_ramssd_data = NULL; 
#endif
#ifdef hash
	bdbm_nvm_hash_t* hash_ino_node=NULL;
	uint8_t hash_hit=0;
#endif

	lpa = lr->logaddr.lpa[0];

	/* get a free page */
	if(p->nr_free_pages > 0){
//		bdbm_msg("get a free nvm buffer");
		bdbm_bug_on(list_empty(p->free_list));
		npage = list_last_entry(p->free_list, bdbm_nvm_page_t, list); 
		bdbm_bug_on(npage == NULL);
		nindex = npage->index;
		p->nr_free_pages --;
		p->nr_inuse_pages ++;
		list_del(&npage->list);
		list_add(&npage->list, p->lru_list);
		nvm_lookup_tbl[lpa].tbl_idx = nindex;
#ifdef	RFLUSH
		nvm_lookup_tbl[lpa].ptr_page = npage;
#endif
#ifdef hash
		if(list_empty(p->hash_list)){//hash list empty
			//ino list node insert
			hash_ino_node=(bdbm_nvm_hash_t*)bdbm_zmalloc(sizeof(bdbm_nvm_hash_t));
			hash_ino_node->ino=lr->ino;
			hash_ino_node->cnt=1;
			INIT_LIST_HEAD(&hash_ino_node->ino_head);
			list_add(&npage->ino_list, &hash_ino_node->ino_head);//page insert in ino_list
			list_add(&hash_ino_node->list , p->hash_list);//hash list ino node insert
		}else{
			list_for_each_entry(hash_ino_node, p->hash_list, list){
				if(hash_ino_node->ino==lr->ino){//if ino list exist
					list_add(&npage->ino_list, &hash_ino_node->ino_head);
					hash_ino_node->cnt++;
					hash_hit=1;
					break;
				}
			}
			if(hash_hit==0){
				hash_ino_node=(bdbm_nvm_hash_t*)bdbm_zmalloc(sizeof(bdbm_nvm_hash_t));
				hash_ino_node->ino=lr->ino;
				hash_ino_node->cnt=1;
				INIT_LIST_HEAD(&hash_ino_node->ino_head);
				list_add(&npage->ino_list, &hash_ino_node->ino_head);//page insert in ino_list
				list_add(&hash_ino_node->list , p->hash_list);//hash list ino node insert
			}
		}
#endif
	}
	else { 
		for(i=0; i< 1 ; i++){
//			bdbm_error("evict start\n");
			// eviction is needed 
			p->nr_evict++;
			p->nr_inuse_pages--;
			p->nr_free_pages++;
			atomic64_inc(&bdi->pm.nvm_ev_cnt);
//			bdbm_bug_on(!list_empty(p->free_list));
//			bdbm_bug_on(list_empty(p->lru_list));
//jsyeon0111			
			epage = list_last_entry(p->lru_list, bdbm_nvm_page_t, list);
//			bdbm_error("evitct_page NULL\n");
			bdbm_bug_on(epage == NULL);
			eindex = epage->index;
			edata_ptr = p->ptr_nvmram + (eindex * np->nvm_page_size); 
			elpa = epage->logaddr.lpa[0];
//jeseong
			esize = epage->size;
			evict_write_traffic += esize;
		
			bdbm_bug_on(!edata_ptr);

//			bdbm_error("evitct_check1\n");
			if(epage->status != CLEAR){	//if page is clean, not write back 
				/* get a free hlm_req from the hlm_reqs_pool */
				if((hr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL){
					bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
					goto fail;
				}
	
				/* build hlm_req with nvm_info */
				// 아래 함수에서 logaddr 을 많이 보내고, 그걸로 만들도록 하는게 좋을듯. 
				// 일단은 하나만 보내보자. 
				if (bdbm_hlm_reqs_pool_build_wb_req (hr, &epage->logaddr, edata_ptr) != 0) {
					bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
					goto fail;
				}
				/* hr->done is locked in pool_build_wb_req() */
	
				/* send req */
				bdbm_sema_lock (&bp->host_lock);
		
				if(bdi->ptr_hlm_inf->make_wb_req (bdi, hr) != 0) {
					bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
				}
				bdbm_sema_unlock (&bp->host_lock);
				evict_cnt++;	
			}

//			bdbm_error("evitct_check2\n");
			nvm_lookup_tbl[elpa].tbl_idx = -1;
#ifdef	RFLUSH
			nvm_lookup_tbl[elpa].ptr_page = NULL;
#endif
	
//			bdbm_error("evitct_check3\n");
			list_del(&epage->list);//LRU_LIST_node delete
			

//			bdbm_error("evitct_check4\n");
			list_add(&epage->list, p->free_list);//free list insert
//			bdbm_error("evitct_cnt : %d\n", i);
#ifdef hash1
			list_for_each_entry(hash_ino_node, p->hash_list, list){
				if(hash_ino_node->ino==epage->ino){
					hash_ino_node->cnt--;
					epage=list_last_entry(&hash_ino_node->ino_head, bdbm_nvm_page_t, ino_list);	
					list_del(&epage->ino_list);
					break;
				}
			}	
#endif
#ifdef hash
			list_del(&epage->ino_list);
#endif
		}
//		bdbm_error("evict page write complete\n");
		/* set new page index */
		nindex = eindex;
		npage = epage;
		list_del(&npage->list);
		list_add(&npage->list, p->lru_list);
		p->nr_free_pages --;
		p->nr_inuse_pages ++;
		nvm_lookup_tbl[lpa].tbl_idx = nindex;
#ifdef	RFLUSH
		nvm_lookup_tbl[lpa].ptr_page = npage;
#endif
//		bdbm_error("evict complete\n");
#ifdef hash
		list_for_each_entry(hash_ino_node, p->hash_list, list){
			if(hash_ino_node->ino==lr->ino){//if ino list exist
				list_add(&npage->ino_list, &hash_ino_node->ino_head);
				hash_ino_node->cnt++;
				hash_hit=1;
				break;
			}
		}
		if(hash_hit==0){
			hash_ino_node=(bdbm_nvm_hash_t*)bdbm_zmalloc(sizeof(bdbm_nvm_hash_t));
			hash_ino_node->ino=lr->ino;
			hash_ino_node->cnt=1;
			INIT_LIST_HEAD(&hash_ino_node->ino_head);
			list_add(&npage->ino_list, &hash_ino_node->ino_head);//page insert in ino_list
			list_add(&hash_ino_node->list , p->hash_list);//hash list ino node insert
		}
#endif
	}

	bdbm_bug_on(p->nr_free_pages + p->nr_inuse_pages != p->nr_total_pages);

	return nindex;

fail:
	if (hr) 
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, hr);
	bdbm_bug_on(1);
	
	return -1;

}

uint64_t bdbm_nvm_write_data (bdbm_drv_info_t* bdi, bdbm_llm_req_t* lr)
{ 
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_nvm_page_t* nvm_tbl = p->ptr_nvm_tbl;
	bdbm_hlm_req_t* hr = NULL;
	int64_t nvm_idx = -1;
	uint8_t* ptr_nvmram_addr = NULL;
	uint64_t i;

	p->nr_total_write++;
	atomic64_inc (&bdi->pm.nvm_w_cnt);
/*
	if((p->nr_total_write % 100) == 0)
		printk(KERN_NOTICE "[bdbm]total write : %lu\n", p->nr_total_write);
*/
	/* find data */
	nvm_idx = bdbm_nvm_find_data(bdi, lr);

	/* write miss */
	if(nvm_idx < 0){
		p->nr_nh_write++;
//		bdbm_msg("alloc nvm for data write");
		if((nvm_idx = bdbm_nvm_alloc_slot(bdi, lr)) < 0)
			return 0;
	} else {	// hit
		if(nvm_tbl[nvm_idx].status == DIRTY)
			atomic64_inc (&bdi->pm.nvm_wh_cnt);
		p->nr_write++;
	}

//	p->nr_write++;

	/* get data addr */
	bdbm_bug_on(np->nr_subpages_per_page != 1);	
	bdbm_bug_on(np->page_main_size / KERNEL_PAGE_SIZE != 1);
	bdbm_bug_on(np->page_main_size != np->nvm_page_size);

	ptr_nvmram_addr = p->ptr_nvmram + (nvm_idx * np->nvm_page_size); 
	bdbm_bug_on(!ptr_nvmram_addr);

	/* copy data */
	bdbm_memcpy(ptr_nvmram_addr, lr->fmain.kp_ptr[0], KERNEL_PAGE_SIZE);


	/* update logaddr */
		
	p->lba_struct[lr->logaddr.lpa[0]].cnt++;
	nvm_tbl[nvm_idx].logaddr.lpa[0] = lr->logaddr.lpa[0]; 
	nvm_tbl[nvm_idx].status 	= DIRTY;
#ifdef penta
	nvm_tbl[nvm_idx].ino = lr->ino;
	nvm_tbl[nvm_idx].size = lr->size;
	total_write_traffic += lr->size;
//	printk(KERN_NOTICE "[bdbm]ino : %lu \n", lr->ino);
#endif
//	bdbm_msg("data write succeeds");
//	for(i = 0; i < p->nr_total_pages; i++){
//		bdbm_msg("nvm_tbl[%llu] lpa = %d", i, nvm_tbl[i].logaddr.lpa[0]);
//	}

	/* update lr req's status */
	lr->serviced_by_nvm = 1;

	/**********************************/
	/*								  */
	/* update page table or send trim */
	/*								  */
	/**********************************/

#ifdef NVM_CACHE_TRIM
//	bdbm_msg("[%s] send TRIM", __FUNCTION__);

	/* get a free hlm_req from the hlm_reqs_pool */
	if((hr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL){
		bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
		goto fail;
	}

	/* build trim hr with lpa, len */
	/* hr->done is locked in pool_build_wb_req() */
	if (bdbm_hlm_reqs_pool_build_int_trim_req (hr, lr->logaddr.lpa[0], 1) != 0) {
		bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
		goto fail;
	}


	/* send req */
	bdbm_sema_lock (&bp->host_lock);

	if(bdi->ptr_hlm_inf->make_req (bdi, hr) != 0) {
		bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
	}
	bdbm_sema_unlock (&bp->host_lock);
#endif
/*
	if(p->nr_total_write % 100 == 0){
		printk(KERN_NOTICE "[bdbm]total_write_traffic  : %lu\n", total_write_traffic);
		printk(KERN_NOTICE "[bdbm]evict_write_traffic  : %lu\n", evict_write_traffic);
		printk(KERN_NOTICE "[bdbm]rflush_write_traffic : %lu\n", rflush_write_traffic);
		printk(KERN_NOTICE "[bdbm]flush_write_traffic  : %lu\n", flush_write_traffic);
		printk(KERN_NOTICE "[bdbm]total_write_count: %lu\n", p->nr_total_write);
		printk(KERN_NOTICE "[bdbm]evict_count      : %lu\n", evict_cnt);
		printk(KERN_NOTICE "[bdbm]buffering_count  : %lu\n", p->nr_write);
		printk(KERN_NOTICE "[bdbm]buffering_count2 : %lu\n", atomic64_read(&bdi->pm.nvm_wh_cnt));
		printk(KERN_NOTICE "[bdbm]rflush_count     : %lu\n", rflush_cnt);
		printk(KERN_NOTICE "[bdbm]flush_count      : %lu\n", flush_cnt);
		printk(KERN_NOTICE "[bdbm]rflush_num       : %lu\n", rflush_num);
		printk(KERN_NOTICE "[bdbm]flush_num        : %lu\n", flush_num);
		printk(KERN_NOTICE "[bdbm]gc inocation : %ld\n", atomic64_read(&bdi->pm.gc_cnt));
		printk(KERN_NOTICE "{bdbm}block erase  : %ld\n", atomic64_read(&bdi->pm.gc_erase_cnt));
	}
*/
	return 1;

fail:
	if (hr) 
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, hr);
	return 1;	

}

#ifdef RFLUSH
uint64_t bdbm_nvm_rflush_data (bdbm_drv_info_t* bdi, uint64_t ino) {
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;	// have lists lru_list and free_list
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;
	bdbm_hlm_req_t* lhr = NULL;
	bdbm_nvm_page_t* fpage = NULL;
	bdbm_nvm_page_t* fpage_temp = NULL;
	uint8_t* fdata_ptr = NULL;
	int64_t findex = -1;
	uint64_t fino = -1;
	uint64_t loop = 0;
	uint32_t fsize=0;
	
	struct lba_entry* lba_entry;
	struct lba_entry* temp;
#ifdef hash
	bdbm_nvm_hash_t* hash_ino_node=NULL;	
	struct list_head* head_temp=NULL;
#endif
	
	fino = ino;

	list_for_each_entry(hash_ino_node, p->hash_list, list){
		if(hash_ino_node->ino==fino){//hit
			list_for_each_entry_safe(fpage, fpage_temp, &hash_ino_node->ino_head, ino_list){	
				if(fpage->status ==DIRTY){
	//			if( fpage->ino == fino || fpage->ino == 1 || fpage->ino == 2 || fpage->ino == 0){
					findex = fpage->index;
					fsize = fpage->size;
					fdata_ptr = p->ptr_nvmram + (findex * np->nvm_page_size);
					/* get a free hlm_req from the hlm_reqs_pool */
   				    if ((lhr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL) {
						bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
   	    			    goto fail;
   		   			 }
							    
   	 			    /* build hlm_req with nvm_info */
   	   			  	if (bdbm_hlm_reqs_pool_build_wb_req (lhr, &fpage->logaddr, fdata_ptr) != 0) {
       	   			  	bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
       	   			  	goto fail;
       				}
	
   		     		/* send req */
   	   		  		bdbm_sema_lock (&bp->host_lock);
		
					if(bdi->ptr_hlm_inf->make_wb_req (bdi, lhr) != 0) {
   	    	     	bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
   		    	 	}
       			 	bdbm_sema_unlock (&bp->host_lock);
					rflush_write_traffic += fsize;
					fpage->status=CLEAR;
					loop++;
					fsize=0;
					if(loop >= max_evict_rflush_num)
						max_evict_rflush_num=loop;
				}			
			}
		}
	}	
	//bdbm_msg("flush ino : %d", ino);
	rflush_cnt += loop;
	rflush_num++;

	bdbm_bug_on(p->nr_free_pages + p->nr_inuse_pages != p->nr_total_pages);
	if(!loop)
		return 0;
	else
		return 1;

fail:
	if (lhr)
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, lhr);
	bdbm_bug_on(1);

	return -1;
}

#endif

#ifdef	RFLUSH_msjung
uint64_t bdbm_nvm_rflush_data (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr) {
	/* find nvm cache */
	bdbm_device_params_t* np = BDBM_GET_DEVICE_PARAMS (bdi);
	bdbm_blkio_private_t* bp = (bdbm_blkio_private_t*) BDBM_HOST_PRIV(bdi);
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;	// have lists lru_list and free_list
	bdbm_nvm_lookup_tbl_entry_t* nvm_lookup_tbl = p->ptr_nvm_lookup_tbl;
	bdbm_hlm_req_t* lhr = NULL;
	bdbm_nvm_page_t* fpage = NULL;
	uint8_t* fdata_ptr = NULL;
	int64_t findex = -1;
	sector_t i = 0;
	uint64_t loop = 0;
	
	bdbm_blkio_req_t* br = (bdbm_blkio_req_t*) hr->blkio_req;
	struct bio* bi = (struct bio*) br->bio;
	sector_t lpamin = bi->bi_min;
	sector_t lpamax = bi->bi_max;

//	printk("lpamin : %lu\n", lpamin);
//	printk("lpamax : %lu\n", lpamax);

	for (i = lpamin; i <= lpamax; i++) {
		if (nvm_lookup_tbl[i].tbl_idx == -1)
			continue;

//		printk("i : %lu\n", i);
		fpage = nvm_lookup_tbl[i].ptr_page;
		bdbm_bug_on(fpage == NULL);
		findex = fpage->index;
		fdata_ptr = p->ptr_nvmram + (findex * np->nvm_page_size);
		/* get a free hlm_req from the hlm_reqs_pool */
        if ((lhr = bdbm_hlm_reqs_pool_get_item(bp->hlm_reqs_pool)) == NULL) {
			bdbm_error("bdbm_hlm_reqs_pool_get_item () failed");
            goto fail;
        }
				    
        /* build hlm_req with nvm_info */
        if (bdbm_hlm_reqs_pool_build_wb_req (lhr, &fpage->logaddr, fdata_ptr) != 0) {
            bdbm_error ("bdbm_hlm_reqs_pool_build_req () failed");
            goto fail;
        }

        /* send req */
        bdbm_sema_lock (&bp->host_lock);

		if(bdi->ptr_hlm_inf->make_wb_req (bdi, lhr) != 0) {
            bdbm_error ("'bdi->ptr_hlm_inf->make_req' failed");
        }
		
		rflush_cnt++;
        bdbm_sema_unlock (&bp->host_lock);
        
		nvm_lookup_tbl[i].tbl_idx = -1; 
        nvm_lookup_tbl[i].ptr_page = NULL;
        
		list_del(&fpage->list);
        p->nr_inuse_pages--;
        list_add(&fpage->list, p->free_list);
        p->nr_free_pages++;
		loop++;
	}
	bdbm_bug_on(p->nr_free_pages + p->nr_inuse_pages != p->nr_total_pages);
	printk(KERN_NOTICE "[bdbm]rflush_cnt : %lu\n", rflush_cnt );
	if(!loop)
		return 0;
	else
		return 1;

fail:
	if (lhr)
		bdbm_hlm_reqs_pool_free_item (bp->hlm_reqs_pool, lhr);
	bdbm_bug_on(1);

	return -1;
}
#endif

uint64_t bdbm_nvm_make_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr){
	/* find nvm cache */
	bdbm_nvm_dev_private_t* p = _nvm_dev.ptr_private;
	bdbm_llm_req_t* lr; 
	int64_t n, nr_remains;
	bdbm_blkio_req_t * br = (bdbm_blkio_req_t *)hr->blkio_req;
	uint64_t ino = br->bi_f_ino;
	uint64_t ret = 0;
	struct timeval tv; 
	struct time_entry *sync_time;		

	nr_remains = hr->nr_llm_reqs;

	bdbm_sema_lock (&p->nvm_lock);
	
#ifdef	RFLUSH
	if (bdbm_is_rflush (hr->req_type)) {
	sync_time = kmalloc(sizeof(*sync_time), GFP_KERNEL);
	sync_time->time =time_us();
	sync_time->type = 1;//rflush 
	INIT_LIST_HEAD(&sync_time->list);
	list_add_tail(&sync_time->list,&sync_time_head);
#ifdef  FLUSHON
		bdbm_nvm_rflush_data (bdi, ino);
		bdi->ptr_llm_inf->flush(bdi);
#endif
		bdi->ptr_host_inf->end_req(bdi,hr);
	}
#endif
#ifdef	FLUSH
	else if (bdbm_is_flush (hr->req_type)){
	sync_time = kmalloc(sizeof(*sync_time), GFP_KERNEL);
	sync_time->time =time_us();
	sync_time->type = 0;//rflush 
	INIT_LIST_HEAD(&sync_time->list);
	list_add_tail(&sync_time->list,&sync_time_head);
#ifdef FLUSHON
		bdbm_blkio_req_t* br = (bdbm_blkio_req_t *)hr->blkio_req;
		bdbm_nvm_flush_data (bdi, ino);
		bdi->ptr_llm_inf->flush(bdi);
#endif 
		if(br->bi_size == 0)
		{
			bdi->ptr_host_inf->end_req(bdi,hr);
		}
	}
#endif

	/* for nr_llm_reqs */	
	for (n = 0; n < hr->nr_llm_reqs; n++) {

		p->nr_total_access++;
		atomic64_inc(&bdi->pm.nvm_a_cnt);
	
		if ((p->nr_total_access % 100000) == 0){
			bdbm_msg("nvm: total access = %llu, total read = %llu, read hit = %llu, read no hit = %llu, total_write = %llu, write hit = %llu, write no hit = %llu, evict = %llu", 
				p->nr_total_access, p->nr_total_read, p->nr_read, p->nr_nh_read, p->nr_total_write, p->nr_write, p->nr_nh_write, p->nr_evict);
		}

		lr = &hr->llm_reqs[n];

		if (lr->req_type == REQTYPE_READ) {
			if(bdbm_nvm_read_data (bdi, &hr->llm_reqs[n]))
				nr_remains --; 	

		} else if (lr->req_type == REQTYPE_WRITE) {
			if(bdbm_nvm_write_data(bdi, &hr->llm_reqs[n]))
				nr_remains --;
		}
	}

	bdbm_sema_unlock (&p->nvm_lock);

	//bdbm_bug_on((hr->req_type == REQTYPE_WRITE) && (nr_remains != 0));

	return nr_remains;
}

#if 0
static int __hlm_reqs_pool_create_trim_req  (
	bdbm_hlm_reqs_pool_t* pool, 
	bdbm_hlm_req_t* hr,
	bdbm_blkio_req_t* br)
{
	int64_t sec_start, sec_end;

	/* trim boundary sectors */
	sec_start = BDBM_ALIGN_UP (br->bi_offset, NR_KSECTORS_IN(pool->map_unit));
	sec_end = BDBM_ALIGN_DOWN (br->bi_offset + br->bi_size, NR_KSECTORS_IN(pool->map_unit));

	/* initialize variables */
	hr->req_type = br->bi_rw;
	bdbm_stopwatch_start (&hr->sw);
	if (sec_start < sec_end) {
		hr->lpa = (sec_start) / NR_KSECTORS_IN(pool->map_unit);
		hr->len = (sec_end - sec_start) / NR_KSECTORS_IN(pool->map_unit);
	} else {
		hr->lpa = (sec_start) / NR_KSECTORS_IN(pool->map_unit);
		hr->len = 0;
	}
	hr->blkio_req = (void*)br;
	hr->ret = 0;

	return 0;
}
#endif
