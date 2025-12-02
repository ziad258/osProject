/*
 * chunk_operations.c
 *
 * Created on: Oct 12, 2022
 * Author: HP
 */

#include <kern/trap/fault_handler.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/proc/user_environment.h>
#include "kheap.h"
#include "memory_manager.h"
#include <inc/queue.h>
#include <inc/mmu.h>        // Required for PAGE_SIZE, PERM_UHPAGE
#include <inc/memlayout.h>  // Required for memory layout constants
#include <inc/types.h>      // Required for types like uint32, NULL

//extern void inctst();

/******************************/
/*[1] RAM CHUNKS MANIPULATION */
/******************************/

//===============================
// 1) CUT-PASTE PAGES IN RAM:
//===============================
int cut_paste_pages(uint32* page_directory, uint32 source_va, uint32 dest_va, uint32 num_of_pages)
{
	//TODO: PRACTICE: fill this function.
	//Comment the following line
	panic("cut_paste_pages() is not implemented yet...!!");
	return 0;
}

//===============================
// 2) COPY-PASTE RANGE IN RAM:
//===============================
int copy_paste_chunk(uint32* page_directory, uint32 source_va, uint32 dest_va, uint32 size)
{
	//TODO: PRACTICE: fill this function.
	//Comment the following line
	panic("copy_paste_chunk() is not implemented yet...!!");
	return 0;
}

//===============================
// 3) SHARE RANGE IN RAM:
//===============================
int share_chunk(uint32* page_directory, uint32 source_va,uint32 dest_va, uint32 size, uint32 perms)
{
	//TODO: PRACTICE: fill this function.
	//Comment the following line
	panic("share_chunk() is not implemented yet...!!");
	return 0;
}

//===============================
// 4) ALLOCATE CHUNK IN RAM:
//===============================
int allocate_chunk(uint32* page_directory, uint32 va, uint32 size, uint32 perms)
{
	//TODO: PRACTICE: fill this function.
	//Comment the following line
	panic("allocate_chunk() is not implemented yet...!!");
	return 0;
}


//=====================================
// 5) CALCULATE FREE SPACE:
//=====================================
uint32 calculate_free_space(uint32* page_directory, uint32 sva, uint32 eva)
{
	//TODO: PRACTICE: fill this function.
	//Comment the following line
	panic("calculate_free_space() is not implemented yet...!!");
	return 0;
}

//=====================================
// 6) CALCULATE ALLOCATED SPACE:
//=====================================
void calculate_allocated_space(uint32* page_directory, uint32 sva, uint32 eva, uint32 *num_tables, uint32 *num_pages)
{
	//TODO: PRACTICE: fill this function.
	//Comment the following line
	panic("calculate_allocated_space() is not implemented yet...!!");
}

//=====================================
// 7) CALCULATE REQUIRED FRAMES IN RAM:
//=====================================
uint32 calculate_required_frames(uint32* page_directory, uint32 sva, uint32 size)
{
	//TODO: PRACTICE: fill this function.
	//Comment the following line
	panic("calculate_required_frames() is not implemented yet...!!");
	return 0;
}

//=================================================================================//
//===========================END RAM CHUNKS MANIPULATION ==========================//
//=================================================================================//

/*******************************/
/*[2] USER CHUNKS MANIPULATION */
/*******************************/

//======================================================
/// functions used for USER HEAP (malloc, free, ...)
//======================================================

//=====================================
/* DYNAMIC ALLOCATOR SYSTEM CALLS */
//=====================================
/*2024*/
void* sys_sbrk(int numOfPages)
{
	panic("not implemented function");
	return NULL;
}

//=====================================
// 1) ALLOCATE USER MEMORY:
//=====================================
void allocate_user_mem(struct Env* e, uint32 virtual_address, uint32 size)
{
	// [PROJECT'25.IM#2] USER HEAP - #2 allocate_user_mem
	uint32 va_start = ROUNDDOWN(virtual_address, PAGE_SIZE);
	uint32 va_end = ROUNDUP(virtual_address + size, PAGE_SIZE);
	uint32 va;
	uint32 *ptr_page_table = NULL;

	for (va = va_start; va < va_end; va += PAGE_SIZE)
	{
		get_page_table(e->env_page_directory, va, &ptr_page_table);
		if (ptr_page_table == NULL)
		{
			create_page_table(e->env_page_directory, va);
			get_page_table(e->env_page_directory, va, &ptr_page_table);
		}

		if (ptr_page_table != NULL)
		{
			ptr_page_table[PTX(va)] |= (PERM_UHPAGE | PERM_USER | PERM_WRITEABLE);
		}
	}
}

//=====================================
// 2) FREE USER MEMORY:
//=====================================
void free_user_mem(struct Env* e, uint32 virtual_address, uint32 size)
{
	// [PROJECT'25.IM#2] USER HEAP - #4 free_user_mem
	uint32 va_start = ROUNDDOWN(virtual_address, PAGE_SIZE);
	uint32 va_end = ROUNDUP(virtual_address + size, PAGE_SIZE);
	uint32 va;
	uint32 *ptr_page_table = NULL;

	for (va = va_start; va < va_end; va += PAGE_SIZE)
	{
		pf_remove_env_page(e, va);
		env_page_ws_invalidate(e, va);

		get_page_table(e->env_page_directory, va, &ptr_page_table);
		if (ptr_page_table != NULL)
		{
			ptr_page_table[PTX(va)] &= ~(PERM_UHPAGE | PERM_USER | PERM_WRITEABLE | PERM_PRESENT);
		}
	}
}

//=====================================
// 4) FREE USER MEMORY (BUFFERING):
//=====================================
void __free_user_mem_with_buffering(struct Env* e, uint32 virtual_address, uint32 size)
{
	// your code is here, remove the panic and write your code
	panic("__free_user_mem_with_buffering() is not implemented yet...!!");
}

//=====================================
// 3) MOVE USER MEMORY:
//=====================================
void move_user_mem(struct Env* e, uint32 src_virtual_address, uint32 dst_virtual_address, uint32 size)
{
	panic("move_user_mem() is not implemented yet...!!");
}

//=================================================================================//
//========================== END USER CHUNKS MANIPULATION =========================//
//=================================================================================//
