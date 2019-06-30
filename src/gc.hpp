#ifndef _GC_HPP_
#define _GC_HPP_

#include <set>
#include "pptr.hpp"

#include "pm_config.hpp"
#include "RegionManager.hpp"
#include "BaseMeta.hpp"

namespace rpmalloc{
	BaseMeta* base_md;
	Regions* _rgs;
}
class GarbageCollection{
public:
	GarbageCollection(){};
	struct gc_ptr_base{
		void* ptr;
		size_t sz;
		gc_ptr_base(void* p, size_t s):ptr(p), sz(s){};
		virtual void filter_func() {
			char* curr = reinterpret_cast<char*>(ptr);
			for(int i=0;i<sz;i++){
				mark_func(*reinterpret_cast<pptr<void>*>(curr));
				curr++;
			}
		}
	};

	// persistent roots are gc_ptr with cross to be true
	template<class T>
	struct gc_ptr : public gc_ptr_base{
		gc_ptr(T* v){
			ptr = (void*)v;
			Descriptor* desc = rpmalloc::base_md->desc_lookup((char*)v);
			sz = desc->block_size();
		};
		gc_ptr(char* v){
			ptr = (void*)v;
			Descriptor* desc = rpmalloc::base_md->desc_lookup(v);
			sz = desc->block_size();
		};
		operator T*(){return (T*)ptr;};//cast to transient pointer
		T& operator *(){return *(T*)ptr;}//dereference
		T* operator ->(){return (T*)ptr;}//arrow
		virtual void filter_func(){
			return gc_ptr_base::filter_func();
		}
	};

	void operator() () {
		DBG_PRINT("Start garbage collection...\n");

		// Step 0: initialize all transient data
		base_md->avail_sb.off.store(nullptr); // initialize avail_sb
		for(int i = 0; i< MAX_SZ_IDX; i++) {
			// initialize partial list of each heap
			base_md->heaps[i].partial_list.off.store(nullptr);
		}

		// Step 1: mark all accessible blocks from roots
		for(int i = 0; i < MAX_ROOTS; i++) {
			if(!(base_md->roots[i]==nullptr)) {
				gc_ptr_base* p = base_md->roots_gc_ptr[i](base_md->roots[i]);
				marked_blk.insert(p->ptr);
				p->filter_func();
			}
		}

		// Step 2: sweep phase, update variables.
		char* curr_sb = _rgs->translate(SB_IDX, SBSIZE); // starting from first sb
		Descriptor* curr_desc = base_md->desc_lookup(curr_sb);
		auto curr_marked_blk = marked_blk.begin();
		char* sb_end = _rgs->regions[SB_IDX]->curr_addr_ptr->load();
		Descriptor* avail_sb = nullptr; // head of new free sb list
		Descriptor* next_partial[MAX_SZ_IDX] = {nullptr};

		// go through all sb in the region
		while(curr_sb < sb_end) {
			Anchor anchor(0, 0, SB_EMPTY);
			char* free_blocks_head = nullptr;
			char* last_possible_free_block = curr_sb;

			// go through all curr_marked_blk that's in this sb
			while (curr_marked_blk!=marked_blk.end() && 
					((uint64_t)curr_sb>>SB_SHIFT) == ((uint64_t)(*curr_marked_blk)>>SB_SHIFT)) { 
				// curr_marked_blk doesn't reach the end of marked_blk and curr_marked_blk is in curr_sb
				assert(!(curr_desc->heap == nullptr));

				if(curr_desc->heap->sc_idx == 0) {
					// large sb that's in use
					assert((*curr_marked_blk) == curr_sb);
					assert(curr_desc->superblock == curr_sb);
					assert(curr_desc->maxcount == 1);
					anchor.state = SB_FULL; // set it as full
				} 
				else {
					// small sb that's in use
					assert(curr_desc->superblock == curr_sb);

					for(char* free_block = last_possible_free_block; 
						free_block < (*curr_marked_blk); free_block+=curr_desc->block_size){
						// put last_possible_free_block...(curr_marked_blk-1) to free blk list
						(*reinterpret_cast<pptr<char>*>(free_block)) = free_blocks_head;
						free_blocks_head = free_block;
						anchor.count++;
						anchor.state = SB_PARTIAL;
					}
					last_possible_free_block = (*curr_marked_blk)+curr_desc->block_size;
				}
				// update local desc info
				curr_marked_blk++;
			}

			if(anchor.state == SB_EMPTY) {
				// curr_sb isn't in use
				new (curr_desc) Descriptor();
				curr_desc->next_free.store(avail_sb);
				avail_sb = curr_desc;
			} else {
				if(curr_desc->heap->sc_idx == 0) {
					// large sb that's in use

					anchor.avail = 0;
					anchor.count = 0;
					anchor.state = SB_FULL;

					// set transient variables in curr_desc
					curr_desc->next_free.store(nullptr);
					curr_desc->next_partial.store(nullptr);
					curr_desc->anchor.store(anchor);
					FLUSH(curr_desc);

					// move curr_sb to the sb next to this large sb
					curr_sb+=curr_desc->block_size;
					curr_desc = base_md->desc_lookup(curr_sb);
				} else {
					// small sb that's in use
					if(anchor.count == 0) { 
						// this sb is fully used
						anchor.avail = curr_desc->maxcount;
						anchor.state = SB_FULL;

						// set transient variables in curr_desc
						curr_desc->next_free.store(nullptr);
						curr_desc->next_partial.store(nullptr);
						curr_desc->anchor.store(anchor);
						FLUSH(curr_desc);
					} else {
						// this sb is partially used
						assert(free_blocks_head != nullptr);
						anchor.avail = (uint64_t)(free_blocks_head - curr_sb)/curr_desc->block_size;
						anchor.state = SB_PARTIAL; // it must be SB_PARTIAL already but we assign it anyway

						// set transient variables in curr_desc
						curr_desc->next_free.store(nullptr);
						base_md->heap_push_partial(curr_desc);
						curr_desc->anchor.store(anchor);
						FLUSH(curr_desc);
					}
					// move curr_sb and curr_desc to next sb
					curr_sb+=SBSIZE;
					curr_desc++;
				}
			}
		}
		// store head of new free sb list into base_md
		ptr_cnt<Descriptor> tmp_avail_sb(avail_sb, 0);
		base_md->avail_sb.store(tmp_avail_sb);
		FLUSHFENCE;
		DBG_PRINT("Garbage collection Completed!\n");
	}

	template<class T>
	void mark_func(const pptr<T>& ptr){
		void* addr = static_cast<void*>(ptr);
		// Step 1: check if it's a valid pptr
		if(UNLIKELY(!rpmalloc::_rgs->in_range(SB_IDX, addr))) 
			return; // return if not in range
		// Step 2: mark potential pptr
		marked_blk.insert(reinterpret_cast<char*>(addr));
		// Step 3: construct gc_ptr<T> from ptr and call its filter_func
		gc_ptr<T> gc_p(addr);
		gc_p->filter_func();
	};

	std::set<char*> marked_blk;
	std::vector<Descriptor*> free_sb;
};

#endif