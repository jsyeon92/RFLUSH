#define FLUSHON
#define file1


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

//#ifndef _BLUEDBM_HOST_BLKDEV_H
//#define _BLUEDBM_HOST_BLKDEV_H
#define CLEAR 0
#define DIRTY 1

#define NVM_BLK_SIZE 4096
#define RFLUSH
extern bdbm_nvm_inf_t _nvm_dev;

struct lba_entry{
	sector_t start_sec;
	sector_t end_sec;
	struct list_head list;	
};
struct time_entry{
	uint32_t time;
	uint32_t type; //rflush 1 flush 0  
	struct list_head list;
};
typedef struct {
	uint8_t status;
	int64_t index;
	bdbm_logaddr_t logaddr;
//	bdbm_phyaddr_t phyaddr;
	struct list_head list;	/* for lru list */
	uint64_t ino;
	uint32_t size;
} bdbm_nvm_page_t;

typedef struct {
	int64_t tbl_idx;
#ifdef	RFLUSH
	bdbm_nvm_page_t* ptr_page;
#endif
} bdbm_nvm_lookup_tbl_entry_t;
struct lba_struct{
	uint64_t cnt;
};
typedef struct {
	bdbm_device_params_t* np;
	uint64_t nr_total_pages;
	uint64_t nr_free_pages;
	uint64_t nr_inuse_pages; 

	uint64_t nr_total_access;
	uint64_t nr_total_write;
	uint64_t nr_total_read;
	uint64_t nr_write; //count of hit
	uint64_t nr_nh_write; //count of no hit
	uint64_t nr_read;
	uint64_t nr_nh_read;
	uint64_t nr_total_hit;
	uint64_t nr_evict;

	void* ptr_nvmram; /* DRAM memory for nvm */
//	bdbm_nvm_page_t* ptr_nvm_rb_tree;
	bdbm_nvm_page_t* ptr_nvm_tbl;
	//bdbm_nvm_page_t* ptr_nvm_lookup_tbl;
	bdbm_nvm_lookup_tbl_entry_t* ptr_nvm_lookup_tbl;

	bdbm_sema_t nvm_lock;
	struct list_head* lru_list;
	struct list_head* free_list;
//jsyeon0111
	struct list_head* clear_list;

//	bdbm_nvm_block_t* ptr_lru_list;

	struct lba_struct* lba_struct;

} bdbm_nvm_dev_private_t;

uint32_t bdbm_nvm_create (bdbm_drv_info_t* bdi);
void bdbm_nvm_destroy (bdbm_drv_info_t* bdi);
uint64_t bdbm_nvm_make_req (bdbm_drv_info_t* bdi, bdbm_hlm_req_t* hr);
uint64_t bdbm_nvm_rflush_data (bdbm_drv_info_t* bdi, uint64_t ino);
uint64_t bdbm_nvm_flush_data (bdbm_drv_info_t* bdi, uint64_t ino);
#ifdef penta
static uint32_t user_flush_cnt=1;
static uint32_t sys_flush_cnt =0;
static uint32_t rflush_cnt = 0;
static uint32_t flush_cnt = 0;
static uint32_t evict_cnt = 0;
static uint32_t flush_num =0;
static uint32_t rflush_num=0;

static uint64_t rflush_write_traffic= 0;
static uint64_t flush_write_traffic = 0;
static uint64_t evict_write_traffic = 0;
static uint64_t total_write_traffic = 0;
static uint32_t sum=0;
static uint32_t hit_addr[101]={0};
static uint32_t cache_threshold=0.8;
static uint32_t evict_cache_pages=0;
static uint32_t max_evict_flush_num=0;
static uint32_t max_evict_rflush_num=0;

#endif
//#endif



