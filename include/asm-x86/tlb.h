#ifndef ASM_X86__TLB_H
#define ASM_X86__TLB_H

#define tlb_start_vma(tlb, vma) do { } while (0)
#define tlb_end_vma(tlb, vma) do { } while (0)
#define __tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)
#define tlb_flush(tlb) flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>

#endif /* ASM_X86__TLB_H */
