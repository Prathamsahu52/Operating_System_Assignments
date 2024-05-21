#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>


/* 
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables 
 * */

#define _4KB 4096
/**
 * mprotect System call Implementation.
*/
void update_pte(u32 pgd, u64 start_addr, u64 end_addr, int prot){

    u64 curr_addr = start_addr;
    // printk("address is %x\n", start_addr);
    // printk("prot is %d\n", prot);

    while(curr_addr< end_addr){

        u64* first_level=(u64*)osmap(pgd);
        u64 pgd_offset= (curr_addr >> 39);
        u64* first_level_pte= first_level+pgd_offset;
        u32 pud_pfn;
        if(!(*(first_level_pte) & 0x1)){ 
            //there is no physical page allotted and hence we can just return
            return;
        }else{
            // printk("first level pte is %x\n",*(first_level_pte));
            pud_pfn = (*(first_level_pte))>>12;
            // *(first_level_pte)=(pud_pfn<<12) | 0x1 | (0x1<<3)| (0x1<<4);
        }

        u64* second_level= (u64*)osmap(pud_pfn);
        u64 pud_offset= (curr_addr >> 30) & 0x1FF;
        u64* second_level_pte= second_level + pud_offset;
        u32 pmd_pfn;

        if(!(*(second_level_pte) & 0x1)){
            //there is no physical page allotted and hence we can just return
            return;
        }else{
            pmd_pfn = (*(second_level_pte))>>12;
            // printk("second level pte is %x\n",*(second_level_pte));

            // *(second_level_pte)=(pmd_pfn<<12)| 0x1| (0x1<<3)|(0x1<<4);
        }

        u64* third_level = (u64*)osmap(pmd_pfn);
        u64 pmd_offset = (curr_addr >> 21) & 0x1FF;
        u64* third_level_pte= third_level+ pmd_offset;
        u32 pte_pfn;

        if(!(*(third_level_pte) & 0x1)){
            //there is no physical page allotted and hence we can just return
            return;
        }else{
            pte_pfn = (*(third_level_pte))>>12;
            // printk("third level pte is %x\n",*(third_level_pte));
            // *(third_level_pte)=(pte_pfn<<12)|0x1| (0x1<<3)|(0x1<<4);
        }

        u64* fourth_level= (u64*)osmap(pte_pfn);
        u64 pte_offset = (curr_addr >> 12) & 0x1FF;
        u64* physical= fourth_level + pte_offset;
        u32 pfn;
        if(!(*(physical) & 0x1)){
            //there is no physical page allotted and hence we can just return
            return;
        }else{
            pfn = (*(physical))>>12;
            if(get_pfn_refcount(pfn)>=2){
                
                //do nothing
            }else{
                *(physical)=(pfn<<12) | 0x1 | (prot<<3)| (0x1<<4);
            }
            // printk("new access flags is %x\n", prot);
            // printk("physical pte entry is %x\n", *(physical));
        }


        //deallocate the physical page

        curr_addr+=_4KB;
    }   

}
void flush_tlb(void) {
    unsigned long cr3;
    asm volatile("mov %%cr3, %0" : "=r" (cr3));
    asm volatile("mov %0, %%cr3" :: "r" (cr3) : "memory");
}
long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot) {

    // asm volatile("invlpg (%0)" ::"r" (addr) : "memory");

    if (!addr || length <= 0 || addr < MMAP_AREA_START || addr + length > MMAP_AREA_END) {
        return -1;
    }
    if (prot != PROT_READ && prot != (PROT_READ | PROT_WRITE)) {
        return -1;
    }
    if (!current || !current->vm_area) {
        return -1; // Invalid context or no VMAs
    }
    

    struct vm_area *curr = current->vm_area;
    struct vm_area *prev = NULL;
    int length_alloc = (length % _4KB == 0) ? length : ((length / _4KB) + 1) * _4KB;
    u64 end_addr = addr + length_alloc;
    int modified_flag = prot==PROT_READ?0:1; // Flag to check if any VMA was modified

       while (curr && curr->vm_start < end_addr) {
        if (curr->vm_end <= addr) {
            // Current VMA is completely before the specified range, skip it
            prev = curr;
            curr = curr->vm_next;
            continue;
        }

        // Check if the VMA overlaps with the range [addr, addr+length)
        if (curr->vm_start < addr || curr->vm_end > end_addr) {
            // Split the VMA into two or three parts depending on the overlap
            struct vm_area *new_vma = NULL;
            if (curr->vm_start < addr) {
                // Allocate a new VMA for the lower part
                new_vma = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                if (!new_vma) {
                    return -1; // Allocation failure
                }
                *new_vma = *curr; // Copy the current VMA details
                new_vma->vm_end = addr;
                new_vma->vm_next = curr;
                curr->vm_start = addr;
                if (prev) {
                    prev->vm_next = new_vma;
                } else {
                    current->vm_area = new_vma;
                }
                prev = new_vma;
                stats->num_vm_area++; // Increment VMA count
            }
            if (curr->vm_end > end_addr) {
                // Allocate a new VMA for the upper part
                new_vma = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                if (!new_vma) {
                    return -1; // Allocation failure
                }
                *new_vma = *curr; // Copy the current VMA details
                new_vma->vm_start = end_addr;
                new_vma->vm_next = curr->vm_next;
                curr->vm_end = end_addr;
                curr->vm_next = new_vma;
                stats->num_vm_area++; // Increment VMA count
            }
        }

        // Update the protection flags and page table entries for the current VMA
        update_pte(current->pgd, curr->vm_start, curr->vm_end, modified_flag);
        curr->access_flags = prot;

        // Merge with next VMA if possible
        if (curr->vm_next && curr->vm_next->vm_start == curr->vm_end && curr->vm_next->access_flags == prot) {
            struct vm_area *next_vma = curr->vm_next;
            curr->vm_end = next_vma->vm_end;
            curr->vm_next = next_vma->vm_next;
            os_free(next_vma, sizeof(struct vm_area));
            stats->num_vm_area--; // Decrement VMA count
        }

        //merge with previous VMA if possible
        if (prev && prev->vm_end == curr->vm_start && prev->access_flags == prot) {
            prev->vm_end = curr->vm_end;
            prev->vm_next = curr->vm_next;
            os_free(curr, sizeof(struct vm_area));
            stats->num_vm_area--; // Decrement VMA count
            curr = prev->vm_next;
        }


        prev = curr;
        curr = curr->vm_next;
    }

    // If no VMA was modified, it's not an error, just return 0
    //we need to flush the TLB
    update_pte(current->pgd,addr, end_addr, modified_flag);
    flush_tlb();
    return 0;
}
/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags) {
    if (length <= 0 || length > 2*1024*1024)
        return -1;
    if (prot != PROT_READ && prot != (PROT_READ | PROT_WRITE))
        return -1;
    if (flags != 0 && flags != MAP_FIXED)
        return -1;
    if (flags == MAP_FIXED && !addr)
        return -1;
    if (!current) {
        return -1;
    }if((addr) & (addr < MMAP_AREA_START || addr + length > MMAP_AREA_END)){
        return -1;
    }
    // printk("reached here\n");
    // Calculate the required length, rounded up to the nearest page size
    unsigned long length_alloc = (length % _4KB == 0) ? length : ((length / _4KB) + 1) * _4KB;
    // printk("length allocated is %d\n", length_alloc);
    struct vm_area *prev = NULL;
    struct vm_area *curr = current->vm_area;

    // If no VMAs exist, create the dummy node
    if (!curr) {
        curr = os_alloc(sizeof(struct vm_area));
        if (!curr) {
            return -1; // Allocation failure
        }
        curr->vm_start = MMAP_AREA_START;
        curr->vm_end = MMAP_AREA_START + _4KB;
        curr->access_flags = 0x0;
        curr->vm_next = NULL;
        current->vm_area = curr;
        stats->num_vm_area++; // Increment VMA count
    }

    // If addr is provided and MAP_FIXED flag is set
    if (addr && flags == MAP_FIXED) {
        while (curr && curr->vm_end <= addr) {
            prev = curr;
            curr = curr->vm_next;
        }
        if (curr && curr->vm_start < addr + length_alloc) {
            return -1; 
        }
    }else if (addr) {
        
        int found = 0;
        while (curr) {
            if (addr >= curr->vm_start && addr < curr->vm_end) {
                found = 1;
                break;
            }
            prev = curr;
            curr = curr->vm_next;
        }
        // If the hint address is already mapped, find the lowest available address
        
        if (found) {
            addr = MMAP_AREA_START;
            curr = current->vm_area;
            while (curr && curr->vm_start - addr < length_alloc) {
                addr = curr->vm_end;
                prev = curr;
                curr = curr->vm_next;
            }
        } else {
            // printk("not found\n");
            // printk("addr is %x\n", addr);
            curr = current->vm_area;
            // If the hint address is not mapped, proceed with the original logic
            while (curr && curr->vm_end <= addr) {
                // printk("curr->vm_end is %x\n", curr->vm_end);
                prev = curr;
                curr = curr->vm_next;
            }
        }

    }else if(!addr && flags == MAP_FIXED){
        return -1;
    }else {
        // If addr is not provided, find the lowest available address
        addr = MMAP_AREA_START;
        while (curr && (curr->vm_start - addr) < length_alloc) {
            addr = curr->vm_end;
            prev = curr;
            curr = curr->vm_next;
        }
    }
    
    // Create a new VMA
    struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
    if (!new_vma) {
        return -1; // Allocation failure
    }
    new_vma->vm_start = addr;
    new_vma->vm_end = addr + length_alloc;
    new_vma->access_flags = prot;
    new_vma->vm_next = curr;

    if (prev) {
        // printk("prev start is %x\n", prev->vm_start);
        prev->vm_next = new_vma;
    } else {
        current->vm_area = new_vma;
    }
    stats->num_vm_area++; // Increment VMA count
    long return_addr = new_vma->vm_start;

    // Merge with previous VMA if possible
    if (prev && prev->vm_end == new_vma->vm_start && prev->access_flags == new_vma->access_flags) {
        prev->vm_end = new_vma->vm_end;
        prev->vm_next = new_vma->vm_next;
        os_free(new_vma, sizeof(struct vm_area));
        stats->num_vm_area--; // Decrement VMA count
        new_vma = prev;
    }

    // Merge with next VMA if possible
    if (curr && curr->vm_start == new_vma->vm_end && curr->access_flags == new_vma->access_flags) {
        new_vma->vm_end = curr->vm_end;
        new_vma->vm_next = curr->vm_next;
        os_free(curr, sizeof(struct vm_area));
        stats->num_vm_area--; // Decrement VMA count
    }

    return return_addr; // Success
}

/**
 * munmap system call implemenations
*/
void free_pte(u32 pgd, u64 start_addr, u64 end_addr){

    u64 curr_addr = start_addr;

    while(curr_addr< end_addr){

        u64* first_level=(u64*)osmap(pgd);
        u64 pgd_offset= (curr_addr >> 39);
        u64* first_level_pte= first_level+pgd_offset;
        u32 pud_pfn;
        if(!(*(first_level_pte) & 0x1)){ 
            //there is no physical page allotted and hence we can just return
            return;
        }else{
            pud_pfn = (*(first_level_pte))>>12;
        }

        u64* second_level= (u64*)osmap(pud_pfn);
        u64 pud_offset= (curr_addr >> 30) & 0x1FF;
        u64* second_level_pte= second_level + pud_offset;
        u32 pmd_pfn;

        if(!(*(second_level_pte) & 0x1)){
            //there is no physical page allotted and hence we can just return
            return;
        }else{
            pmd_pfn = (*(second_level_pte))>>12;
        }

        u64* third_level = (u64*)osmap(pmd_pfn);
        u64 pmd_offset = (curr_addr >> 21) & 0x1FF;
        u64* third_level_pte= third_level+ pmd_offset;
        u32 pte_pfn;

        if(!(*(third_level_pte) & 0x1)){
            //there is no physical page allotted and hence we can just return
            return;
        }else{
            pte_pfn = (*(third_level_pte))>>12;
        }

        u64* fourth_level= (u64*)osmap(pte_pfn);
        u64 pte_offset = (curr_addr >> 12) & 0x1FF;
        u64* physical= fourth_level + pte_offset;
        //deallocate the physical page
        if(!(*(physical) & 0x1)){
            //there is no physical page allotted and hence we can just return
            return;
        }else{
            u32 physical_pfn = (*physical)>>12;
        
            if(get_pfn_refcount(physical_pfn)==1){
                // printk("pfn is %x\n", physical_pfn);
                // printk("ref count is %d\n", get_pfn_refcount(physical_pfn));
                put_pfn(physical_pfn);
                os_pfn_free(USER_REG, physical_pfn);

            }else{
                // printk("pfn is %x\n", physical_pfn);
                // printk("ref count is %d\n", get_pfn_refcount(physical_pfn));
                put_pfn(physical_pfn);
            }
            *(physical)=0x0;    
        }

        
        // //deallocate the pmd if all the entries are empty
        // flag=0;
        // for(i=0;i<512;i++){
        //     if(third_level[i] & 0x1){
        //         flag=1;
        //         break;
        //     }
        // }
        // if(flag==0){
        //     os_pfn_free(OS_PT_REG, pmd_pfn);
        //     *(second_level_pte)=0x0;
        // }

        // //deallocate the pud if all the entries are empty
        // flag=0;
        // for(i=0;i<512;i++){
        //     if(second_level[i] & 0x1){
        //         flag=1;
        //         break;
        //     }
        // }
        // if(flag==0){
        //     os_pfn_free(OS_PT_REG, pud_pfn);
        //     *(first_level_pte)=0x0;
        // }

        curr_addr+=_4KB;
    }   
}
long vm_area_unmap(struct exec_context *current, u64 addr, int length) {
    // printk("inside unmap\n");
    if (!addr || length <= 0)
        return -1;
    if (!current || !current->vm_area) {
        return -EINVAL; // Invalid context or no VMAs
    }


    struct vm_area *prev = NULL;
    struct vm_area *curr = current->vm_area;
    unsigned long length_alloc = (length % _4KB == 0) ? length : ((length / _4KB) + 1) * _4KB;

    u64 end_addr = addr + length_alloc;

    while (curr) {
        // printk("inside loop\n");
        // printk("start address of VMA is %x, end address is %x\n", curr->vm_start,curr->vm_end);
        if (curr->vm_start >= end_addr) {
            // No overlap, and since the list is sorted, no further checks are needed
            break;
        }

        if (curr->vm_end > addr) {
            // Overlap detected
            if (curr->vm_start < addr) {
                if (curr->vm_end <= end_addr) {
                    // Left split
                    free_pte(current->pgd, addr, curr->vm_end);
                    curr->vm_end = addr;
                    
                } else {
                    // Middle split: Create a new VMA for the right portion
                    struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
                    if (!new_vma) {
                        return -1; // Allocation failure
                    }
                    new_vma->vm_start = end_addr;
                    new_vma->vm_end = curr->vm_end;
                    new_vma->access_flags = curr->access_flags;
                    new_vma->vm_next = curr->vm_next;

                    free_pte(current->pgd, addr, end_addr);
                    curr->vm_end = addr;
                    curr->vm_next = new_vma;
                    curr = new_vma;
                    stats->num_vm_area++; // Decrement VMA count

                }
            } else {
                if (curr->vm_end <= end_addr) {
                    // Full overlap: Remove the VMA
                    if (prev) {
                        prev->vm_next = curr->vm_next;
                    } else {
                        current->vm_area = curr->vm_next;
                    }
                    free_pte(current->pgd, curr->vm_start, curr->vm_end);
                    os_free(curr, sizeof(struct vm_area));
                    stats->num_vm_area--; // Decrement VMA count

                    if (prev) {
                        curr = prev->vm_next;
                    } else {
                        curr = current->vm_area;
                    }
                    continue; // Skip the usual curr progression to the next VMA
                } else {
                    // Right split
                    curr->vm_start = end_addr;
                }
            }
        }

        prev = curr;
        curr = curr->vm_next;
    }
    // asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
    flush_tlb();
    return 0; // Success
}
/**
* We need to write a function that allocates a given virtual address to a given page frame
*
*/
void install_pte(struct exec_context *current, u32 pgd, u64 va, u64 pfn, int access_flags){
    //first get the page table entry for the virtual address
    //note if the page table entry does not exist for a given va, do we have to create it???

    // printk("now we are doing the page table walk to install pte\n");
    access_flags=(access_flags==PROT_READ)?0:1;
    // printk("access flags is %x\n", access_flags);
    u64* first_level=(u64*)osmap(pgd);
    u64 pgd_offset= (va >> 39);
    u64* first_level_pte= first_level+pgd_offset;
    // printk("first level pte is %x\n",*(first_level_pte));
    u32 pud_pfn;
    if(!(*(first_level_pte) & 0x1)){
        //this entry is not valid and we will need to create a new pfn
        pud_pfn = os_pfn_alloc(OS_PT_REG);
        *(first_level_pte)=(pud_pfn<<12) | 0x1 | (0x1<<3)| (0x1<<4);
    }else{
        pud_pfn = (*(first_level_pte))>>12;
    }

    
    u64* second_level= (u64*)osmap(pud_pfn);
    u64 pud_offset= (va >> 30) & 0x1FF;
    u64* second_level_pte= second_level + pud_offset;
    // printk("second level pte is %x\n",*(second_level_pte));
    u32 pmd_pfn;
    if(!(*(second_level_pte) & 0x1)){
        //this entry is not valid and we will need to create a new pfn
        pmd_pfn = os_pfn_alloc(OS_PT_REG);
        *(second_level_pte)=(pmd_pfn<<12) | 0x1 | (0x1<<3)| (0x1<<4);
    }else{
        pmd_pfn = (*(second_level_pte))>>12;
    }

    u64* third_level = (u64*)osmap(pmd_pfn);
    u64 pmd_offset = (va >> 21) & 0x1FF;
    u64* third_level_pte= third_level+ pmd_offset;
    // printk("third level pte is %x\n",*(third_level_pte));
    u32 pte_pfn;
    if(!(*(third_level_pte) & 0x1)){
        //this entry is not valid and we will need to create a new pfn
        pte_pfn = os_pfn_alloc(OS_PT_REG);
        *(third_level_pte)=(pte_pfn<<12) | 0x1 | (0x1<<3)| (0x1<<4);
    }else{
        pte_pfn = (*(third_level_pte))>>12;
    }

    u64* fourth_level= (u64*)osmap(pte_pfn);
    u64 pte_offset = (va >> 12) & 0x1FF;
    u64* physical= fourth_level + pte_offset;
    //allocate along with flags
    
    *(physical)=(pfn<<12) | 0x1 | (access_flags<<3)| (0x1<<4);
    // printk("physical pte entry is %x\n", *(physical));
    // printk("page table walk done bros\n");
}
/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */
long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    // printk("hello im inside the page fault handler\n");
    // printk("page fault handling is for addr %x\n", addr);
    struct vm_area *curr = current->vm_area;
    int found=0;
    u32 pgd=current->pgd;

    while(curr){
        if(curr->vm_start > addr){
            //it has already crossed
            // printk("not a valid  vm addr\n");
            break;
        }
        if(curr->vm_start <= addr && curr->vm_end > addr){
            //it belongs to this vm
            // printk("vm found and my error code is %x\n", error_code);
            found =1;
            int access_flags=curr->access_flags;
            if(error_code == 0x4){
                
                u32 pfn= os_pfn_alloc(USER_REG);
                install_pte(current, pgd, addr, pfn, access_flags);
                return 1;

            }else if(error_code == 0x6){
                //write to a page where no physical page is allocated
                // printk("error code 0x6\n");
                u32 pfn= os_pfn_alloc(USER_REG);
                install_pte(current, pgd, addr, pfn, access_flags);
                return 1;
            }else if(error_code == 0x7){
                // print_physical_pte(current, addr, pgd);
                // printk("error code 0x7\n");
                //write when vm area has only read access
                // printk("access flags is %x\n", access_flags);
                if(access_flags==PROT_READ){
                    // printk("goes here\n");
                    return -1;
                }else{
                    // printk("handling cow fault for addr%x\n", addr);
                    handle_cow_fault(current, addr, access_flags);
                    // printk("returned from handler cow fault\n");
                    return 1;
                }
            }
        }        
        curr=curr->vm_next;
    }

    

    if(found==0){
        //invalid memory access(no vma)
        return -1;
    }
    return -1;
}
/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the 
 * end of this function (e.g., setup_child_context etc.)
 */
void create_page_table_entries_fork(u32 child_pgd, u32 parent_pgd, u64 start_addr, u64 end_addr){
    u64 curr_addr = start_addr;
    // printk("start %x and end %x\n", start_addr, end_addr);
    while(curr_addr< end_addr){
        u64* first_level_parent=(u64*)osmap(parent_pgd);
        u64* first_level_child=(u64*)osmap(child_pgd);
        // printk("first level child is %x\n", first_level_child);
        u64 pgd_offset= (curr_addr >> 39);
        u64* first_level_pte_parent= first_level_parent+pgd_offset;
        u64* first_level_pte_child= first_level_child+pgd_offset;
        u32 pud_pfn_parent;
        u32 pud_pfn_child;

        if(!(*(first_level_pte_parent) & 0x1)){ 
            //there is no physical page  in parent allotted and hence we can just conntinue
            curr_addr+=_4KB;
            continue;
        }else{
            pud_pfn_parent = (*(first_level_pte_parent))>>12;
            *(first_level_pte_parent)=(pud_pfn_parent<<12) | 0x1 | (0x1<<3)| (0x1<<4);
            //allocate the same in child page table
            //create child pfn and then allocate
            if(!(*(first_level_pte_child) & 0x1)){
                pud_pfn_child = os_pfn_alloc(OS_PT_REG);
                *(first_level_pte_child)=(pud_pfn_child<<12) | 0x1 | (0x1<<3)| (0x1<<4);
            }else{
                pud_pfn_child = (*(first_level_pte_child))>>12;
            }
        }

        u64* second_level_parent= (u64*)osmap(pud_pfn_parent);
        u64* second_level_child= (u64*)osmap(pud_pfn_child);
        u64 pud_offset= (curr_addr >> 30) & 0x1FF;
        u64* second_level_pte_parent= second_level_parent + pud_offset;
        u64* second_level_pte_child= second_level_child + pud_offset;
        u32 pmd_pfn_parent;
        u32 pmd_pfn_child;

        if(!(*(second_level_pte_parent) & 0x1)){
            //there is no physical page allotted and hence we can just continue
            // printk("no physical page allotted on second level\n");
            curr_addr+=_4KB;
            continue;
        }else{
            pmd_pfn_parent = (*(second_level_pte_parent))>>12;
            *(second_level_pte_parent)=(pmd_pfn_parent<<12) | 0x1 | (0x1<<3)| (0x1<<4);
            //allocate the same in child page table
            //create child pfn and then allocate
            if(!(*(second_level_pte_child) & 0x1)){
                pmd_pfn_child = os_pfn_alloc(OS_PT_REG);
                *(second_level_pte_child)=(pmd_pfn_child<<12) | 0x1 | (0x1<<3)| (0x1<<4);
            }else{
                pmd_pfn_child = (*(second_level_pte_child))>>12;
            }
        }


        u64* third_level_parent = (u64*)osmap(pmd_pfn_parent);
        u64* third_level_child = (u64*)osmap(pmd_pfn_child);
        u64 pmd_offset = (curr_addr >> 21) & 0x1FF;
        u64* third_level_pte_parent= third_level_parent+ pmd_offset;
        u64* third_level_pte_child= third_level_child+ pmd_offset;
        u32 pte_pfn_parent;
        u32 pte_pfn_child;

        if(!(*(third_level_pte_parent) & 0x1)){
            //there is no physical page allotted and hence we can just continue
            // printk("no physical page allotted on third level\n");
            curr_addr+=_4KB;
            continue;
        }else{
            pte_pfn_parent = (*(third_level_pte_parent))>>12;
            *(third_level_pte_parent)=(pte_pfn_parent<<12) | 0x1 | (0x1<<3)| (0x1<<4);
            
            if(!(*(third_level_pte_child) & 0x1)){
                pte_pfn_child = os_pfn_alloc(OS_PT_REG);
                *(third_level_pte_child)=(pte_pfn_child<<12) | 0x1 | (0x1<<3)| (0x1<<4);
            }else{
                pte_pfn_child = (*(third_level_pte_child))>>12;
            }
            
        }

        u64* fourth_level_parent= (u64*)osmap(pte_pfn_parent);
        u64* fourth_level_child= (u64*)osmap(pte_pfn_child);
        u64 pte_offset = (curr_addr >> 12) & 0x1FF;
        u64* physical_parent= fourth_level_parent + pte_offset;
        u64* physical_child= fourth_level_child + pte_offset;
        u32 pfn_parent;
        u32 pfn_child;

        if(!(*(physical_parent) & 0x1)){
            curr_addr+=_4KB;
            continue;
        }else{
            pfn_parent = (*(physical_parent))>>12;
            *(physical_parent)=(pfn_parent<<12)|(0x1)|(0x0<<3)|(0x1<<4);
            get_pfn(pfn_parent);
            *(physical_child)=*(physical_parent);
        }

        //copy the contents of the physical page from parent to child
        u64* parent_virtual_page= (u64*)osmap(pfn_parent);
        u64* child_virtual_page= (u64*)osmap(pfn_child);
        // memcpy(child_virtual_page, parent_virtual_page, _4KB);
        curr_addr+=_4KB;
    }

    // curr_addr=start_addr;
    
}
long do_cfork(){
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
     /* Do not modify above lines
     * 
     * */   
     /*--------------------- Your code [start]---------------*/
    pid=new_ctx->pid;
    *new_ctx=*ctx;
    new_ctx->pid=pid;
    new_ctx->pgd= os_pfn_alloc(OS_PT_REG);
    // printk("return address is %x\n", new_ctx->regs.rbp);
    // // new_ctx->os_rsp=ctx->os_rsp;
    for(int i=0;i<MAX_MM_SEGS;i++){
        new_ctx->mms[i]=ctx->mms[i];
    }
    create_page_table_entries_fork(new_ctx->pgd, ctx->pgd, ctx->mms[MM_SEG_STACK].start, ctx->mms[MM_SEG_STACK].end);
    create_page_table_entries_fork(new_ctx->pgd, ctx->pgd, ctx->mms[MM_SEG_DATA].start, ctx->mms[MM_SEG_DATA].next_free);
    create_page_table_entries_fork(new_ctx->pgd, ctx->pgd, ctx->mms[MM_SEG_RODATA].start, ctx->mms[MM_SEG_RODATA].next_free);
    create_page_table_entries_fork(new_ctx->pgd, ctx->pgd, ctx->mms[MM_SEG_CODE].start, ctx->mms[MM_SEG_CODE].next_free);
    //copy the vm_area list from ctx to new_ctx
    // new_ctx->vm_area=ctx->vm_area;
    // struct vm_area* new_curr=new_ctx->vm_area;

    // while(curr){
    //     // printk("loop1\n");
    //     struct vm_area *new_vma = os_alloc(sizeof(struct vm_area));
    //     if (!new_vma) {
    //         return -1; // Allocation failure
    //     }
    //     *new_vma = *curr; // Copy the current VMA details
    //     new_curr=new_vma;
    //     curr=curr->vm_next;
    // }
    // curr=ctx->vm_area;
    // while(curr){
    //     // printk("loop2\n");
    //     //alloc new vm in new_ctx
    //     create_page_table_entries_fork(new_ctx->pgd, ctx->pgd, curr->vm_start, curr->vm_end);
    //     curr=curr->vm_next;
    // }
    struct vm_area* curr=ctx->vm_area;
    struct vm_area* new_list= new_ctx->vm_area;
    while(curr){
        struct vm_area* new_vma= (struct vm_area*)os_alloc(sizeof(struct vm_area));
        *new_vma=*curr;
        create_page_table_entries_fork(new_ctx->pgd, ctx->pgd, curr->vm_start, curr->vm_end);
        new_list= new_vma;
        new_list=new_list->vm_next;
        curr= curr->vm_next;
    }
    for(int i=0;i<CNAME_MAX;i++){
        new_ctx->name[i]=ctx->name[i];
    }
    for(int i=0;i<MAX_SIGNALS;i++){
        new_ctx->sighandlers[i]=ctx->sighandlers[i];
    }
    for(int i=0;i<MAX_OPEN_FILES;i++){
        new_ctx->files[i]=ctx->files[i];
    }
     /*--------------------- Your code [end] ----------------*/
    
     /*
     * The remaining part must not be changed
     */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}
/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data) 
 * it is called when there is a CoW violation in these areas. 
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

int update_virtual_physical(struct exec_context *current, u64 vaddr, int access_flags){

    // printk("inside update virtual physical for vaddr %x\n", vaddr);
    u32 pgd=current->pgd;
    access_flags=(access_flags==PROT_READ)?0:1;

    u64* first_level=(u64*)osmap(pgd);
    u64 pgd_offset= (vaddr >> 39);
    u64* first_level_pte= first_level+pgd_offset;
    u32 pud_pfn;
    if(!(*(first_level_pte) & 0x1)){ 
        //there is no physical page allotted and hence we can just return
        return -1;
    }else{
        pud_pfn = (*(first_level_pte))>>12;
    }
    // printk("reached here 1\n");
    u64* second_level= (u64*)osmap(pud_pfn);
    u64 pud_offset= (vaddr >> 30) & 0x1FF;
    u64* second_level_pte= second_level + pud_offset;
    u32 pmd_pfn;

    if(!(*(second_level_pte) & 0x1)){
        //there is no physical page allotted and hence we can just return
        return -1;
    }else{
        pmd_pfn = (*(second_level_pte))>>12;
    }
    // printk("reached here 2\n");
    u64* third_level = (u64*)osmap(pmd_pfn);
    u64 pmd_offset = (vaddr >> 21) & 0x1FF;
    u64* third_level_pte= third_level+ pmd_offset;
    u32 pte_pfn;

    if(!(*(third_level_pte) & 0x1)){
        //there is no physical page allotted and hence we can just return
        return -1;
    }else{
        pte_pfn = (*(third_level_pte))>>12;
    }
    // printk("reached here 3\n");
    u64* fourth_level= (u64*)osmap(pte_pfn);
    u64 pte_offset = (vaddr >> 12) & 0x1FF;
    u64* physical= fourth_level + pte_offset;
    if(!(*(physical) & 0x1)){
        //there is no physical page allotted and hence we can just return
        return -1;
    }else{
        u32 init_pfn=(*(physical))>>12;
        //check ref count of the pfn.
        s8 ref_count= get_pfn_refcount(init_pfn);
        if(ref_count>=2){
            u32 new_pfn= os_pfn_alloc(USER_REG);
            //copy init pfn to new pfn
            u64* init_virtual_page= (u64*)osmap(init_pfn);
            u64* new_virtual_page= (u64*)osmap(new_pfn);
            memcpy(new_virtual_page, init_virtual_page, _4KB);
            *(physical)=(new_pfn<<12)|0x1|(access_flags<<3)|(0x1<<4);
            //adjust the  ref count of old pfn
            put_pfn(init_pfn);
        }else{
            //we just need to update the access flags   
            *(physical)=(init_pfn<<12)|0x1| (access_flags<<3)|(0x1<<4);
        }
    }
    return 1;
}

long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{
    // printk("inside cow fault handler\n");
    if(update_virtual_physical(current, vaddr, access_flags)){
        flush_tlb();
        return 1;
    }else{
        return -1;
    }
    
}
