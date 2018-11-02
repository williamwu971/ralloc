/*
 * Copyright (C) 2007  Scott Schneider, Christos Antonopoulos
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "pmmalloc.h"
#include <stdatomic.h>
#include <immintrin.h> // RTM support (TSX)

/*
 * This implementation uses RTM to avoid ABA-problem in Partial queue and free desc
 * linked list.
 * The heaps are per-threaded, but according to the header of each allocated block, we
 * are able to put them back to the list of their corresponding heap.
 */


const unsigned int RETRY_MAX = 1024;

static void lf_fifo_queue_init(lf_fifo_queue_t *queue);
static void lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element);
static void *lf_fifo_dequeue(lf_fifo_queue_t *queue);

inline void lf_fifo_queue_init(lf_fifo_queue_t *queue)
{
	queue->top = 0;
}

inline void *lf_fifo_dequeue(lf_fifo_queue_t *queue)
{
	/* Tag version of ABA-free dequeue
	top_aba_t head;
	top_aba_t next;
	void* ret = NULL;

	head.top = queue->both.top;
	head.top = queue->both.top;
	while(1) {
		if (head.top == 0) {
			break;
		}
		next.top = (uint64_t)(((struct queue_elem_t *)head.top)->next);
		next.ocount = head.ocount + 1;
		if (atomic_compare_exchange_weak((volatile uint64_t *)&(queue->both), ((uint64_t*)&head), *((uint64_t*)&next))) {
			ret = ((void *)head.top);
			break;
		}
	}
	*/

	uint64_t head;
	uint64_t next;
	unsigned int status = 0;
	unsigned int retry = 0;
	void* ret = NULL;

	while(1){
		FLUSHFENCE;//TODO: double check the correctness
		if ((status = _xbegin ()) == _XBEGIN_STARTED) {
			head = queue->top;
			if(head == 0){
				_xabort (1);//_XABORT_EXPLICIT bit in status will be set
			}
			next = (uint64_t)(((struct queue_elem_t *)head)->next);
			queue->top = next;
			ret = (void*)head;
			_xend ();
			FLUSH(&queue->top);
			FLUSHFENCE;
			break;
		} else {
			if(status & _XABORT_EXPLICIT) {
				//explicitly aborted due to empty head
				ret=NULL;
				break;
			}
			else if(retry++ < RETRY_MAX){
				continue;
			} else{
				fprintf(stderr, "Warning: RTM retries too many times!\n");
				continue;
			}
		}
	}
	return ret;
}

inline void lf_fifo_enqueue(lf_fifo_queue_t *queue, void *element)
{
	/* tag version of ABA-free enqueue
	top_aba_t old_top;
	top_aba_t new_top;

	old_top.ocount = queue->both.ocount;
	old_top.top = queue->both.top;
	while(1) {

		((struct queue_elem_t *)element)->next = (struct queue_elem_t *)old_top.top;
		new_top.top = (uint64_t)element;
		new_top.ocount = old_top.ocount + 1;
		if (atomic_compare_exchange_weak((volatile uint64_t *)&(queue->both), ((uint64_t*)&old_top), *((uint64_t*)&new_top))) {
			return 0;
		}
	}
	*/
	uint64_t old_top;
	unsigned int status = 0;
	unsigned int retry = 0;
	while(1){
		FLUSHFENCE;//TODO: double check the correctness
		if ((status = _xbegin ()) == _XBEGIN_STARTED) {
			old_top = queue->top;
			((struct queue_elem_t *)element)->next = (struct queue_elem_t *)old_top;
			queue->top = (uint64_t)element;
			_xend ();
			FLUSH(&queue->top);
			FLUSHFENCE;
			break;
		} else {
			if(retry++ < RETRY_MAX){
				continue;
			} else{
				fprintf(stderr, "Warning: RTM retries too many times!\n");
				continue;
			}
		}
	}
	return;
}




/* This is large and annoying, but it saves us from needing an 
 * initialization routine. */
sizeclass sizeclasses[2048 / GRANULARITY] =
				{
				{LF_FIFO_QUEUE_STATIC_INIT, 8, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 16, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 24, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 32, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 40, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 48, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 56, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 64, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 72, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 80, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 88, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 96, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 104, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 112, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 120, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 128, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 136, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 144, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 152, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 160, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 168, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 176, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 184, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 192, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 200, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 208, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 216, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 224, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 232, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 240, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 248, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 256, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 264, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 272, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 280, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 288, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 296, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 304, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 312, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 320, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 328, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 336, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 344, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 352, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 360, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 368, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 376, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 384, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 392, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 400, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 408, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 416, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 424, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 432, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 440, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 448, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 456, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 464, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 472, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 480, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 488, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 496, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 504, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 512, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 520, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 528, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 536, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 544, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 552, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 560, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 568, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 576, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 584, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 592, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 600, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 608, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 616, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 624, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 632, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 640, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 648, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 656, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 664, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 672, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 680, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 688, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 696, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 704, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 712, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 720, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 728, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 736, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 744, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 752, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 760, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 768, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 776, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 784, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 792, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 800, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 808, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 816, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 824, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 832, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 840, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 848, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 856, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 864, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 872, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 880, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 888, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 896, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 904, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 912, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 920, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 928, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 936, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 944, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 952, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 960, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 968, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 976, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 984, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 992, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1000, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1008, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1016, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1024, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1032, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1040, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1048, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1056, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1064, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1072, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1080, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1088, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1096, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1104, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1112, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1120, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1128, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1136, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1144, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1152, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1160, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1168, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1176, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1184, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1192, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1200, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1208, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1216, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1224, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1232, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1240, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1248, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1256, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1264, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1272, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1280, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1288, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1296, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1304, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1312, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1320, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1328, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1336, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1344, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1352, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1360, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1368, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1376, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1384, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1392, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1400, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1408, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1416, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1424, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1432, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1440, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1448, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1456, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1464, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1472, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1480, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1488, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1496, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1504, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1512, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1520, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1528, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1536, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1544, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1552, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1560, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1568, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1576, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1584, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1592, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1600, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1608, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1616, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1624, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1632, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1640, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1648, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1656, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1664, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1672, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1680, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1688, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1696, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1704, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1712, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1720, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1728, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1736, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1744, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1752, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1760, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1768, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1776, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1784, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1792, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1800, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1808, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1816, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1824, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1832, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1840, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1848, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1856, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1864, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1872, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1880, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1888, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1896, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1904, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1912, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1920, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1928, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1936, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1944, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1952, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1960, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1968, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1976, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 1984, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 1992, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 2000, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 2008, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 2016, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 2024, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 2032, SBSIZE},
				{LF_FIFO_QUEUE_STATIC_INIT, 2040, SBSIZE}, {LF_FIFO_QUEUE_STATIC_INIT, 2048, SBSIZE},
				};

__thread procheap* heaps[2048 / GRANULARITY] =	{ };

static volatile descriptor_queue queue_head;

static inline long min(long a, long b)
{
	return a < b ? a : b;
}

static inline long max(long a, long b)
{
	return a > b ? a : b;
}

static void* AllocNewSB(size_t size, uint64_t alignement)
{
	void* addr;

	//mmap will align to a page which is 4096B so the first desc must be aligned to 64B
	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "AllocNewSB() mmap failed, %lu: ", size);
		switch (errno) {
			case EBADF:	fprintf(stderr, "EBADF"); break;
			case EACCES:	fprintf(stderr, "EACCES"); break;
			case EINVAL:	fprintf(stderr, "EINVAL"); break;
			case ETXTBSY:	fprintf(stderr, "ETXBSY"); break;
			case EAGAIN:	fprintf(stderr, "EAGAIN"); break;
			case ENOMEM:	fprintf(stderr, "ENOMEM"); break;
			case ENODEV:	fprintf(stderr, "ENODEV"); break;
		}
		fprintf(stderr, "\n");
		fflush(stderr);
		exit(1);
	}
	else if (addr == NULL) {
		fprintf(stderr, "AllocNewSB() mmap of size %lu returned NULL\n", size);
		fflush(stderr);
		exit(1);
	}

	return addr;
}

static void organize_desc_list(descriptor* start, uint64_t count, uint64_t stride)
{
	uint64_t ptr;
	unsigned int i;
 
	start->Next = (descriptor*)(start + stride);
	FLUSH(&start->Next);
	ptr = (uint64_t)start; 
	for (i = 1; i < count - 1; i++) {
		ptr += stride;
		((descriptor*)ptr)->Next = (descriptor*)(ptr + stride);
		FLUSH(&((descriptor*)ptr)->Next);
	}
	ptr += stride;
	((descriptor*)ptr)->Next = NULL;
	FLUSH(&((descriptor*)ptr)->Next);
	// FLUSHFENCE;//TODO: double check if we need this
}

static void organize_list(void* start, uint64_t count, uint64_t stride)
{
	uint64_t ptr;
	uint64_t i;
  
	ptr = (uint64_t)start; 
	for (i = 1; i < count - 1; i++) {
		ptr += stride;
		*((uint64_t*)ptr) = i + 1;
		FLUSH((uint64_t*)ptr);
	}
	// FLUSHFENCE();//TODO: double check if we need this
}

static descriptor* DescAlloc() {
  
	descriptor_queue old_queue, new_queue;
	descriptor* desc;
	unsigned int status;
	unsigned int retry = 0;

#ifdef DEBUG
	fprintf(stderr, "In DescAlloc\n");
	fflush(stderr);
#endif

	descriptor* new_sb = AllocNewSB(DESCSBSIZE, sizeof(descriptor));
	organize_desc_list((void *)new_sb, DESCSBSIZE / sizeof(descriptor), sizeof(descriptor));
	while(1) {
		/* tag version of ABA-free solution
		old_queue = queue_head;
		if (old_queue.DescAvail) {
			new_queue.DescAvail = (uint64_t)((descriptor*)old_queue.DescAvail)->Next;
			new_queue.tag = old_queue.tag + 1;
			if (atomic_compare_exchange_weak((volatile uint64_t*)&queue_head, ((uint64_t*)&old_queue), *((uint64_t*)&new_queue))) {
				desc = (descriptor*)old_queue.DescAvail;
#ifdef DEBUG
				fprintf(stderr, "Returning recycled descriptor %p (tag %hu)\n", desc, queue_head.tag);
				fflush(stderr);
#endif
				break;
			}
		}
		else {
			desc = AllocNewSB(DESCSBSIZE, sizeof(descriptor));
			organize_desc_list((void *)desc, DESCSBSIZE / sizeof(descriptor), sizeof(descriptor));

			new_queue.DescAvail = (uint64_t)desc->Next;
			new_queue.tag = old_queue.tag + 1;
			if (atomic_compare_exchange_weak((volatile uint64_t*)&queue_head, ((uint64_t*)&old_queue), *((uint64_t*)&new_queue))) {
#ifdef DEBUG
				fprintf(stderr, "Returning descriptor %p from new descriptor block\n", desc);
				fflush(stderr);
#endif
				break;
			}
			munmap((void*)desc, DESCSBSIZE);   
		}
		*/
		FLUSHFENCE;//TODO: double check the correctness
		if ((status = _xbegin()) == _XBEGIN_STARTED) {
			old_queue = queue_head;
			if (old_queue.DescAvail) {
				new_queue.DescAvail = (uint64_t)((descriptor*)old_queue.DescAvail)->Next;
				queue_head = new_queue;
				desc = (descriptor*)old_queue.DescAvail;
				_xend();
				FLUSH(&new_queue);
				FLUSH(&queue_head);
				FLUSHFENCE;
				munmap((void*)new_sb, DESCSBSIZE);//don't need new_sb so munmap it.
				break;
			} else {
				desc = new_sb;
				new_queue.DescAvail = (uint64_t)desc->Next;
				queue_head = new_queue;
				_xend();
				FLUSH(&new_queue);
				FLUSH(&queue_head);
				break;
			}
		} else{
			if(retry++ < RETRY_MAX){
				continue;
			} else{
				fprintf(stderr, "Warning: RTM retries too many times!\n");
				continue;
			}
		}
	}
	return desc;
}

void DescRetire(descriptor* desc)
{
	descriptor_queue old_queue, new_queue;

#ifdef DEBUG
	fprintf(stderr, "Recycling descriptor %p (sb %p)\n", desc, desc->sb);
	fflush(stderr);
#endif  
	old_queue = queue_head;//atomic_compare_exchange_weak updates old_queue on failure of CAS
	do {
		desc->Next = (descriptor*)old_queue.DescAvail;
		new_queue.DescAvail = (uint64_t)desc;
		FLUSHFENCE;
	} while (!atomic_compare_exchange_weak((volatile uint64_t*)&queue_head, ((uint64_t*)&old_queue), *((uint64_t*)&new_queue)));//it's ABA-safe to CAS without tag
	FLUSH(&queue_head);
	FLUSHFENCE;
}

static void ListRemoveEmptyDesc(sizeclass* sc)
{//TODO: is it necessary?
	descriptor *desc;
	lf_fifo_queue_t temp = LF_FIFO_QUEUE_STATIC_INIT;

	while ((desc = (descriptor *)lf_fifo_dequeue(&sc->Partial))) {
		lf_fifo_enqueue(&temp, (void *)desc);
		if (desc->sb == NULL) {
			DescRetire(desc);
		}
		else {
			break;
		}
	}

	while ((desc = (descriptor *)lf_fifo_dequeue(&temp))) {
		lf_fifo_enqueue(&sc->Partial, (void *)desc);
	}
}

static descriptor* ListGetPartial(sizeclass* sc)
{
	return (descriptor*)lf_fifo_dequeue(&sc->Partial);
}

static void ListPutPartial(descriptor* desc)
{
	lf_fifo_enqueue(&desc->heap->sc->Partial, (void*)desc);  
}

static void RemoveEmptyDesc(procheap* heap, descriptor* desc)
{
	FLUSHFENCE;
	if (atomic_compare_exchange_strong(&heap->Partial, &desc, NULL)) {
		FLUSH(&heap->Partial);
		FLUSHFENCE;
		DescRetire(desc);
	}
	else {
		ListRemoveEmptyDesc(heap->sc);
	}
}

static descriptor* HeapGetPartial(procheap* heap)
{ 
	descriptor* desc;
	desc = *((descriptor**)&heap->Partial); // casts away the volatile
	do {
		if (desc == NULL) {
			return ListGetPartial(heap->sc);
		}
		FLUSHFENCE;
	} while (!atomic_compare_exchange_weak(&heap->Partial, &desc, NULL));
	FLUSH(&heap->Partial);
	FLUSHFENCE;
	return desc;
}

static void HeapPutPartial(descriptor* desc)
{
	descriptor* prev;
	prev = (descriptor*)desc->heap->Partial; // casts away volatile
	FLUSHFENCE;//TODO: double check the correctness
	while (!atomic_compare_exchange_weak(&desc->heap->Partial, &prev, desc)){};
	FLUSH(&desc->heap->Partial);
	FLUSHFENCE;
	if (prev) {
		ListPutPartial(prev); 
	}
}

static void UpdateActive(procheap* heap, descriptor* desc, uint64_t morecredits)
{ 
	active oldactive, newactive;
	anchor oldanchor, newanchor;

#ifdef DEBUG
	fprintf(stderr, "UpdateActive() heap->Active %p, credits %lu\n", *((void**)&heap->Active), morecredits);
	fflush(stderr);
#endif

	*((uint64_t*)&oldactive) = 0;
	newactive.ptr = (uint64_t)desc>>6;
	newactive.credits = morecredits - 1;
	FLUSHFENCE;
	if (atomic_compare_exchange_strong((volatile uint64_t *)&heap->Active, ((uint64_t*)&oldactive), *((uint64_t*)&newactive))) {
		FLUSH(&heap->Active);
		FLUSHFENCE;
		return;
	}

	// Someone installed another active sb
	// Return credits to sb and make it partial
	oldanchor = desc->Anchor;
	do { 
		newanchor = oldanchor;//atomic_compare_exchange_weak updates old_queue on failure of CAS
		newanchor.count += morecredits;
		newanchor.state = PARTIAL;
		FLUSHFENCE;
	} while (!atomic_compare_exchange_weak((volatile uint64_t *)&desc->Anchor, ((uint64_t*)&oldanchor), *((uint64_t*)&newanchor)));
	FLUSH(&desc->Anchor);
	FLUSHFENCE;

	HeapPutPartial(desc);
}

static descriptor* mask_credits(active oldactive)
{
	uint64_t ret = oldactive.ptr;
	return (descriptor*)(ret<<6);
}

static void* MallocFromActive(procheap *heap) 
{
	active newactive, oldactive;
	descriptor* desc;
	anchor oldanchor, newanchor;
	void* addr;
	uint64_t morecredits = 0;
	unsigned int next = 0;

	// First step: reserve block
	oldactive = heap->Active;
	do { 
		newactive = oldactive;//atomic_compare_exchange updates old_queue on failure of CAS
		if (!(*((uint64_t*)(&oldactive)))) {
			return NULL;
		}
		if (oldactive.credits == 0) {
			*((uint64_t*)(&newactive)) = 0;
#ifdef DEBUG
			fprintf(stderr, "MallocFromActive() setting active to NULL, %lu, %d\n", newactive.ptr, newactive.credits);
			fflush(stderr);
#endif
		}
		else {
			--newactive.credits;
		}
		FLUSHFENCE;
	} while (!atomic_compare_exchange_weak((volatile uint64_t*)&heap->Active, ((uint64_t*)&oldactive), *((uint64_t*)&newactive)));
	FLUSH(&heap->Active);
	FLUSHFENCE;

#ifdef DEBUG
	fprintf(stderr, "MallocFromActive() heap->Active %p, credits %hu\n", *((void**)&heap->Active), oldactive.credits);
	fflush(stderr);
#endif

	// Second step: pop block
	desc = mask_credits(oldactive);
	oldanchor = desc->Anchor;
	do {
		// state may be ACTIVE, PARTIAL or FULL
		newanchor = oldanchor;//atomic_compare_exchange updates old_queue on failure of CAS
		addr = (void *)((uint64_t)desc->sb + oldanchor.avail * desc->sz);
		next = *(uint64_t *)addr;
		newanchor.avail = next;
		++newanchor.tag;

		if (oldactive.credits == 0) {

			// state must be ACTIVE
			if (oldanchor.count == 0) {
#ifdef DEBUG
				fprintf(stderr, "MallocFromActive() setting superblock %p to FULL\n", desc->sb);
				fflush(stderr);
#endif
				newanchor.state = FULL;
			}
			else { 
				morecredits = min(oldanchor.count, MAXCREDITS);
				newanchor.count -= morecredits;
			}
		} 
		FLUSHFENCE;
	} while (!atomic_compare_exchange_weak((volatile uint64_t*)&desc->Anchor, ((uint64_t*)&oldanchor), *((uint64_t*)&newanchor)));
	FLUSH(&desc->Anchor);
	FLUSHFENCE;
#ifdef DEBUG
	fprintf(stderr, "MallocFromActive() sb %p, Active %p, avail %d, oldanchor.count %hu, newanchor.count %hu, morecredits %lu, MAX %d\n", 
			desc->sb, *((void**)&heap->Active), desc->Anchor.avail, oldanchor.count, newanchor.count, morecredits, MAXCREDITS);
	fflush(stderr);
#endif

	if (oldactive.credits == 0 && oldanchor.count > 0) {
		UpdateActive(heap, desc, morecredits);
	}

	*((char*)addr) = (char)SMALL; 
	addr += TYPE_SIZE;
	*((descriptor**)addr) = desc; 
	FLUSH(addr-TYPE_SIZE);
	FLUSH(addr);//so that desc of each block won't lose
	return ((void*)((uint64_t)addr + PTR_SIZE));
}

static void* MallocFromPartial(procheap* heap)
{
	descriptor* desc;
	anchor oldanchor, newanchor;
	uint64_t morecredits;
	void* addr;
  
retry:
	desc = HeapGetPartial(heap);
	if (!desc) {
		return NULL;
	}

	desc->heap = heap;
	FLUSH(&desc->heap);
	oldanchor = desc->Anchor;
	do {
		// reserve blocks
		newanchor = oldanchor;
		if (oldanchor.state == EMPTY) {
			DescRetire(desc); 
			goto retry;
		}

		// oldanchor state must be PARTIAL
		// oldanchor count must be > 0
		morecredits = min(oldanchor.count - 1, MAXCREDITS);
		newanchor.count -= morecredits + 1;
		newanchor.state = (morecredits > 0) ? ACTIVE : FULL;
		FLUSHFENCE;
	} while (!atomic_compare_exchange_weak((volatile uint64_t*)&desc->Anchor, ((uint64_t*)&oldanchor), *((uint64_t*)&newanchor)));
	FLUSH(&desc->Anchor);
	FLUSHFENCE;

	oldanchor = desc->Anchor;
	do { 
		// pop reserved block
		newanchor = oldanchor;
		addr = (void*)((uint64_t)desc->sb + oldanchor.avail * desc->sz);

		newanchor.avail = *(uint64_t*)addr;
		++newanchor.tag;
		FLUSHFENCE;
	} while (!atomic_compare_exchange_weak((volatile uint64_t*)&desc->Anchor, ((uint64_t*)&oldanchor), *((uint64_t*)&newanchor)));
	FLUSH(&desc->Anchor);
	FLUSHFENCE;

	if (morecredits > 0) {
		UpdateActive(heap, desc, morecredits);
	}

	*((char*)addr) = (char)SMALL; 
	addr += TYPE_SIZE;
	*((descriptor**)addr) = desc; 
	FLUSH(addr-TYPE_SIZE);
	FLUSH(addr);//so that desc at the head of each block won't lose
	return ((void *)((uint64_t)addr + PTR_SIZE));
}

static void* MallocFromNewSB(procheap* heap)
{
	descriptor* desc;
	void* addr;
	active newactive, oldactive;

	*((uint64_t*)&oldactive) = 0;
	desc = DescAlloc();
	desc->sb = AllocNewSB(heap->sc->sbsize, SBSIZE);

	desc->heap = heap;
	desc->Anchor.avail = 1;
	desc->sz = heap->sc->sz;
	desc->maxcount = heap->sc->sbsize / desc->sz;
	FLUSH(&desc->sb);
	FLUSH(&desc->heap);
	FLUSH(&desc->sz);
	FLUSH(&desc->maxcount);

	// Organize blocks in a linked list starting with index 0.
	organize_list(desc->sb, desc->maxcount, desc->sz);

#ifdef DEBUG
	fprintf(stderr, "New SB %p associated with desc %p (sz %u, sbsize %d, heap %p, Anchor.avail %hu, Anchor.count %hu)\n", 
			desc->sb, desc, desc->sz, heap->sc->sbsize, heap, desc->Anchor.avail, desc->Anchor.count);
	fflush(stderr);
#endif

	*((uint64_t*)&newactive) = 0;
	newactive.ptr = (uint64_t)desc>>6;
	newactive.credits = min(desc->maxcount - 1, MAXCREDITS) - 1;

	desc->Anchor.count = max(((signed long)desc->maxcount - 1 ) - ((signed long)newactive.credits + 1), 0); // max added by Scott
	desc->Anchor.state = ACTIVE;
	FLUSH(&desc->Anchor);

#ifdef DEBUG
	fprintf(stderr, "MallocFromNewSB() sz %u, maxcount %u, Anchor.count %hu, newactive.credits %hu, max %ld\n", 
			desc->sz, desc->maxcount, desc->Anchor.count, newactive.credits, 
			((signed long)desc->maxcount - 1 ) - ((signed long)newactive.credits + 1));
	fflush(stderr);
#endif

	// memory fence.
	FLUSHFENCE;
	if (atomic_compare_exchange_strong((volatile uint64_t*)&heap->Active, ((uint64_t*)&oldactive), *((uint64_t*)&newactive))) { 
		FLUSH(&heap->Active);
		addr = desc->sb;
		*((char*)addr) = (char)SMALL; 
		addr += TYPE_SIZE;
		*((descriptor **)addr) = desc; 
		FLUSH(addr-TYPE_SIZE);
		FLUSH(addr);//so that desc at the head of each block won't lose
		return (void *)((uint64_t)addr + PTR_SIZE);
	} 
	else {
		//Free the superblock desc->sb.
		munmap(desc->sb, desc->heap->sc->sbsize);
		DescRetire(desc); 
		return NULL;
	}
}

static procheap* find_heap(size_t sz)
{
	procheap* heap;
  
	// We need to fit both the object and the descriptor in a single block
	sz += HEADER_SIZE;
	if (sz > 2048) {
		return NULL;
	}
  
	heap = heaps[sz / GRANULARITY];
	if (heap == NULL) {
		heap = mmap(NULL, sizeof(procheap), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		*((uint64_t*)&(heap->Active)) = 0;
		heap->Partial = NULL;
		heap->sc = &sizeclasses[sz / GRANULARITY];
		FLUSH(&heap->Partial);
		FLUSH(&heap->sc);
		heaps[sz / GRANULARITY] = heap;
	}
	
	return heap;
}

static void* alloc_large_block(size_t sz)
{
	void* addr;
	addr = mmap(NULL, sz + HEADER_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	// If the highest bit of the descriptor is 1, then the object is large (allocated / freed directly from / to the OS)
	*((char*)addr) = (char)LARGE;
	addr += TYPE_SIZE;
	*((uint64_t *)addr) = sz + HEADER_SIZE;
	FLUSH(addr-TYPE_SIZE);
	FLUSH(addr);//so that desc at the head of each block won't lose
	return (void*)(addr + PTR_SIZE); 
}

void* malloc(size_t sz)
{ 
	procheap *heap;
	void* addr;

#ifdef DEBUG
	fprintf(stderr, "malloc() sz %lu\n", sz);
	fflush(stderr);
#endif
	// Use sz and thread id to find heap.
	heap = find_heap(sz);

	if (!heap) {
		// Large block
		addr = alloc_large_block(sz);
#ifdef DEBUG
		fprintf(stderr, "Large block allocation: %p\n", addr);
		fflush(stderr);
#endif
		FLUSHFENCE;
		return addr;
	}

	while(1) { 
		addr = MallocFromActive(heap);
		if (addr) {
#ifdef DEBUG
			fprintf(stderr, "malloc() return MallocFromActive %p\n", addr);
			fflush(stderr);
#endif
			FLUSHFENCE;
			return addr;
		}
		addr = MallocFromPartial(heap);
		if (addr) {
#ifdef DEBUG
			fprintf(stderr, "malloc() return MallocFromPartial %p\n", addr);
			fflush(stderr);
#endif
			FLUSHFENCE;
			return addr;
		}
		addr = MallocFromNewSB(heap);
		if (addr) {
#ifdef DEBUG
			fprintf(stderr, "malloc() return MallocFromNewSB %p\n", addr);
			fflush(stderr);
#endif
			FLUSHFENCE;
			return addr;
		}
	} 
}

void free(void* ptr) 
{
	descriptor* desc;
	void* sb;
	anchor oldanchor, newanchor;
	procheap* heap = NULL;

#ifdef DEBUG
	fprintf(stderr, "Calling my free %p\n", ptr);
	fflush(stderr);
#endif

	if (!ptr) {
		return;
	}
	
	// get prefix
	ptr = (void*)((uint64_t)ptr - HEADER_SIZE);  
	if (*((char*)ptr) == (char)LARGE) {
#ifdef DEBUG
		fprintf(stderr, "Freeing large block\n");
		fflush(stderr);
#endif
		munmap(ptr, *((uint64_t *)(ptr + TYPE_SIZE)));
		return;
	}
	desc = *((descriptor**)((uint64_t)ptr + TYPE_SIZE));
	
	sb = desc->sb;
	oldanchor = desc->Anchor;
	do { 
		newanchor = oldanchor;

		*((uint64_t*)ptr) = oldanchor.avail;
		newanchor.avail = ((uint64_t)ptr - (uint64_t)sb) / desc->sz;

		if (oldanchor.state == FULL) {
#ifdef DEBUG
			fprintf(stderr, "Marking superblock %p as PARTIAL\n", sb);
			fflush(stderr);
#endif
			newanchor.state = PARTIAL;
		}

		if (oldanchor.count == desc->maxcount - 1) {
			heap = desc->heap;
			// instruction fence.
#ifdef DEBUG
			fprintf(stderr, "Marking superblock %p as EMPTY; count %d\n", sb, oldanchor.count);
			fflush(stderr);
#endif
			newanchor.state = EMPTY;
		} 
		else {
			++newanchor.count;
		}
		// memory fence.
		FLUSHFENCE;
	} while (!atomic_compare_exchange_weak((volatile uint64_t*)&desc->Anchor, ((uint64_t*)&oldanchor), *((uint64_t*)&newanchor)));
	FLUSH(&desc->Anchor);
	FLUSHFENCE;
	if (newanchor.state == EMPTY) {
#ifdef DEBUG
		fprintf(stderr, "Freeing superblock %p with desc %p (count %hu)\n", sb, desc, desc->Anchor.count);
		fflush(stderr);
#endif

		munmap(sb, heap->sc->sbsize);
		RemoveEmptyDesc(heap, desc);
	} 
	else if (oldanchor.state == FULL) {
#ifdef DEBUG
		fprintf(stderr, "Puting superblock %p to PARTIAL heap\n", sb);
		fflush(stderr);
#endif
		HeapPutPartial(desc);
	}
}

void *calloc(size_t nmemb, size_t size)
{//not persistent!
	void *ptr;
	
	ptr = malloc(nmemb*size);
	if (!ptr) {
		return NULL;
	}

	return memset(ptr, 0, nmemb*size);
}

void *valloc(size_t size)
{
	fprintf(stderr, "valloc() called in libmaged. Not implemented. Exiting.\n");
	fflush(stderr);
	exit(1);
}

void *memalign(size_t boundary, size_t size)
{
	void *p;

	p = malloc((size + boundary - 1) & ~(boundary - 1));
	if (!p) {
		return NULL;
	}

	return(void*)(((uint64_t)p + boundary - 1) & ~(boundary - 1)); 
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	*memptr = memalign(alignment, size);
	if (*memptr) {
		return 0;
	}
	else {
		/* We have to "personalize" the return value according to the error */
		return -1;
	}
}

void *realloc(void *object, size_t size)
{//not persistent!
	descriptor* desc;
	void* header;
	void* ret;

	if (object == NULL) {
		return malloc(size);
	}
	else if (size == 0) {
		free(object);
		return NULL;
	}

	header = (void*)((uint64_t)object - HEADER_SIZE);  

	if (*((char*)header) == (char)LARGE) {
		ret = malloc(size);
		memcpy(ret, object, *((uint64_t *)(header + TYPE_SIZE)));
		munmap(object, *((uint64_t *)(header + TYPE_SIZE)));
	}
	else {
		desc = *((descriptor**)((uint64_t)header + TYPE_SIZE));
		if (size <= desc->sz - HEADER_SIZE) {
			ret = object;
		}
		else {
			ret = malloc(size);
			memcpy(ret, object, desc->sz - HEADER_SIZE);
			free(object);
		}
	}

	return ret;
}
