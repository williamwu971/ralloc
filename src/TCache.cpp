/*
 * Copyright (C) 2019 University of Rochester. All rights reserved.
 * Licenced under the MIT licence. See LICENSE file in the project root for
 * details. 
 */

/*
 * Copyright (C) 2018 Ricardo Leite
 * Licenced under the MIT licence. This file shares some portion from
 * LRMalloc(https://github.com/ricleite/lrmalloc) and its copyright 
 * is retained. See LICENSE for details about MIT License.
 */

#include "TCache.hpp"

using namespace ralloc;
thread_local TCaches ralloc::t_caches;

#include <x86intrin.h>

inline
uint64_t readTSC(int front, int back) {
    if (front)_mm_mfence();
    uint64_t tsc = __rdtsc();
    if (back)_mm_mfence();
    return tsc;
}
#include <libpmem.h>

void TCacheBin::push_block(char* block)
{
    uint64_t a,b,c;

    a= readTSC(1,1);

	// block has at least sizeof(char*)
//	*(pptr<char>*)block = _block;
    pmem_memcpy_persist(block,&_block,sizeof(char*));

    b= readTSC(1,1);

	_block = block;
	_block_num++;

    c= readTSC(1,1);

    static int times=0;

    if (times++%1000==0){
        times=0;
//        printf("tc a-b %lu b-c %lu\n",b-a,c-b);
    }
}

void TCacheBin::push_list(char* block, uint32_t length)
{
	// caller must ensure there's no available block
	// this op is only used to fill empty cache
	assert(_block_num == 0);

	_block = block;
	_block_num = length;
}

char* TCacheBin::pop_block()
{
    // caller must ensure there's an available block
    assert(_block_num > 0);

    char* ret = _block;

    if ((char*)(*(pptr<char>*)ret)==NULL && _block_idx<_maxcount-1){
        *(pptr<char>*)ret = _superblock + (_block_idx++ + 1) * _block_size;
    }

    char* next = (char*)(*(pptr<char>*)ret);

    _block = next;
    _block_num--;
    return ret;
}

void TCacheBin::pop_list(char* block, uint32_t length)
{
	assert(_block_num >= length);

	_block = block;
	_block_num -= length;
}
