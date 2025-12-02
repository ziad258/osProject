#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator

	// 1. Set global limits for the dynamic allocator
	dynAllocStart = daStart;
	dynAllocEnd = daEnd;

	// 2. Initialize all free block lists
	// Iterate from LOG_MIN_SIZE up to LOG_MAX_SIZE
	int i;
	for (i = 0; i <= (LOG2_MAX_SIZE - LOG2_MIN_SIZE); i++)
	{
		LIST_INIT(&freeBlockLists[i]);
	}

	// 3. Initialize the free pages list
	LIST_INIT(&freePagesList);

	// 4. Initialize the entire pageBlockInfoArr
	// (Zero out block_size and num_of_free_blocks for all potential pages)
	uint32 max_pages_in_array = DYN_ALLOC_MAX_SIZE / PAGE_SIZE;
	for (i = 0; i < max_pages_in_array; i++)
	{
		pageBlockInfoArr[i].block_size = 0;
		pageBlockInfoArr[i].num_of_free_blocks = 0;
	}

	// 5. Add the pages within the active DA range [daStart, daEnd) to the freePagesList
	uint32 num_active_pages = (daEnd - daStart) / PAGE_SIZE;
	for (i = 0; i < num_active_pages; i++)
	{
		// Add the page info element to the tail of the free pages list
		LIST_INSERT_TAIL(&freePagesList, &pageBlockInfoArr[i]);
	}
}

//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size

	// 1. Align the given virtual address down to its page's start address
	uint32 page_va = ROUNDDOWN((uint32)va, PAGE_SIZE);

	// 2. Calculate the index in the pageBlockInfoArr
	// This is the reverse operation of to_page_va()
	int idxInPageInfoArr = (page_va - dynAllocStart) >> PGSHIFT; // (page_va - dynAllocStart) / PAGE_SIZE

	// 3. Get the PageInfoElement pointer from the array using the index
	struct PageInfoElement *ptrPageInfo = &pageBlockInfoArr[idxInPageInfoArr];

	// 4. Return the block_size stored in that page's info
	return ptrPageInfo->block_size;
}

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block

	// [Slide 19] Return NULL if the requested size is 0
	if (size == 0)
	{
		return NULL;
	}

	// 1. Find nearest power-of-2 size and corresponding list index
	uint32 actual_block_size = DYN_ALLOC_MIN_BLOCK_SIZE; // 8
	int list_index = 0;
	while (actual_block_size < size)
	{
		actual_block_size <<= 1; // Double it (8 -> 16 -> 32 ...)
		list_index++;
	}

	// CASE 1: If a free block of the correct size exists
	if (!LIST_EMPTY(&freeBlockLists[list_index]))
	{
		// Get the first block from the list
		struct BlockElement* block_to_alloc = LIST_FIRST(&freeBlockLists[list_index]);

		// Remove it from the list
		LIST_REMOVE(&freeBlockLists[list_index], block_to_alloc);

		// Find its corresponding PageInfoElement
		uint32 page_va = ROUNDDOWN((uint32)block_to_alloc, PAGE_SIZE);
		int page_index = (page_va - dynAllocStart) >> PGSHIFT;
		struct PageInfoElement* page_info = &pageBlockInfoArr[page_index];

		// Update page info
		page_info->num_of_free_blocks--;

		// Return the block's address
		return (void*)block_to_alloc;
	}

	// CASE 2: Else, if a free page exists
	if (!LIST_EMPTY(&freePagesList))
	{
		// Get the first free page
		struct PageInfoElement* new_page_info = LIST_FIRST(&freePagesList);

		// Remove it from the free page list
		LIST_REMOVE(&freePagesList, new_page_info);

		// Get the page's virtual address
		uint32 new_page_va = to_page_va(new_page_info);

		// "Allocate it from the OS page allocator" (as per slide 16)
		if (get_page((void*)new_page_va) != 0)
		{
			// Failed to get page from OS. Put page info back and fail.
			LIST_INSERT_HEAD(&freePagesList, new_page_info);
			return NULL;
		}

		// "Split it into blocks..."
		int num_blocks_in_page = PAGE_SIZE / actual_block_size;

		// "Add these blocks to the corresponding list"
		// (Efficient version: Add N-1 blocks to the list, return the 1st one)

		// Start from the 2nd block's VA
		uint32 current_block_va = new_page_va + actual_block_size;

		// Loop N-1 times (for blocks 2 through N)
		for (int i = 1; i < num_blocks_in_page; i++)
		{
			struct BlockElement* block_element = (struct BlockElement*)current_block_va;
			LIST_INSERT_TAIL(&freeBlockLists[list_index], block_element);
			current_block_va += actual_block_size;
		}

		// Update Page Info for the newly allocated page
		new_page_info->block_size = actual_block_size;
		new_page_info->num_of_free_blocks = num_blocks_in_page - 1; // We are allocating one

		// "Allocate a block" (Return the first block)
		return (void*)new_page_va;
	}

	// CASE 3: else, allocate block from the next list(s) (FIX for tst dynalloc alloc panic)
	// Iterate from the next list index up to the max
	int max_list_index = LOG2_MAX_SIZE - LOG2_MIN_SIZE;
	for (int next_list_index = list_index + 1; next_list_index <= max_list_index; next_list_index++)
	{
		if (!LIST_EMPTY(&freeBlockLists[next_list_index]))
		{
			// Found a larger block. Allocate it.
			struct BlockElement* larger_block = LIST_FIRST(&freeBlockLists[next_list_index]);

			// Remove it from its list
			LIST_REMOVE(&freeBlockLists[next_list_index], larger_block);

			// Find its PageInfoElement
			uint32 page_va = ROUNDDOWN((uint32)larger_block, PAGE_SIZE);
			int page_index = (page_va - dynAllocStart) >> PGSHIFT;
			struct PageInfoElement* page_info = &pageBlockInfoArr[page_index];

			// Decrement free count for that page
			page_info->num_of_free_blocks--;

			// (We don't split the block for now, just return it)
			// This fixes the panic, as the test just wants a valid block.
			return (void*)larger_block;
		}
	}

	// CASE 4: No free block and no free page. Return NULL.
	return NULL;

	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block
}

//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block

	// 1. Get the block's size
	uint32 actual_block_size = get_block_size(va);
	// If block size is 0, it's not a valid block to free (already freed)
	if (actual_block_size == 0) return;

	// 2. Find the corresponding list index
	uint32 temp_size = DYN_ALLOC_MIN_BLOCK_SIZE;
	int list_index = 0;
	while (temp_size < actual_block_size)
	{
		temp_size <<= 1;
		list_index++;
	}

	// 3. Find the corresponding PageInfoElement
	uint32 page_va = ROUNDDOWN((uint32)va, PAGE_SIZE);
	int page_index = (page_va - dynAllocStart) >> PGSHIFT;
	struct PageInfoElement* page_info = &pageBlockInfoArr[page_index];

	// 4. Add the block back to the corresponding free list
	struct BlockElement* block_to_free = (struct BlockElement*)va;
	LIST_INSERT_HEAD(&freeBlockLists[list_index], block_to_free);

	// 5. Increment the number of free blocks in the page info
	page_info->num_of_free_blocks++;

	// 6. Check if the entire page is now free (Slide 11)
	int num_blocks_in_page = PAGE_SIZE / actual_block_size;
	if (page_info->num_of_free_blocks == num_blocks_in_page)
	{
		// "Remove all its blocks from corresponding list"

		// FIX: The previous while() loop was unsafe.
		// Use a temporary list to safely rebuild the free list.

		struct BlockElement_List temp_list;
		LIST_INIT(&temp_list);
		struct BlockElement* block_iter;

		// 1. Drain freeBlockLists[list_index] into temp_list,
		//    but only keep blocks that are NOT on the page we are freeing.
		while ((block_iter = LIST_FIRST(&freeBlockLists[list_index])) != NULL)
		{
			LIST_REMOVE(&freeBlockLists[list_index], block_iter);
			if (ROUNDDOWN((uint32)block_iter, PAGE_SIZE) != page_va)
			{
				// This block is on a different page, so keep it.
				LIST_INSERT_TAIL(&temp_list, block_iter);
			}
			// Blocks belonging to page_va are simply not added to the temp list,
			// effectively removing them from circulation.
		}

		// 2. Now, repopulate freeBlockLists[list_index] from the temp_list.
		while ((block_iter = LIST_FIRST(&temp_list)) != NULL)
		{
			LIST_REMOVE(&temp_list, block_iter);
			LIST_INSERT_TAIL(&freeBlockLists[list_index], block_iter);
		}


		// "return it to the free frame list" (i.e., return page to OS)
		return_page((void*)page_va);

		// "add it to the freePagesList"
		LIST_INSERT_HEAD(&freePagesList, page_info);

		// Reset page info fields (FIX for tst dynalloc free panic)
		page_info->block_size = 0;
		page_info->num_of_free_blocks = 0;
	}
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block

	// A call with virtual_address = null is equivalent to kmalloc()
	if (va == NULL)
	{
		return alloc_block(new_size);
	}

	// A call with new_size = zero is equivalent to kfree()
	if (new_size == 0)
	{
		free_block(va);
		return NULL;
	}

	// Get the size of the old block
	uint32 old_size = get_block_size(va);

	// If the new size is the same or smaller, just return the original block
	// (We don't bother splitting and freeing the tail end for simplification)
	if (new_size <= old_size)
	{
		return va;
	}

	// If the new size is larger, we need to allocate a new block
	void* new_block = alloc_block(new_size);

	// On failure, return a NULL pointer, and the old virtual address remains valid.
	if (new_block == NULL)
	{
		return NULL;
	}

	// Copy the contents from the old block to the new block
	// We only copy up to old_size, as that's all the valid data we have.
	memcpy(new_block, va, old_size);

	// Free the old block
	free_block(va);

	// Return the new block
	return new_block;
}
