#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include "../conc/kspinlock.h"

// We MUST include this to get the list macro definitions
#include "inc/queue.h"

//
// These are the one-and-only DEFINITIONS of the global variables
//
uint32 kheapPageAllocStart;
uint32 kheapPageAllocBreak;
uint32 kheapPlacementStrategy;

//==================================================================================//
//==================== PAGE ALLOCATOR DATA STRUCTURES ==============================//
//==================================================================================//

// Structure to track allocated/free blocks in the page allocator
struct PageAllocBlock {
    uint32 start_va;
    uint32 num_pages;
    // This field name MUST be "prev_next_info" to match the macros in queue.h
    LIST_ENTRY(PageAllocBlock) prev_next_info;
};

// List of FREE blocks (holes) in the page allocator area
LIST_HEAD(FreeBlockList, PageAllocBlock) free_blocks_list;

// List of USED blocks in the page allocator area
LIST_HEAD(UsedBlockList, PageAllocBlock) used_blocks_list;


//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
void kheap_init()
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		initialize_dynamic_allocator(KERNEL_HEAP_START, KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
		set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
		kheapPageAllocStart = dynAllocEnd + PAGE_SIZE; //
		kheapPageAllocBreak = kheapPageAllocStart; //
	}
	//==================================================================================
	//==================================================================================

	// Initialize the lists for the page allocator
	LIST_INIT(&free_blocks_list);
	LIST_INIT(&used_blocks_list);
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE), PERM_WRITEABLE, 1);
	if (ret < 0)
		panic("get_page() in kern: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	unmap_frame(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE)); //
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
void* kmalloc(unsigned int size)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc

	if (size == 0) {
		return NULL;
	}

	if(size <= DYN_ALLOC_MAX_BLOCK_SIZE){
		return alloc_block(size); //
	}

	uint32 needed_pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;

	struct PageAllocBlock *exact_fit = NULL;
	struct PageAllocBlock *worst_fit = NULL;
	struct PageAllocBlock *block;


	LIST_FOREACH(block, &free_blocks_list) {
		if (block->num_pages == needed_pages) {
			exact_fit = block;
			break;
		}
		if (block->num_pages > needed_pages) {
			if (worst_fit == NULL || block->num_pages > worst_fit->num_pages) {
				worst_fit = block;
			}
		}
	}

	struct PageAllocBlock *block_to_use = (exact_fit != NULL) ? exact_fit : worst_fit;
	uint32 alloc_va;

	if (block_to_use != NULL) {

		alloc_va = block_to_use->start_va;
		LIST_REMOVE(&free_blocks_list, block_to_use);

		if (block_to_use->num_pages > needed_pages) {

			struct PageAllocBlock *new_free_block = (struct PageAllocBlock*)alloc_block(sizeof(struct PageAllocBlock));
			if (new_free_block == NULL) panic("kmalloc: out of metadata memory!");

			new_free_block->start_va = alloc_va + needed_pages * PAGE_SIZE;
			new_free_block->num_pages = block_to_use->num_pages - needed_pages;
			LIST_INSERT_HEAD(&free_blocks_list, new_free_block);
		}


		block_to_use->start_va = alloc_va;
		block_to_use->num_pages = needed_pages;
		LIST_INSERT_HEAD(&used_blocks_list, block_to_use);


		for (int i = 0; i < needed_pages; i++) {
			int ret = get_page((void*)(alloc_va + i * PAGE_SIZE));
			if (ret < 0) panic("kmalloc get page failed in existing hole");
		}

		return (void*)alloc_va;

	} else {
		if(needed_pages * PAGE_SIZE>KERNEL_HEAP_MAX-kheapPageAllocBreak){
			return NULL;
		}
		alloc_va = kheapPageAllocBreak;
		uint32 new_break = alloc_va + needed_pages * PAGE_SIZE;

		if (new_break > KERNEL_HEAP_MAX) {
			return NULL;
		}

		for (int i = 0; i < needed_pages; i++) {
			int ret = alloc_page(ptr_page_directory, alloc_va + i * PAGE_SIZE, PERM_WRITEABLE, 1);
			if (ret < 0) {

				for(int j = 0; j < i; j++) {
					unmap_frame(ptr_page_directory, alloc_va + j * PAGE_SIZE);
				}
				return NULL;
			}
		}

		kheapPageAllocBreak = new_break;

		struct PageAllocBlock *new_used_block = (struct PageAllocBlock*)alloc_block(sizeof(struct PageAllocBlock));
		if (new_used_block == NULL) panic("kmall??");

		new_used_block->start_va = alloc_va;
		new_used_block->num_pages = needed_pages;
		LIST_INSERT_HEAD(&used_blocks_list, new_used_block);

		return (void*)alloc_va;
	}
}

//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void* virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #2 kfree
	if (virtual_address == NULL) return;

	uint32 va = (uint32)virtual_address;

	// 1. Block Allocator
	if (va >= KERNEL_HEAP_START && va < kheapPageAllocStart) {
		free_block(virtual_address); //
		return;
	}

	// 2. Page Allocator
	if (va >= kheapPageAllocStart && va < KERNEL_HEAP_MAX) {
		struct PageAllocBlock *block_to_free = NULL;

		// Find in used_list
		LIST_FOREACH(block_to_free, &used_blocks_list) {
			if (block_to_free->start_va == va) {
				break;
			}
		}

		if (block_to_free == NULL) {
			panic("kfree: invalid virtual address or not allocated!"); //
		}

		// FREE the space from RAM
		for (int i = 0; i < block_to_free->num_pages; i++) {
			unmap_frame(ptr_page_directory, va + i * PAGE_SIZE);
		}

		LIST_REMOVE(&used_blocks_list, block_to_free);

		// Merge adjacent blocks
		struct PageAllocBlock *prev_free = NULL;
		struct PageAllocBlock *next_free = NULL;
		struct PageAllocBlock *iter_block;

		uint32 free_end = va + block_to_free->num_pages * PAGE_SIZE;

		LIST_FOREACH(iter_block, &free_blocks_list) {
			if (iter_block->start_va + iter_block->num_pages * PAGE_SIZE == va) {
				prev_free = iter_block;
			} else if (free_end == iter_block->start_va) {
				next_free = iter_block;
			}
		}

		// Perform merging
		if (prev_free != NULL) {
			prev_free->num_pages += block_to_free->num_pages;
			free_block(block_to_free); // Free metadata struct
			block_to_free = prev_free; // The merged block is now prev_free
			if (next_free != NULL) {
				block_to_free->num_pages += next_free->num_pages;
				LIST_REMOVE(&free_blocks_list, next_free);
				free_block(next_free);
			}
		} else if (next_free != NULL) {
			next_free->num_pages += block_to_free->num_pages;
			next_free->start_va = va;
			free_block(block_to_free); // Free metadata struct
			block_to_free = next_free; // The merged block is now next_free
		} else {
			// No merge, just add to free list
			LIST_INSERT_HEAD(&free_blocks_list, block_to_free);
		}

		// Update the kheapPageAllocBreak if freeing the last space
		if (block_to_free->start_va + block_to_free->num_pages * PAGE_SIZE == kheapPageAllocBreak) {
			kheapPageAllocBreak = block_to_free->start_va;
			LIST_REMOVE(&free_blocks_list, block_to_free);
			free_block(block_to_free); // Free metadata struct
		}

		return;
	}

	// 3. Invalid address
	panic("kfree: virtual address out of kernel heap range!");
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #3 kheap_virtual_address

	// 1. Calculate the offset within the page (last 12 bits)
	uint32 offset = physical_address & 0xFFF;

	// 2. Get the Frame Page Address (remove offset)
	uint32 frame_pa = physical_address & 0xFFFFF000;

	// 3. Search the KERNEL HEAP range for this physical address
	// NOTE: This is O(N) because we cannot modify struct FrameInfo to store the VA
	for (uint32 va = KERNEL_HEAP_START; va < KERNEL_HEAP_MAX; va += PAGE_SIZE)
	{
		uint32* ptr_page_table = NULL;

		// We use get_page_table to safely access the table entry
		get_page_table(ptr_page_directory, va, &ptr_page_table);

		// If table exists and page is PRESENT
		if (ptr_page_table != NULL && (ptr_page_table[PTX(va)] & PERM_PRESENT))
		{
			// Extract the Physical Address from the table entry
			uint32 entry_pa = ptr_page_table[PTX(va)] & 0xFFFFF000;

			// If found, return the VA + original offset
			if (entry_pa == frame_pa)
			{
				return va + offset;
			}
		}
	}

	// If not found in Kernel Heap, return 0 [cite: 901]
	return 0;
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
	//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #4 kheap_physical_address

	// 1. Get the page table for the virtual address
	uint32* ptr_page_table = NULL;
	get_page_table(ptr_page_directory, virtual_address, &ptr_page_table);

	// 2. Check if the page table exists and the page is present
	if (ptr_page_table != NULL && (ptr_page_table[PTX(virtual_address)] & PERM_PRESENT))
	{
		// 3. Extract the Frame Physical Address (upper 20 bits)
		uint32 frame_pa = ptr_page_table[PTX(virtual_address)] & 0xFFFFF000;

		// 4. Add the offset (lower 12 bits) from the virtual address
		uint32 offset = virtual_address & 0xFFF;

		return frame_pa + offset;
	}

	// If no mapping, return 0 [cite: 893]
	return 0;
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//
// krealloc():

//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to kmalloc().
//	A call with new_size = zero is equivalent to kfree().

extern __inline__ uint32 get_block_size(void *va);

void *krealloc(void *virtual_address, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - krealloc

	// A call with virtual_address = null is equivalent to kmalloc()
	if (virtual_address == NULL) {
		return kmalloc(new_size);
	}

	// A call with new_size = zero is equivalent to kfree()
	if (new_size == 0) {
		kfree(virtual_address);
		return NULL;
	}

	uint32 va = (uint32)virtual_address;
	uint32 old_size;

	// Check if it's in the block allocator
	if (va >= KERNEL_HEAP_START && va < kheapPageAllocStart) {
		old_size = get_block_size(virtual_address);
		if (old_size == 0) panic("krealloc: invalid block address!");

		// If new size fits in the same block, just return
		if (new_size <= old_size) {
			return virtual_address;
		}

		// --- Cross-allocator check ---
		if (new_size > DYN_ALLOC_MAX_BLOCK_SIZE) {
			// Growing from block to page
			void* new_va = kmalloc(new_size);
			if (new_va == NULL) return NULL;
			memcpy(new_va, virtual_address, old_size);
			kfree(virtual_address);
			return new_va;
		}

		// Growing within block allocator (must move)
		void* new_va = alloc_block(new_size);
		if (new_va == NULL) return NULL; // Failed to allocate
		memcpy(new_va, virtual_address, old_size);
		free_block(virtual_address);
		return new_va;
	}
	// Check if it's in the page allocator
	else if (va >= kheapPageAllocStart && va < KERNEL_HEAP_MAX) {
		struct PageAllocBlock *block_to_resize = NULL;
		LIST_FOREACH(block_to_resize, &used_blocks_list) {
			if (block_to_resize->start_va == va) {
				break;
			}
		}
		if (block_to_resize == NULL) panic("krealloc: invalid page address!");

		old_size = block_to_resize->num_pages * PAGE_SIZE;

		// --- Cross-allocator check ---
		if (new_size <= DYN_ALLOC_MAX_BLOCK_SIZE) {
			// Shrinking from page to block
			void* new_va = alloc_block(new_size);
			if (new_va == NULL) return NULL;
			memcpy(new_va, virtual_address, new_size); // Only copy new_size
			kfree(virtual_address);
			return new_va;
		}

		// Staying within page allocator
		uint32 needed_pages = ROUNDUP(new_size, PAGE_SIZE) / PAGE_SIZE;

		if (needed_pages == block_to_resize->num_pages) {
			return virtual_address; // Size is the same
		}

		if (needed_pages < block_to_resize->num_pages) {
			// Shrinking
			uint32 pages_to_free = block_to_resize->num_pages - needed_pages;
			uint32 free_va = va + needed_pages * PAGE_SIZE;

			// Temporarily create a fake used block to free
			struct PageAllocBlock *temp_free_block = (struct PageAllocBlock*)alloc_block(sizeof(struct PageAllocBlock));
			if(temp_free_block == NULL) panic("krealloc: out of metadata!");

			temp_free_block->start_va = free_va;
			temp_free_block->num_pages = pages_to_free;
			LIST_INSERT_HEAD(&used_blocks_list, temp_free_block);

			// kfree will handle unmapping and merging
			kfree((void*)free_va);

			block_to_resize->num_pages = needed_pages; // Update original block size
			return virtual_address;
		}

		// Growing (must move)
		void* new_va = kmalloc(new_size);
		if (new_va == NULL) return NULL;
		memcpy(new_va, virtual_address, old_size);
		kfree(virtual_address);
		return new_va;

	} else {
		panic("krealloc: invalid address!");
	}
}
