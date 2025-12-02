/*
 * fault_handler.c
 *
 * Created on: Oct 12, 2022
 * Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>

/*
 * Added missing headers for constants (like USER_TOP, USER_HEAP_START, PERM_UHPAGE)
 * and error codes (like E_PAGE_NOT_EXIST_IN_PF).
 */
#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/error.h>
/* ================================================ */


//2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
// 0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
//2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
/*2024*/ void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
/*2025*/ void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };

//2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

//==================
// [0] INIT HANDLER:
//==================
void fault_handler_init()
{
	//setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
	//setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	//setPageReplacmentAlgorithmModifiedCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0) ;
	setModifiedBufferLength(1000);
}
//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;
extern uint32 sys_calculate_free_frames() ;

struct Env* last_faulted_env = NULL;

void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	// Read processor's CR2 register to find the faulting address
	uint32 fault_va = rcr2();
	//cprintf("************Faulted VA = %x************\n", fault_va);
	//	print_trapframe(tf);
	/******************************************************/

	//If same fault va for 3 times, then panic
	//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
	struct Env* cur_env = get_cpu_proc();
	if (last_fault_va == fault_va && last_faulted_env == cur_env)
	{
		num_repeated_fault++ ;
		if (num_repeated_fault == 3)
		{
			print_trapframe(tf);
			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
		}
	}
	else
	{
		before_last_fault_va = last_fault_va;
		before_last_eip = last_eip;
		num_repeated_fault = 0;
	}
	last_eip = (uint32)tf->tf_eip;
	last_fault_va = fault_va ;
	last_faulted_env = cur_env;
	/******************************************************/
	//2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3) {
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu* c = mycpu();
		//cprintf("trap from KERNEL\n");
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	//2017: Check stack underflow for User
	else
	{
		//cprintf("trap from USER\n");
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	//get a pointer to the environment that caused the fault at runtime
	//cprintf("curenv = %x\n", curenv);
	struct Env* faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		cprintf("\nFaulted VA = %x\n", fault_va);
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}
	//check the faulted address, is it a table or not ?
	//If the directory entry of the faulted address is NOT PRESENT then
	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		faulted_env->tableFaultsCounter ++ ;
		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{
			/*============================================================================================*/
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #2 Check for invalid pointers
			//(e.g. pointing to unmarked user heap page, kernel or wrong access rights),
			//your code is here

			//[1] Accessing Kernel Space from User
			if (fault_va >= USER_TOP)
			{
				//cprintf("[%s] ERROR: User environment %d trying to access KERNEL SPACE at va = %x\n", faulted_env->prog_name, faulted_env->env_id, fault_va);
				env_exit();
			}

			//[2] Accessing Unmarked User Heap Page
			if (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)
			{
				uint32* ptr_table = NULL;
				get_page_table(faulted_env->env_page_directory, fault_va, &ptr_table);

				// Check PERM_UHPAGE bit
				if (ptr_table != NULL)
				{
					if ((ptr_table[PTX(fault_va)] & PERM_UHPAGE) == 0)
					{
						//cprintf("[%s] ERROR: User environment %d trying to access UNMARKED HEAP PAGE at va = %x\n", faulted_env->prog_name, faulted_env->env_id, fault_va);
						env_exit();
					}
				}
			}

			//[3] Write Violation on Read-Only Page
			uint32 err = tf->tf_err;
			// Error Code bit 0: Present (1 = protection fault, 0 = not present)
			// Error Code bit 1: Write (1 = write fault, 0 = read fault)
			if ((err & 0x1) && (err & 0x2))
			{
				uint32* ptr_table = NULL;
				get_page_table(faulted_env->env_page_directory, fault_va, &ptr_table);

				if (ptr_table != NULL)
				{
					if ((ptr_table[PTX(fault_va)] & PERM_WRITEABLE) == 0)
					{
						//cprintf("[%s] ERROR: User environment %d trying to WRITE on READ-ONLY page at va = %x\n", faulted_env->prog_name, faulted_env->env_id, fault_va);
						env_exit();
					}
				}
			}
			/*============================================================================================*/
		}

		/*2022: Check if fault due to Access Rights */
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;
		/*============================================================================================*/


		// we have normal page fault =============================================================
		faulted_env->pageFaultsCounter ++ ;

//				cprintf("[%08s] user PAGE fault va %08x\n", faulted_env->prog_name, fault_va);
//				cprintf("\nPage working set BEFORE fault handler...\n");
//				env_page_ws_print(faulted_env);
		//int ffb = sys_calculate_free_frames();

		if(isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			page_fault_handler(faulted_env, fault_va);
		}

		//		cprintf("\nPage working set AFTER fault handler...\n");
		//		env_page_ws_print(faulted_env);
		//		int ffa = sys_calculate_free_frames();
		//		cprintf("fault handling @%x: difference in free frames (after - before = %d)\n", fault_va, ffa - ffb);
	}

	/*************************************************************/
	//Refresh the TLB cache
	tlbflush();
	/*************************************************************/
}


//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	//panic("table_fault_handler() is not implemented yet...!!");
	//Check if it's a stack page
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
/* Calculate the number of page faults according th the OPTIMAL replacement strategy
 * Given:
 * 1. Initial Working Set List (that the process started with)
 * 2. Max Working Set Size
 * 3. Page References List (contains the stream of referenced VAs till the process finished)
 *
 * IMPORTANT: This function SHOULD NOT change any of the given lists
 */
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
	//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
	//Your code is here
	//Comment the following line
	panic("get_optimal_num_faults() is not implemented yet...!!");
}


void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{
#if USE_KHEAP
	if (isPageReplacmentAlgorithmOPTIMAL())
	    {
	        //=============================================================================
	        // [1] Add the faulted page to the reference stream
	        //=============================================================================
	        struct PageRefElement *new_ref = kmalloc(sizeof(struct PageRefElement));
	        if (new_ref == NULL) panic("page_fault_handler: kmalloc failed!");
	        new_ref->virtual_address = fault_va;
	        LIST_INSERT_TAIL(&(faulted_env->referenceStreamList), new_ref);

	        //=============================================================================
	        // [2] Ensure the Page is Mapped and PRESENT in Memory
	        //=============================================================================
	        uint32 rounded_va = ROUNDDOWN(fault_va, PAGE_SIZE);
	        uint32 *ptr_table = NULL;

	        get_page_table(faulted_env->env_page_directory, rounded_va, &ptr_table);

	        // Check if page frame exists (Upper 20 bits of PTE are not zero)
	        int in_mem = 0;
	        if (ptr_table != NULL && (ptr_table[PTX(rounded_va)] & 0xFFFFF000) != 0) {
	            in_mem = 1;
	        }

	        if (!in_mem)
	        {
	            // CASE A: Page NOT in memory. Load from disk or allocate new.
	            int ret = pf_read_env_page(faulted_env, (void*)fault_va);

	            if (ret == E_PAGE_NOT_EXIST_IN_PF)
	            {
	                // CASE B: New Stack/Heap page
	                if ((fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) ||
	                    (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX))
	                {
	                    struct FrameInfo *ptr_fi = NULL;
	                    allocate_frame(&ptr_fi);
	                    map_frame(faulted_env->env_page_directory, ptr_fi, fault_va, PERM_USER | PERM_WRITEABLE);
	                }
	                else
	                {
	                    unmap_frame(faulted_env->env_page_directory, fault_va);
	                    env_exit();
	                }
	            }
	        }
	        else
	        {
	            // CASE C: Page IS in memory, just hidden (Simulated Replacement)
	            // Force setting PRESENT and WRITEABLE to prevent infinite loops on write access
	            ptr_table[PTX(rounded_va)] |= (PERM_PRESENT | PERM_USER | PERM_WRITEABLE);
	        }

	        //=============================================================================
	        // [3] Update Working Set (OPT2 Logic)
	        //=============================================================================
	        int found_in_ws = 0;
	        struct WorkingSetElement *ele = NULL;

	        LIST_FOREACH(ele, &(faulted_env->page_WS_list))
	        {
	            if (ROUNDDOWN(ele->virtual_address, PAGE_SIZE) == rounded_va)
	            {
	                found_in_ws = 1;
	                break;
	            }
	        }

	        // IF NOT in Active WS, we must add it (and possibly clear the WS if full)
	        if (!found_in_ws)
	        {
	            // Check if WS is FULL
	            if (LIST_SIZE(&(faulted_env->page_WS_list)) >= faulted_env->page_WS_max_size)
	            {
	                // If WS is FULL -> Reset present bit of ALL pages in WS & delete them
	                while (!LIST_EMPTY(&(faulted_env->page_WS_list)))
	                {
	                    struct WorkingSetElement *curr = LIST_FIRST(&(faulted_env->page_WS_list));

	                    uint32 *curr_ptr_table = NULL;
	                    get_page_table(faulted_env->env_page_directory, curr->virtual_address, &curr_ptr_table);

	                    // "Hide" the page: Clear Present bit so we trap next time it's used
	                    if (curr_ptr_table != NULL)
	                    {
	                        curr_ptr_table[PTX(curr->virtual_address)] &= ~PERM_PRESENT;
	                    }

	                    LIST_REMOVE(&(faulted_env->page_WS_list), curr);
	                    kfree(curr);
	                }

	                // FLUSH TLB to ensure the CPU sees the "Hidden" pages immediately
	                tlbflush();
	            }

	            // Add the faulted page to the Active WS
	            struct WorkingSetElement *new_wse = env_page_ws_list_create_element(faulted_env, fault_va);
	            LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_wse);
	        }

	        // Final TLB flush to be safe (ensures the NEW permissions are seen)
	        tlbflush();
	    }
	else
	{
		struct WorkingSetElement *victimWSElement = NULL;
		uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
		if(wsSize < (faulted_env->page_WS_max_size))
		{
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
			//Your code is here
			//Comment the following line
			//panic("page_fault_handler().PLACEMENT is not implemented yet...!!");

			// [1] Allocate a frame for the faulted page and map it
			struct FrameInfo *ptr_fi = NULL;
			allocate_frame(&ptr_fi);
			map_frame(faulted_env->env_page_directory, ptr_fi, fault_va, PERM_USER | PERM_WRITEABLE);

			// [2] Read the faulted page from page file to memory
			int ret = pf_read_env_page(faulted_env, (void*)fault_va);

			// [3] If the page does not exist on page file
			if (ret == E_PAGE_NOT_EXIST_IN_PF)
				{
					// Check if it is a valid Stack page or Heap page
					if ((fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) ||
						(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX))
						{
							// Valid stack/heap access
						}
					else
						{
							unmap_frame(faulted_env->env_page_directory, fault_va);
							env_exit();
						}
				}

			// [4] Reflect the changes in the page working set list

			struct WorkingSetElement* wse = env_page_ws_list_create_element(faulted_env, fault_va);
			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), wse);

			if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size)
				{
					faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
				}
			else
				{
					faulted_env->page_last_WS_element = NULL;
				}
		}
		else
		{
			if (isPageReplacmentAlgorithmCLOCK())
			{
				//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
				//Your code is here
				//Comment the following line
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
				//Your code is here
				//Comment the following line
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
			else if (isPageReplacmentAlgorithmModifiedCLOCK())
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
				//Your code is here
				//Comment the following line
				panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
		}
	}
#endif
}

void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}
/*
 * fault_handler.c
 *
 * Created on: Oct 12, 2022
 * Author: HP
 */

//#include "trap.h"
//#include <kern/proc/user_environment.h>
//#include <kern/cpu/sched.h>
//#include <kern/cpu/cpu.h>
//#include <kern/disk/pagefile_manager.h>
//#include <kern/mem/memory_manager.h>
//#include <kern/mem/kheap.h>
//
///*
// * Added missing headers for constants (like USER_TOP, USER_HEAP_START, PERM_UHPAGE)
// * and error codes (like E_PAGE_NOT_EXIST_IN_PF).
// */
//#include <inc/mmu.h>
//#include <inc/memlayout.h>
//#include <inc/error.h>
///* ================================================ */
//
//
////2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
//// 0 means don't bypass the PAGE FAULT
//uint8 bypassInstrLength = 0;
//
////===============================
//// REPLACEMENT STRATEGIES
////===============================
////2020
//void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
//{
//	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
//	_PageRepAlgoType = LRU_TYPE ;
//}
//void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
//void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
//void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
///*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
///*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
///*2024*/ void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
///*2025*/ void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };
//
////2020
//uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
//uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
//uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
//uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
///*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
///*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
///*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}
//
////===============================
//// PAGE BUFFERING
////===============================
//void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
//uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }
//
//void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
//uint8 isBufferingEnabled(){  return _EnableBuffering ; }
//
//void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
//uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}
//
////===============================
//// FAULT HANDLERS
////===============================
//
////==================
//// [0] INIT HANDLER:
////==================
//void fault_handler_init()
//{
//	//setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
//	//setPageReplacmentAlgorithmOPTIMAL();
//	setPageReplacmentAlgorithmCLOCK();
//	//setPageReplacmentAlgorithmModifiedCLOCK();
//	enableBuffering(0);
//	enableModifiedBuffer(0) ;
//	setModifiedBufferLength(1000);
//}
////==================
//// [1] MAIN HANDLER:
////==================
///*2022*/
//uint32 last_eip = 0;
//uint32 before_last_eip = 0;
//uint32 last_fault_va = 0;
//uint32 before_last_fault_va = 0;
//int8 num_repeated_fault  = 0;
//extern uint32 sys_calculate_free_frames() ;
//
//struct Env* last_faulted_env = NULL;
//
//void fault_handler(struct Trapframe *tf)
//{
//	/******************************************************/
//	// Read processor's CR2 register to find the faulting address
//	uint32 fault_va = rcr2();
//	//cprintf("************Faulted VA = %x************\n", fault_va);
//	//	print_trapframe(tf);
//	/******************************************************/
//
//	//If same fault va for 3 times, then panic
//	//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
//	struct Env* cur_env = get_cpu_proc();
//	if (last_fault_va == fault_va && last_faulted_env == cur_env)
//	{
//		num_repeated_fault++ ;
//		if (num_repeated_fault == 3)
//		{
//			print_trapframe(tf);
//			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
//		}
//	}
//	else
//	{
//		before_last_fault_va = last_fault_va;
//		before_last_eip = last_eip;
//		num_repeated_fault = 0;
//	}
//	last_eip = (uint32)tf->tf_eip;
//	last_fault_va = fault_va ;
//	last_faulted_env = cur_env;
//	/******************************************************/
//	//2017: Check stack overflow for Kernel
//	int userTrap = 0;
//	if ((tf->tf_cs & 3) == 3) {
//		userTrap = 1;
//	}
//	if (!userTrap)
//	{
//		struct cpu* c = mycpu();
//		//cprintf("trap from KERNEL\n");
//		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
//			panic("User Kernel Stack: overflow exception!");
//		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
//			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
//#if USE_KHEAP
//		if (fault_va >= KERNEL_HEAP_MAX)
//			panic("Kernel: heap overflow exception!");
//#endif
//	}
//	//2017: Check stack underflow for User
//	else
//	{
//		//cprintf("trap from USER\n");
//		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
//			panic("User: stack underflow exception!");
//	}
//
//	//get a pointer to the environment that caused the fault at runtime
//	//cprintf("curenv = %x\n", curenv);
//	struct Env* faulted_env = cur_env;
//	if (faulted_env == NULL)
//	{
//		cprintf("\nFaulted VA = %x\n", fault_va);
//		print_trapframe(tf);
//		panic("faulted env == NULL!");
//	}
//	//check the faulted address, is it a table or not ?
//	//If the directory entry of the faulted address is NOT PRESENT then
//	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
//	{
//		faulted_env->tableFaultsCounter ++ ;
//		table_fault_handler(faulted_env, fault_va);
//	}
//	else
//	{
//		if (userTrap)
//		{
//			/*============================================================================================*/
//			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #2 Check for invalid pointers
//			//(e.g. pointing to unmarked user heap page, kernel or wrong access rights),
//			//your code is here
//
//			//[1] Accessing Kernel Space from User
//			if (fault_va >= USER_TOP)
//			{
//				//cprintf("[%s] ERROR: User environment %d trying to access KERNEL SPACE at va = %x\n", faulted_env->prog_name, faulted_env->env_id, fault_va);
//				env_exit();
//			}
//
//			//[2] Accessing Unmarked User Heap Page
//			if (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)
//			{
//				uint32* ptr_table = NULL;
//				get_page_table(faulted_env->env_page_directory, fault_va, &ptr_table);
//
//				// Check PERM_UHPAGE bit
//				if (ptr_table != NULL)
//				{
//					if ((ptr_table[PTX(fault_va)] & PERM_UHPAGE) == 0)
//					{
//						//cprintf("[%s] ERROR: User environment %d trying to access UNMARKED HEAP PAGE at va = %x\n", faulted_env->prog_name, faulted_env->env_id, fault_va);
//						env_exit();
//					}
//				}
//			}
//
//			//[3] Write Violation on Read-Only Page
//			uint32 err = tf->tf_err;
//			// Error Code bit 0: Present (1 = protection fault, 0 = not present)
//			// Error Code bit 1: Write (1 = write fault, 0 = read fault)
//			if ((err & 0x1) && (err & 0x2))
//			{
//				uint32* ptr_table = NULL;
//				get_page_table(faulted_env->env_page_directory, fault_va, &ptr_table);
//
//				if (ptr_table != NULL)
//				{
//					if ((ptr_table[PTX(fault_va)] & PERM_WRITEABLE) == 0)
//					{
//						//cprintf("[%s] ERROR: User environment %d trying to WRITE on READ-ONLY page at va = %x\n", faulted_env->prog_name, faulted_env->env_id, fault_va);
//						env_exit();
//					}
//				}
//			}
//			/*============================================================================================*/
//		}
//
//		/*2022: Check if fault due to Access Rights */
//		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
//		if (perms & PERM_PRESENT)
//			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;
//		/*============================================================================================*/
//
//
//		// we have normal page fault =============================================================
//		faulted_env->pageFaultsCounter ++ ;
//
////				cprintf("[%08s] user PAGE fault va %08x\n", faulted_env->prog_name, fault_va);
////				cprintf("\nPage working set BEFORE fault handler...\n");
////				env_page_ws_print(faulted_env);
//		//int ffb = sys_calculate_free_frames();
//
//		if(isBufferingEnabled())
//		{
//			__page_fault_handler_with_buffering(faulted_env, fault_va);
//		}
//		else
//		{
//			page_fault_handler(faulted_env, fault_va);
//		}
//
//		//		cprintf("\nPage working set AFTER fault handler...\n");
//		//		env_page_ws_print(faulted_env);
//		//		int ffa = sys_calculate_free_frames();
//		//		cprintf("fault handling @%x: difference in free frames (after - before = %d)\n", fault_va, ffa - ffb);
//	}
//
//	/*************************************************************/
//	//Refresh the TLB cache
//	tlbflush();
//	/*************************************************************/
//}
//
//
////=========================
//// [2] TABLE FAULT HANDLER:
////=========================
//void table_fault_handler(struct Env * curenv, uint32 fault_va)
//{
//	//panic("table_fault_handler() is not implemented yet...!!");
//	//Check if it's a stack page
//	uint32* ptr_table;
//#if USE_KHEAP
//	{
//		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
//	}
//#else
//	{
//		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
//	}
//#endif
//}
//
////=========================
//// [3] PAGE FAULT HANDLER:
////=========================
///* Calculate the number of page faults according th the OPTIMAL replacement strategy
// * Given:
// * 1. Initial Working Set List (that the process started with)
// * 2. Max Working Set Size
// * 3. Page References List (contains the stream of referenced VAs till the process finished)
// *
// * IMPORTANT: This function SHOULD NOT change any of the given lists
// */
//int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
//{
//	//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
//	//Your code is here
//	int faults = 0;
//	// Create a temporary "Simulated Working Set" array to track simulated pages
//	// We use kmalloc to allocate dynamic array based on maxWSSize
//	uint32 *simulated_ws = kmalloc(sizeof(uint32) * maxWSSize);
//
//	// Always check for kmalloc failure to prevent kernel panics
//	if (simulated_ws == NULL) {
//		panic("get_optimal_num_faults: kmalloc failed!");
//	}
//
//	int sim_ws_count = 0;
//
//	[cite_start]// [Step 1] Initialize the simulated WS with the process's initial Working Set [cite: 466]
//	struct WorkingSetElement *ws_iter;
//	LIST_FOREACH(ws_iter, initWorkingSet)
//	{
//		simulated_ws[sim_ws_count++] = ws_iter->virtual_address;
//	}
//
//	[cite_start]// [Step 2] Iterate through the entire recorded reference stream [cite: 467-471]
//	struct PageRefElement *ref_iter;
//	LIST_FOREACH(ref_iter, pageReferences)
//	{
//		uint32 current_va = ref_iter->virtual_address;
//		int found = 0;
//
//		// Check if the referenced page is already in our simulated WS
//		for (int i = 0; i < sim_ws_count; i++)
//		{
//			// Compare Page Addresses (ROUNDDOWN to ignore offset)
//			if (ROUNDDOWN(simulated_ws[i], PAGE_SIZE) == ROUNDDOWN(current_va, PAGE_SIZE))
//			{
//				found = 1;
//				break;
//			}
//		}
//
//		if (!found)
//		{
//			// Page Fault occurred in simulation!
//			faults++;
//
//			if (sim_ws_count < maxWSSize)
//			{
//				[cite_start]// If WS is not full, just add the new page (Placement) [cite: 567]
//				simulated_ws[sim_ws_count++] = current_va;
//			}
//			else
//			{
//				[cite_start]// WS is Full -> Apply Optimal Replacement Strategy [cite: 459]
//				// Goal: Select page for which time to the next reference is the longest
//				int victim_idx = -1;
//				int max_dist = -1;
//
//				// Check every page currently in the simulated WS
//				for (int i = 0; i < sim_ws_count; i++)
//				{
//					int dist = 0;
//					int found_in_future = 0;
//					struct PageRefElement *future_iter = LIST_NEXT(ref_iter);
//
//					// Look ahead in the reference stream to find next occurrence
//					while (future_iter)
//					{
//						dist++;
//						if (ROUNDDOWN(future_iter->virtual_address, PAGE_SIZE) == ROUNDDOWN(simulated_ws[i], PAGE_SIZE))
//						{
//							found_in_future = 1;
//							break;
//						}
//						future_iter = LIST_NEXT(future_iter);
//					}
//
//					// If page is never referenced again, distance is effectively infinite
//					if (!found_in_future)
//					{
//						dist = 2147483647; // Max INT value
//					}
//
//					// Update victim if this page has a longer distance than current max
//					if (dist > max_dist)
//					{
//						max_dist = dist;
//						victim_idx = i;
//					}
//				}
//
//				[cite_start]// Replace the victim page in the simulated WS with the new page [cite: 459]
//				simulated_ws[victim_idx] = current_va;
//			}
//		}
//	}
//
//	// Clean up simulated array
//	kfree(simulated_ws);
//	return faults;
//}
//
//
//void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
//{
//#if USE_KHEAP
//	if (isPageReplacmentAlgorithmOPTIMAL())
//	{
//		//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
//		//Your code is here
//
//		[cite_start]// [1] Add faulted page to the end of the reference stream list [cite: 520]
//		// NOTE: Use 'referenceStreamList' as per struct Env definition in inc/environment_definitions.h
//		struct PageRefElement *new_ref = kmalloc(sizeof(struct PageRefElement));
//
//		// Safety Check for kmalloc failure to prevent null pointer dereference
//		if (new_ref == NULL)
//		{
//			panic("page_fault_handler: kmalloc failed! Kernel Heap likely full or uninitialized.");
//		}
//
//		new_ref->virtual_address = fault_va;
//		LIST_INSERT_TAIL(&(faulted_env->referenceStreamList), new_ref);
//
//		[cite_start]// [2] Check if faulted page is already in Active WS (just needs update) [cite: 517]
//		struct WorkingSetElement *wse_iter;
//		int found = 0;
//		LIST_FOREACH(wse_iter, &(faulted_env->page_WS_list))
//		{
//			if (ROUNDDOWN(wse_iter->virtual_address, PAGE_SIZE) == ROUNDDOWN(fault_va, PAGE_SIZE))
//			{
//				found = 1;
//				break;
//			}
//		}
//
//		if (!found)
//		{
//			[cite_start]// [3] If not found, and WS is FULL, flush the WS [cite: 518]
//			// This logic is unique to trace generation for Optimal:
//			// We clear the WS so the OS is forced to fault again on these pages, allowing us to record the stream.
//			if (LIST_SIZE(&(faulted_env->page_WS_list)) >= faulted_env->page_WS_max_size)
//			{
//				while (!LIST_EMPTY(&(faulted_env->page_WS_list)))
//				{
//					struct WorkingSetElement *curr = LIST_FIRST(&(faulted_env->page_WS_list));
//
//					// Unmap removes from PT and TLB (effectively resetting present bit so we fault next time)
//					unmap_frame(faulted_env->env_page_directory, curr->virtual_address);
//
//					// Remove from WS list and free element memory
//					LIST_REMOVE(&(faulted_env->page_WS_list), curr);
//					kfree(curr);
//				}
//			}
//
//			[cite_start]// [4] Placement: Add the faulted page to the Active WS [cite: 519]
//			struct FrameInfo *ptr_fi = NULL;
//			allocate_frame(&ptr_fi);
//			map_frame(faulted_env->env_page_directory, ptr_fi, fault_va, PERM_USER | PERM_WRITEABLE);
//
//			// Load page from disk if necessary
//			int ret = pf_read_env_page(faulted_env, (void*)fault_va);
//
//			// Check for Invalid Access (not in Stack, not in Heap)
//			if (ret == E_PAGE_NOT_EXIST_IN_PF)
//			{
//				if (!((fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) ||
//					  (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)))
//				{
//					unmap_frame(faulted_env->env_page_directory, fault_va);
//					env_exit();
//				}
//			}
//
//			// Create and add new WS Element
//			struct WorkingSetElement *new_wse = env_page_ws_list_create_element(faulted_env, fault_va);
//			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), new_wse);
//
//			// Update the "Last Added" pointer (Clock Hand) if needed
//			if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size)
//				faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
//			else
//				faulted_env->page_last_WS_element = NULL;
//		}
//	}
//	else
//	{
//		struct WorkingSetElement *victimWSElement = NULL;
//		uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
//		if(wsSize < (faulted_env->page_WS_max_size))
//		{
//			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
//			//Your code is here
//
//			// [1] Allocate a frame for the faulted page and map it
//			struct FrameInfo *ptr_fi = NULL;
//			allocate_frame(&ptr_fi);
//			map_frame(faulted_env->env_page_directory, ptr_fi, fault_va, PERM_USER | PERM_WRITEABLE);
//
//			// [2] Read the faulted page from page file to memory
//			int ret = pf_read_env_page(faulted_env, (void*)fault_va);
//
//			// [3] If the page does not exist on page file
//			if (ret == E_PAGE_NOT_EXIST_IN_PF)
//			{
//				// Check if it is a valid Stack page or Heap page
//				if ((fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) ||
//					(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX))
//				{
//					// Valid stack/heap access
//				}
//				else
//				{
//					unmap_frame(faulted_env->env_page_directory, fault_va);
//					env_exit();
//				}
//			}
//
//			// [4] Reflect the changes in the page working set list
//
//			struct WorkingSetElement* wse = env_page_ws_list_create_element(faulted_env, fault_va);
//			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), wse);
//
//			if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size)
//			{
//				faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
//			}
//			else
//			{
//				faulted_env->page_last_WS_element = NULL;
//			}
//		}
//		else
//		{
//			if (isPageReplacmentAlgorithmCLOCK())
//			{
//				//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
//				//Your code is here
//				[cite_start]// Use the Clock Hand pointer to iterate [cite: 538]
//				struct WorkingSetElement *curr = faulted_env->page_last_WS_element;
//				while (1)
//				{
//					uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, curr->virtual_address);
//					if (perms & PERM_USED)
//					{
//						[cite_start]// Give second chance: Reset Used Bit to 0 [cite: 540]
//						pt_set_page_permissions(faulted_env->env_page_directory, curr->virtual_address, 0, PERM_USED);
//						// Advance Clock Hand
//						curr = LIST_NEXT(curr);
//						if (curr == NULL) curr = LIST_FIRST(&(faulted_env->page_WS_list));
//					}
//					else
//					{
//						[cite_start]// Found Victim (Used Bit = 0) [cite: 541]
//						victimWSElement = curr;
//						// Advance Clock Hand to the next element for future replacements
//						faulted_env->page_last_WS_element = LIST_NEXT(curr);
//						if (faulted_env->page_last_WS_element == NULL)
//							faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
//						break;
//					}
//				}
//
//				// [Remove Victim] Write back to disk if modified, unmap, and free WSE
//				struct FrameInfo *ptr_fi = NULL;
//				pf_update_env_page(faulted_env, victimWSElement->virtual_address, ptr_fi);
//				unmap_frame(faulted_env->env_page_directory, victimWSElement->virtual_address);
//				LIST_REMOVE(&(faulted_env->page_WS_list), victimWSElement);
//				kfree(victimWSElement);
//
//				[cite_start]// [Placement] Allocate and Map New Page [cite: 567]
//				ptr_fi = NULL;
//				allocate_frame(&ptr_fi);
//				map_frame(faulted_env->env_page_directory, ptr_fi, fault_va, PERM_USER | PERM_WRITEABLE);
//				int ret = pf_read_env_page(faulted_env, (void*)fault_va);
//
//				// Validate Access
//				if (ret == E_PAGE_NOT_EXIST_IN_PF)
//				{
//					if (!((fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) ||
//						  (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)))
//					{
//						unmap_frame(faulted_env->env_page_directory, fault_va);
//						env_exit();
//					}
//				}
//
//				// Add New Page to WS List (Tail)
//				struct WorkingSetElement *wse = env_page_ws_list_create_element(faulted_env, fault_va);
//				LIST_INSERT_TAIL(&(faulted_env->page_WS_list), wse);
//
//				// NOTE: In Clock, the new page is conceptually inserted where the victim was.
//				// However, our list implementation inserts at tail. The `page_last_WS_element` (clock hand)
//				// correctly points to the element AFTER the victim (which became the head if victim was tail, etc).
//				// We just need to handle the NULL pointer case if the list logic expects it.
//				if (LIST_SIZE(&(faulted_env->page_WS_list)) == faulted_env->page_WS_max_size)
//				{
//					// If clock hand became null (e.g. victim was last element), reset to head
//					if(faulted_env->page_last_WS_element == NULL)
//						faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
//				}
//				else
//					faulted_env->page_last_WS_element = NULL;
//			}
//			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
//			{
//				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
//				//Your code is here
//				[cite_start]// Strategy: Find page with lowest timestamp (oldest access) [cite: 636]
//				uint32 min_ts = 0xFFFFFFFF;
//				struct WorkingSetElement *iter;
//
//				// Scan the Working Set to find the minimum timestamp
//				LIST_FOREACH(iter, &(faulted_env->page_WS_list))
//				{
//					if (iter->time_stamp < min_ts)
//					{
//						min_ts = iter->time_stamp;
//						victimWSElement = iter;
//					}
//				}
//
//				// [Remove Victim] Write back, Unmap, Remove from List
//				struct FrameInfo *ptr_fi = NULL;
//				pf_update_env_page(faulted_env, victimWSElement->virtual_address, ptr_fi);
//				unmap_frame(faulted_env->env_page_directory, victimWSElement->virtual_address);
//				LIST_REMOVE(&(faulted_env->page_WS_list), victimWSElement);
//				kfree(victimWSElement);
//
//				// [Placement] Add new page
//				ptr_fi = NULL;
//				allocate_frame(&ptr_fi);
//				map_frame(faulted_env->env_page_directory, ptr_fi, fault_va, PERM_USER | PERM_WRITEABLE);
//				int ret = pf_read_env_page(faulted_env, (void*)fault_va);
//
//				// Validate Access
//				if (ret == E_PAGE_NOT_EXIST_IN_PF)
//				{
//					if (!((fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) ||
//						  (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)))
//					{
//						unmap_frame(faulted_env->env_page_directory, fault_va);
//						env_exit();
//					}
//				}
//
//				// Add to WS List
//				struct WorkingSetElement *wse = env_page_ws_list_create_element(faulted_env, fault_va);
//				LIST_INSERT_TAIL(&(faulted_env->page_WS_list), wse);
//			}
//			else if (isPageReplacmentAlgorithmModifiedCLOCK())
//			{
//				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
//				//Your code is here
//				[cite_start]// Use the Clock Hand pointer to start search [cite: 691]
//				struct WorkingSetElement *curr = faulted_env->page_last_WS_element;
//				int ws_size = LIST_SIZE(&(faulted_env->page_WS_list));
//				int found = 0;
//
//				// Loop until a victim is found
//				while (!found)
//				{
//					[cite_start]// Pass 1: Look for (Used=0, Mod=0) without changing bits [cite: 699-702]
//					struct WorkingSetElement *start = curr;
//					for (int i = 0; i < ws_size; i++)
//					{
//						uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, curr->virtual_address);
//						int used = (perms & PERM_USED) ? 1 : 0;
//						int mod = (perms & PERM_MODIFIED) ? 1 : 0;
//
//						if (used == 0 && mod == 0)
//						{
//							// Perfect victim found
//							victimWSElement = curr;
//							found = 1;
//							faulted_env->page_last_WS_element = LIST_NEXT(curr);
//							if (faulted_env->page_last_WS_element == NULL)
//								faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
//							break;
//						}
//						// Advance pointer
//						curr = LIST_NEXT(curr);
//						if (curr == NULL) curr = LIST_FIRST(&(faulted_env->page_WS_list));
//					}
//
//					if (found) break;
//
//					[cite_start]// Pass 2: Look for (Used=0, Mod=1), clearing Used bits if Used=1 [cite: 703-706]
//					// We restart scanning from where we left off (start)
//					curr = start;
//					for (int i = 0; i < ws_size; i++)
//					{
//						uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, curr->virtual_address);
//						int used = (perms & PERM_USED) ? 1 : 0;
//						int mod = (perms & PERM_MODIFIED) ? 1 : 0;
//
//						if (used == 0)
//						{
//							// Victim found (0, 1) - because we assume (0,0) wasn't found in Pass 1
//							victimWSElement = curr;
//							found = 1;
//							faulted_env->page_last_WS_element = LIST_NEXT(curr);
//							if (faulted_env->page_last_WS_element == NULL)
//								faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
//							break;
//						}
//						else
//						{
//							// Set Used=0 as we pass by
//							pt_set_page_permissions(faulted_env->env_page_directory, curr->virtual_address, 0, PERM_USED);
//						}
//						// Advance pointer
//						curr = LIST_NEXT(curr);
//						if (curr == NULL) curr = LIST_FIRST(&(faulted_env->page_WS_list));
//					}
//					// If not found in Pass 2, loop repeats (Pass 1 again, now with Used bits cleared)
//				}
//
//				// [Remove Victim] Write back, Unmap, Free
//				struct FrameInfo *ptr_fi = NULL;
//				pf_update_env_page(faulted_env, victimWSElement->virtual_address, ptr_fi);
//				unmap_frame(faulted_env->env_page_directory, victimWSElement->virtual_address);
//				LIST_REMOVE(&(faulted_env->page_WS_list), victimWSElement);
//				kfree(victimWSElement);
//
//				// [Placement] Allocate new page
//				ptr_fi = NULL;
//				allocate_frame(&ptr_fi);
//				map_frame(faulted_env->env_page_directory, ptr_fi, fault_va, PERM_USER | PERM_WRITEABLE);
//				int ret = pf_read_env_page(faulted_env, (void*)fault_va);
//
//				// Validate Access
//				if (ret == E_PAGE_NOT_EXIST_IN_PF)
//				{
//					if (!((fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) ||
//						  (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)))
//					{
//						unmap_frame(faulted_env->env_page_directory, fault_va);
//						env_exit();
//					}
//				}
//
//				// Add to WS
//				struct WorkingSetElement *wse = env_page_ws_list_create_element(faulted_env, fault_va);
//				LIST_INSERT_TAIL(&(faulted_env->page_WS_list), wse);
//			}
//		}
//	}
//#endif
//}
//
//void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
//{
//	panic("this function is not required...!!");
//}
