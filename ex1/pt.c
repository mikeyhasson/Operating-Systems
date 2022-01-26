#include "os.h"
#define PT_LEVELS 5
/*
Calculations:
In order to write the assignment code, we first need to calculate the structure of the multi-level page table.
Every frame is 4KB = 4096 bytes =  2^12 bytes = 2^15 bits.
Every PTE is 64 bits.
Therefore, 2^15/2^6=2^9. Meaning 512 children (9 bits per level)
45 bits are required for a VPN.
45/9=5: 4-level page table (as the lecture) (5th level contains the ppn)
*/

void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn);
uint64_t page_table_query(uint64_t pt, uint64_t vpn);

int get_entry_index (int level, uint64_t vpn) {
    vpn = vpn >> (36 - 9*level); /*getting the 9 bits of the vpn coresponding to the level to be the first 9 bits of the vpn. */
    vpn = vpn & 0x1FF; /*And with 0...0111111111=0x1FF */
    return vpn;
}
void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn){
    int pte_index;
    uint64_t* page_table;

    pt = pt  << 12;
    for(int i=0;i<PT_LEVELS;i++){
        page_table = phys_to_virt (pt);
        pte_index = get_entry_index (i,vpn);
        if (i==PT_LEVELS-1){/*last level */
            if (ppn == NO_MAPPING){
                page_table [pte_index] = 0;
            }
            else {
                page_table [pte_index] = (ppn << 12) | 1;
            }
            return;  
        }

        pt = page_table [pte_index];
        if ((pt & 1) == 0){/*if valid bit is 0, validate if needed. */
            if (ppn == NO_MAPPING){
                return;
            }
            pt = alloc_page_frame() << 12; /*new phys address */
            page_table [pte_index] = pt | 1; /*enabling valid bit */
        }
        else {
            pt -=1;/* disabling valid bit, to use address */
        }
    }
}

uint64_t page_table_query(uint64_t pt, uint64_t vpn) {
    int pte_index;
    uint64_t* page_table;

    pt = pt  << 12;
    for(int i=0;i<PT_LEVELS;i++){
        page_table = phys_to_virt (pt);
        pte_index = get_entry_index (i,vpn);
        pt = page_table [pte_index];
        if ((pt & 1) == 0){/*if valid bit is 0, somewhere along the trie, then there is no mapping. */
            return NO_MAPPING;
        }
        pt -=1;/* disabling valid bit, to use address */
    }
    return pt >> 12;/*only page num, without offset */
}