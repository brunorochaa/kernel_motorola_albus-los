/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Synthesize TLB refill handlers at runtime.
 *
 * Copyright (C) 2004, 2005, 2006, 2008	 Thiemo Seufer
 * Copyright (C) 2005, 2007, 2008, 2009	 Maciej W. Rozycki
 * Copyright (C) 2006  Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2008, 2009 Cavium Networks, Inc.
 * Copyright (C) 2011  MIPS Technologies, Inc.
 *
 * ... and the days got worse and worse and now you see
 * I've gone completly out of my mind.
 *
 * They're coming to take me a away haha
 * they're coming to take me a away hoho hihi haha
 * to the funny farm where code is beautiful all the time ...
 *
 * (Condolences to Napoleon XIV)
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/cache.h>

#include <asm/cacheflush.h>
#include <asm/cpu-type.h>
#include <asm/pgtable.h>
#include <asm/war.h>
#include <asm/uasm.h>
#include <asm/setup.h>

/*
 * TLB load/store/modify handlers.
 *
 * Only the fastpath gets synthesized at runtime, the slowpath for
 * do_page_fault remains normal asm.
 */
extern void tlb_do_page_fault_0(void);
extern void tlb_do_page_fault_1(void);

struct work_registers {
	int r1;
	int r2;
	int r3;
};

struct tlb_reg_save {
	unsigned long a;
	unsigned long b;
} ____cacheline_aligned_in_smp;

static struct tlb_reg_save handler_reg_save[NR_CPUS];

static inline int r45k_bvahwbug(void)
{
	/* XXX: We should probe for the presence of this bug, but we don't. */
	return 0;
}

static inline int r4k_250MHZhwbug(void)
{
	/* XXX: We should probe for the presence of this bug, but we don't. */
	return 0;
}

static inline int __maybe_unused bcm1250_m3_war(void)
{
	return BCM1250_M3_WAR;
}

static inline int __maybe_unused r10000_llsc_war(void)
{
	return R10000_LLSC_WAR;
}

static int use_bbit_insns(void)
{
	switch (current_cpu_type()) {
	case CPU_CAVIUM_OCTEON:
	case CPU_CAVIUM_OCTEON_PLUS:
	case CPU_CAVIUM_OCTEON2:
	case CPU_CAVIUM_OCTEON3:
		return 1;
	default:
		return 0;
	}
}

static int use_lwx_insns(void)
{
	switch (current_cpu_type()) {
	case CPU_CAVIUM_OCTEON2:
	case CPU_CAVIUM_OCTEON3:
		return 1;
	default:
		return 0;
	}
}
#if defined(CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE) && \
    CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE > 0
static bool scratchpad_available(void)
{
	return true;
}
static int scratchpad_offset(int i)
{
	/*
	 * CVMSEG starts at address -32768 and extends for
	 * CAVIUM_OCTEON_CVMSEG_SIZE 128 byte cache lines.
	 */
	i += 1; /* Kernel use starts at the top and works down. */
	return CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE * 128 - (8 * i) - 32768;
}
#else
static bool scratchpad_available(void)
{
	return false;
}
static int scratchpad_offset(int i)
{
	BUG();
	/* Really unreachable, but evidently some GCC want this. */
	return 0;
}
#endif
/*
 * Found by experiment: At least some revisions of the 4kc throw under
 * some circumstances a machine check exception, triggered by invalid
 * values in the index register.  Delaying the tlbp instruction until
 * after the next branch,  plus adding an additional nop in front of
 * tlbwi/tlbwr avoids the invalid index register values. Nobody knows
 * why; it's not an issue caused by the core RTL.
 *
 */
static int m4kc_tlbp_war(void)
{
	return (current_cpu_data.processor_id & 0xffff00) ==
	       (PRID_COMP_MIPS | PRID_IMP_4KC);
}

/* Handle labels (which must be positive integers). */
enum label_id {
	label_second_part = 1,
	label_leave,
	label_vmalloc,
	label_vmalloc_done,
	label_tlbw_hazard_0,
	label_split = label_tlbw_hazard_0 + 8,
	label_tlbl_goaround1,
	label_tlbl_goaround2,
	label_nopage_tlbl,
	label_nopage_tlbs,
	label_nopage_tlbm,
	label_smp_pgtable_change,
	label_r3000_write_probe_fail,
	label_large_segbits_fault,
#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	label_tlb_huge_update,
#endif
};

UASM_L_LA(_second_part)
UASM_L_LA(_leave)
UASM_L_LA(_vmalloc)
UASM_L_LA(_vmalloc_done)
/* _tlbw_hazard_x is handled differently.  */
UASM_L_LA(_split)
UASM_L_LA(_tlbl_goaround1)
UASM_L_LA(_tlbl_goaround2)
UASM_L_LA(_nopage_tlbl)
UASM_L_LA(_nopage_tlbs)
UASM_L_LA(_nopage_tlbm)
UASM_L_LA(_smp_pgtable_change)
UASM_L_LA(_r3000_write_probe_fail)
UASM_L_LA(_large_segbits_fault)
#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
UASM_L_LA(_tlb_huge_update)
#endif

static int hazard_instance;

static void uasm_bgezl_hazard(u32 **p, struct uasm_reloc **r, int instance)
{
	switch (instance) {
	case 0 ... 7:
		uasm_il_bgezl(p, r, 0, label_tlbw_hazard_0 + instance);
		return;
	default:
		BUG();
	}
}

static void uasm_bgezl_label(struct uasm_label **l, u32 **p, int instance)
{
	switch (instance) {
	case 0 ... 7:
		uasm_build_label(l, *p, label_tlbw_hazard_0 + instance);
		break;
	default:
		BUG();
	}
}

/*
 * pgtable bits are assigned dynamically depending on processor feature
 * and statically based on kernel configuration.  This spits out the actual
 * values the kernel is using.	Required to make sense from disassembled
 * TLB exception handlers.
 */
static void output_pgtable_bits_defines(void)
{
#define pr_define(fmt, ...)					\
	pr_debug("#define " fmt, ##__VA_ARGS__)

	pr_debug("#include <asm/asm.h>\n");
	pr_debug("#include <asm/regdef.h>\n");
	pr_debug("\n");

	pr_define("_PAGE_PRESENT_SHIFT %d\n", _PAGE_PRESENT_SHIFT);
	pr_define("_PAGE_READ_SHIFT %d\n", _PAGE_READ_SHIFT);
	pr_define("_PAGE_WRITE_SHIFT %d\n", _PAGE_WRITE_SHIFT);
	pr_define("_PAGE_ACCESSED_SHIFT %d\n", _PAGE_ACCESSED_SHIFT);
	pr_define("_PAGE_MODIFIED_SHIFT %d\n", _PAGE_MODIFIED_SHIFT);
#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	pr_define("_PAGE_HUGE_SHIFT %d\n", _PAGE_HUGE_SHIFT);
	pr_define("_PAGE_SPLITTING_SHIFT %d\n", _PAGE_SPLITTING_SHIFT);
#endif
	if (cpu_has_rixi) {
#ifdef _PAGE_NO_EXEC_SHIFT
		pr_define("_PAGE_NO_EXEC_SHIFT %d\n", _PAGE_NO_EXEC_SHIFT);
#endif
#ifdef _PAGE_NO_READ_SHIFT
		pr_define("_PAGE_NO_READ_SHIFT %d\n", _PAGE_NO_READ_SHIFT);
#endif
	}
	pr_define("_PAGE_GLOBAL_SHIFT %d\n", _PAGE_GLOBAL_SHIFT);
	pr_define("_PAGE_VALID_SHIFT %d\n", _PAGE_VALID_SHIFT);
	pr_define("_PAGE_DIRTY_SHIFT %d\n", _PAGE_DIRTY_SHIFT);
	pr_define("_PFN_SHIFT %d\n", _PFN_SHIFT);
	pr_debug("\n");
}

static inline void dump_handler(const char *symbol, const u32 *handler, int count)
{
	int i;

	pr_debug("LEAF(%s)\n", symbol);

	pr_debug("\t.set push\n");
	pr_debug("\t.set noreorder\n");

	for (i = 0; i < count; i++)
		pr_debug("\t.word\t0x%08x\t\t# %p\n", handler[i], &handler[i]);

	pr_debug("\t.set\tpop\n");

	pr_debug("\tEND(%s)\n", symbol);
}

/* The only general purpose registers allowed in TLB handlers. */
#define K0		26
#define K1		27

/* Some CP0 registers */
#define C0_INDEX	0, 0
#define C0_ENTRYLO0	2, 0
#define C0_TCBIND	2, 2
#define C0_ENTRYLO1	3, 0
#define C0_CONTEXT	4, 0
#define C0_PAGEMASK	5, 0
#define C0_BADVADDR	8, 0
#define C0_ENTRYHI	10, 0
#define C0_EPC		14, 0
#define C0_XCONTEXT	20, 0

#ifdef CONFIG_64BIT
# define GET_CONTEXT(buf, reg) UASM_i_MFC0(buf, reg, C0_XCONTEXT)
#else
# define GET_CONTEXT(buf, reg) UASM_i_MFC0(buf, reg, C0_CONTEXT)
#endif

/* The worst case length of the handler is around 18 instructions for
 * R3000-style TLBs and up to 63 instructions for R4000-style TLBs.
 * Maximum space available is 32 instructions for R3000 and 64
 * instructions for R4000.
 *
 * We deliberately chose a buffer size of 128, so we won't scribble
 * over anything important on overflow before we panic.
 */
static u32 tlb_handler[128];

/* simply assume worst case size for labels and relocs */
static struct uasm_label labels[128];
static struct uasm_reloc relocs[128];

static int check_for_high_segbits;

static unsigned int kscratch_used_mask;

static inline int __maybe_unused c0_kscratch(void)
{
	switch (current_cpu_type()) {
	case CPU_XLP:
	case CPU_XLR:
		return 22;
	default:
		return 31;
	}
}

static int allocate_kscratch(void)
{
	int r;
	unsigned int a = cpu_data[0].kscratch_mask & ~kscratch_used_mask;

	r = ffs(a);

	if (r == 0)
		return -1;

	r--; /* make it zero based */

	kscratch_used_mask |= (1 << r);

	return r;
}

static int scratch_reg;
static int pgd_reg;
enum vmalloc64_mode {not_refill, refill_scratch, refill_noscratch};

static struct work_registers build_get_work_registers(u32 **p)
{
	struct work_registers r;

	if (scratch_reg >= 0) {
		/* Save in CPU local C0_KScratch? */
		UASM_i_MTC0(p, 1, c0_kscratch(), scratch_reg);
		r.r1 = K0;
		r.r2 = K1;
		r.r3 = 1;
		return r;
	}

	if (num_possible_cpus() > 1) {
		/* Get smp_processor_id */
		UASM_i_CPUID_MFC0(p, K0, SMP_CPUID_REG);
		UASM_i_SRL_SAFE(p, K0, K0, SMP_CPUID_REGSHIFT);

		/* handler_reg_save index in K0 */
		UASM_i_SLL(p, K0, K0, ilog2(sizeof(struct tlb_reg_save)));

		UASM_i_LA(p, K1, (long)&handler_reg_save);
		UASM_i_ADDU(p, K0, K0, K1);
	} else {
		UASM_i_LA(p, K0, (long)&handler_reg_save);
	}
	/* K0 now points to save area, save $1 and $2  */
	UASM_i_SW(p, 1, offsetof(struct tlb_reg_save, a), K0);
	UASM_i_SW(p, 2, offsetof(struct tlb_reg_save, b), K0);

	r.r1 = K1;
	r.r2 = 1;
	r.r3 = 2;
	return r;
}

static void build_restore_work_registers(u32 **p)
{
	if (scratch_reg >= 0) {
		UASM_i_MFC0(p, 1, c0_kscratch(), scratch_reg);
		return;
	}
	/* K0 already points to save area, restore $1 and $2  */
	UASM_i_LW(p, 1, offsetof(struct tlb_reg_save, a), K0);
	UASM_i_LW(p, 2, offsetof(struct tlb_reg_save, b), K0);
}

#ifndef CONFIG_MIPS_PGD_C0_CONTEXT

/*
 * CONFIG_MIPS_PGD_C0_CONTEXT implies 64 bit and lack of pgd_current,
 * we cannot do r3000 under these circumstances.
 *
 * Declare pgd_current here instead of including mmu_context.h to avoid type
 * conflicts for tlbmiss_handler_setup_pgd
 */
extern unsigned long pgd_current[];

/*
 * The R3000 TLB handler is simple.
 */
static void build_r3000_tlb_refill_handler(void)
{
	long pgdc = (long)pgd_current;
	u32 *p;

	memset(tlb_handler, 0, sizeof(tlb_handler));
	p = tlb_handler;

	uasm_i_mfc0(&p, K0, C0_BADVADDR);
	uasm_i_lui(&p, K1, uasm_rel_hi(pgdc)); /* cp0 delay */
	uasm_i_lw(&p, K1, uasm_rel_lo(pgdc), K1);
	uasm_i_srl(&p, K0, K0, 22); /* load delay */
	uasm_i_sll(&p, K0, K0, 2);
	uasm_i_addu(&p, K1, K1, K0);
	uasm_i_mfc0(&p, K0, C0_CONTEXT);
	uasm_i_lw(&p, K1, 0, K1); /* cp0 delay */
	uasm_i_andi(&p, K0, K0, 0xffc); /* load delay */
	uasm_i_addu(&p, K1, K1, K0);
	uasm_i_lw(&p, K0, 0, K1);
	uasm_i_nop(&p); /* load delay */
	uasm_i_mtc0(&p, K0, C0_ENTRYLO0);
	uasm_i_mfc0(&p, K1, C0_EPC); /* cp0 delay */
	uasm_i_tlbwr(&p); /* cp0 delay */
	uasm_i_jr(&p, K1);
	uasm_i_rfe(&p); /* branch delay */

	if (p > tlb_handler + 32)
		panic("TLB refill handler space exceeded");

	pr_debug("Wrote TLB refill handler (%u instructions).\n",
		 (unsigned int)(p - tlb_handler));

	memcpy((void *)ebase, tlb_handler, 0x80);

	dump_handler("r3000_tlb_refill", (u32 *)ebase, 32);
}
#endif /* CONFIG_MIPS_PGD_C0_CONTEXT */

/*
 * The R4000 TLB handler is much more complicated. We have two
 * consecutive handler areas with 32 instructions space each.
 * Since they aren't used at the same time, we can overflow in the
 * other one.To keep things simple, we first assume linear space,
 * then we relocate it to the final handler layout as needed.
 */
static u32 final_handler[64];

/*
 * Hazards
 *
 * From the IDT errata for the QED RM5230 (Nevada), processor revision 1.0:
 * 2. A timing hazard exists for the TLBP instruction.
 *
 *	stalling_instruction
 *	TLBP
 *
 * The JTLB is being read for the TLBP throughout the stall generated by the
 * previous instruction. This is not really correct as the stalling instruction
 * can modify the address used to access the JTLB.  The failure symptom is that
 * the TLBP instruction will use an address created for the stalling instruction
 * and not the address held in C0_ENHI and thus report the wrong results.
 *
 * The software work-around is to not allow the instruction preceding the TLBP
 * to stall - make it an NOP or some other instruction guaranteed not to stall.
 *
 * Errata 2 will not be fixed.	This errata is also on the R5000.
 *
 * As if we MIPS hackers wouldn't know how to nop pipelines happy ...
 */
static void __maybe_unused build_tlb_probe_entry(u32 **p)
{
	switch (current_cpu_type()) {
	/* Found by experiment: R4600 v2.0/R4700 needs this, too.  */
	case CPU_R4600:
	case CPU_R4700:
	case CPU_R5000:
	case CPU_NEVADA:
		uasm_i_nop(p);
		uasm_i_tlbp(p);
		break;

	default:
		uasm_i_tlbp(p);
		break;
	}
}

/*
 * Write random or indexed TLB entry, and care about the hazards from
 * the preceding mtc0 and for the following eret.
 */
enum tlb_write_entry { tlb_random, tlb_indexed };

static void build_tlb_write_entry(u32 **p, struct uasm_label **l,
				  struct uasm_reloc **r,
				  enum tlb_write_entry wmode)
{
	void(*tlbw)(u32 **) = NULL;

	switch (wmode) {
	case tlb_random: tlbw = uasm_i_tlbwr; break;
	case tlb_indexed: tlbw = uasm_i_tlbwi; break;
	}

	if (cpu_has_mips_r2) {
		/*
		 * The architecture spec says an ehb is required here,
		 * but a number of cores do not have the hazard and
		 * using an ehb causes an expensive pipeline stall.
		 */
		switch (current_cpu_type()) {
		case CPU_M14KC:
		case CPU_74K:
		case CPU_1074K:
		case CPU_PROAPTIV:
		case CPU_P5600:
		case CPU_M5150:
			break;

		default:
			uasm_i_ehb(p);
			break;
		}
		tlbw(p);
		return;
	}

	switch (current_cpu_type()) {
	case CPU_R4000PC:
	case CPU_R4000SC:
	case CPU_R4000MC:
	case CPU_R4400PC:
	case CPU_R4400SC:
	case CPU_R4400MC:
		/*
		 * This branch uses up a mtc0 hazard nop slot and saves
		 * two nops after the tlbw instruction.
		 */
		uasm_bgezl_hazard(p, r, hazard_instance);
		tlbw(p);
		uasm_bgezl_label(l, p, hazard_instance);
		hazard_instance++;
		uasm_i_nop(p);
		break;

	case CPU_R4600:
	case CPU_R4700:
		uasm_i_nop(p);
		tlbw(p);
		uasm_i_nop(p);
		break;

	case CPU_R5000:
	case CPU_NEVADA:
		uasm_i_nop(p); /* QED specifies 2 nops hazard */
		uasm_i_nop(p); /* QED specifies 2 nops hazard */
		tlbw(p);
		break;

	case CPU_R4300:
	case CPU_5KC:
	case CPU_TX49XX:
	case CPU_PR4450:
	case CPU_XLR:
		uasm_i_nop(p);
		tlbw(p);
		break;

	case CPU_R10000:
	case CPU_R12000:
	case CPU_R14000:
	case CPU_4KC:
	case CPU_4KEC:
	case CPU_M14KC:
	case CPU_M14KEC:
	case CPU_SB1:
	case CPU_SB1A:
	case CPU_4KSC:
	case CPU_20KC:
	case CPU_25KF:
	case CPU_BMIPS32:
	case CPU_BMIPS3300:
	case CPU_BMIPS4350:
	case CPU_BMIPS4380:
	case CPU_BMIPS5000:
	case CPU_LOONGSON2:
	case CPU_LOONGSON3:
	case CPU_R5500:
		if (m4kc_tlbp_war())
			uasm_i_nop(p);
	case CPU_ALCHEMY:
		tlbw(p);
		break;

	case CPU_RM7000:
		uasm_i_nop(p);
		uasm_i_nop(p);
		uasm_i_nop(p);
		uasm_i_nop(p);
		tlbw(p);
		break;

	case CPU_VR4111:
	case CPU_VR4121:
	case CPU_VR4122:
	case CPU_VR4181:
	case CPU_VR4181A:
		uasm_i_nop(p);
		uasm_i_nop(p);
		tlbw(p);
		uasm_i_nop(p);
		uasm_i_nop(p);
		break;

	case CPU_VR4131:
	case CPU_VR4133:
	case CPU_R5432:
		uasm_i_nop(p);
		uasm_i_nop(p);
		tlbw(p);
		break;

	case CPU_JZRISC:
		tlbw(p);
		uasm_i_nop(p);
		break;

	default:
		panic("No TLB refill handler yet (CPU type: %d)",
		      current_cpu_type());
		break;
	}
}

static __maybe_unused void build_convert_pte_to_entrylo(u32 **p,
							unsigned int reg)
{
	if (cpu_has_rixi) {
		UASM_i_ROTR(p, reg, reg, ilog2(_PAGE_GLOBAL));
	} else {
#ifdef CONFIG_64BIT_PHYS_ADDR
		uasm_i_dsrl_safe(p, reg, reg, ilog2(_PAGE_GLOBAL));
#else
		UASM_i_SRL(p, reg, reg, ilog2(_PAGE_GLOBAL));
#endif
	}
}

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT

static void build_restore_pagemask(u32 **p, struct uasm_reloc **r,
				   unsigned int tmp, enum label_id lid,
				   int restore_scratch)
{
	if (restore_scratch) {
		/* Reset default page size */
		if (PM_DEFAULT_MASK >> 16) {
			uasm_i_lui(p, tmp, PM_DEFAULT_MASK >> 16);
			uasm_i_ori(p, tmp, tmp, PM_DEFAULT_MASK & 0xffff);
			uasm_i_mtc0(p, tmp, C0_PAGEMASK);
			uasm_il_b(p, r, lid);
		} else if (PM_DEFAULT_MASK) {
			uasm_i_ori(p, tmp, 0, PM_DEFAULT_MASK);
			uasm_i_mtc0(p, tmp, C0_PAGEMASK);
			uasm_il_b(p, r, lid);
		} else {
			uasm_i_mtc0(p, 0, C0_PAGEMASK);
			uasm_il_b(p, r, lid);
		}
		if (scratch_reg >= 0)
			UASM_i_MFC0(p, 1, c0_kscratch(), scratch_reg);
		else
			UASM_i_LW(p, 1, scratchpad_offset(0), 0);
	} else {
		/* Reset default page size */
		if (PM_DEFAULT_MASK >> 16) {
			uasm_i_lui(p, tmp, PM_DEFAULT_MASK >> 16);
			uasm_i_ori(p, tmp, tmp, PM_DEFAULT_MASK & 0xffff);
			uasm_il_b(p, r, lid);
			uasm_i_mtc0(p, tmp, C0_PAGEMASK);
		} else if (PM_DEFAULT_MASK) {
			uasm_i_ori(p, tmp, 0, PM_DEFAULT_MASK);
			uasm_il_b(p, r, lid);
			uasm_i_mtc0(p, tmp, C0_PAGEMASK);
		} else {
			uasm_il_b(p, r, lid);
			uasm_i_mtc0(p, 0, C0_PAGEMASK);
		}
	}
}

static void build_huge_tlb_write_entry(u32 **p, struct uasm_label **l,
				       struct uasm_reloc **r,
				       unsigned int tmp,
				       enum tlb_write_entry wmode,
				       int restore_scratch)
{
	/* Set huge page tlb entry size */
	uasm_i_lui(p, tmp, PM_HUGE_MASK >> 16);
	uasm_i_ori(p, tmp, tmp, PM_HUGE_MASK & 0xffff);
	uasm_i_mtc0(p, tmp, C0_PAGEMASK);

	build_tlb_write_entry(p, l, r, wmode);

	build_restore_pagemask(p, r, tmp, label_leave, restore_scratch);
}

/*
 * Check if Huge PTE is present, if so then jump to LABEL.
 */
static void
build_is_huge_pte(u32 **p, struct uasm_reloc **r, unsigned int tmp,
		  unsigned int pmd, int lid)
{
	UASM_i_LW(p, tmp, 0, pmd);
	if (use_bbit_insns()) {
		uasm_il_bbit1(p, r, tmp, ilog2(_PAGE_HUGE), lid);
	} else {
		uasm_i_andi(p, tmp, tmp, _PAGE_HUGE);
		uasm_il_bnez(p, r, tmp, lid);
	}
}

static void build_huge_update_entries(u32 **p, unsigned int pte,
				      unsigned int tmp)
{
	int small_sequence;

	/*
	 * A huge PTE describes an area the size of the
	 * configured huge page size. This is twice the
	 * of the large TLB entry size we intend to use.
	 * A TLB entry half the size of the configured
	 * huge page size is configured into entrylo0
	 * and entrylo1 to cover the contiguous huge PTE
	 * address space.
	 */
	small_sequence = (HPAGE_SIZE >> 7) < 0x10000;

	/* We can clobber tmp.	It isn't used after this.*/
	if (!small_sequence)
		uasm_i_lui(p, tmp, HPAGE_SIZE >> (7 + 16));

	build_convert_pte_to_entrylo(p, pte);
	UASM_i_MTC0(p, pte, C0_ENTRYLO0); /* load it */
	/* convert to entrylo1 */
	if (small_sequence)
		UASM_i_ADDIU(p, pte, pte, HPAGE_SIZE >> 7);
	else
		UASM_i_ADDU(p, pte, pte, tmp);

	UASM_i_MTC0(p, pte, C0_ENTRYLO1); /* load it */
}

static void build_huge_handler_tail(u32 **p, struct uasm_reloc **r,
				    struct uasm_label **l,
				    unsigned int pte,
				    unsigned int ptr)
{
#ifdef CONFIG_SMP
	UASM_i_SC(p, pte, 0, ptr);
	uasm_il_beqz(p, r, pte, label_tlb_huge_update);
	UASM_i_LW(p, pte, 0, ptr); /* Needed because SC killed our PTE */
#else
	UASM_i_SW(p, pte, 0, ptr);
#endif
	build_huge_update_entries(p, pte, ptr);
	build_huge_tlb_write_entry(p, l, r, pte, tlb_indexed, 0);
}
#endif /* CONFIG_MIPS_HUGE_TLB_SUPPORT */

#ifdef CONFIG_64BIT
/*
 * TMP and PTR are scratch.
 * TMP will be clobbered, PTR will hold the pmd entry.
 */
static void
build_get_pmde64(u32 **p, struct uasm_label **l, struct uasm_reloc **r,
		 unsigned int tmp, unsigned int ptr)
{
#ifndef CONFIG_MIPS_PGD_C0_CONTEXT
	long pgdc = (long)pgd_current;
#endif
	/*
	 * The vmalloc handling is not in the hotpath.
	 */
	uasm_i_dmfc0(p, tmp, C0_BADVADDR);

	if (check_for_high_segbits) {
		/*
		 * The kernel currently implicitely assumes that the
		 * MIPS SEGBITS parameter for the processor is
		 * (PGDIR_SHIFT+PGDIR_BITS) or less, and will never
		 * allocate virtual addresses outside the maximum
		 * range for SEGBITS = (PGDIR_SHIFT+PGDIR_BITS). But
		 * that doesn't prevent user code from accessing the
		 * higher xuseg addresses.  Here, we make sure that
		 * everything but the lower xuseg addresses goes down
		 * the module_alloc/vmalloc path.
		 */
		uasm_i_dsrl_safe(p, ptr, tmp, PGDIR_SHIFT + PGD_ORDER + PAGE_SHIFT - 3);
		uasm_il_bnez(p, r, ptr, label_vmalloc);
	} else {
		uasm_il_bltz(p, r, tmp, label_vmalloc);
	}
	/* No uasm_i_nop needed here, since the next insn doesn't touch TMP. */

	if (pgd_reg != -1) {
		/* pgd is in pgd_reg */
		UASM_i_MFC0(p, ptr, c0_kscratch(), pgd_reg);
	} else {
#if defined(CONFIG_MIPS_PGD_C0_CONTEXT)
		/*
		 * &pgd << 11 stored in CONTEXT [23..63].
		 */
		UASM_i_MFC0(p, ptr, C0_CONTEXT);

		/* Clear lower 23 bits of context. */
		uasm_i_dins(p, ptr, 0, 0, 23);

		/* 1 0	1 0 1  << 6  xkphys cached */
		uasm_i_ori(p, ptr, ptr, 0x540);
		uasm_i_drotr(p, ptr, ptr, 11);
#elif defined(CONFIG_SMP)
		UASM_i_CPUID_MFC0(p, ptr, SMP_CPUID_REG);
		uasm_i_dsrl_safe(p, ptr, ptr, SMP_CPUID_PTRSHIFT);
		UASM_i_LA_mostly(p, tmp, pgdc);
		uasm_i_daddu(p, ptr, ptr, tmp);
		uasm_i_dmfc0(p, tmp, C0_BADVADDR);
		uasm_i_ld(p, ptr, uasm_rel_lo(pgdc), ptr);
#else
		UASM_i_LA_mostly(p, ptr, pgdc);
		uasm_i_ld(p, ptr, uasm_rel_lo(pgdc), ptr);
#endif
	}

	uasm_l_vmalloc_done(l, *p);

	/* get pgd offset in bytes */
	uasm_i_dsrl_safe(p, tmp, tmp, PGDIR_SHIFT - 3);

	uasm_i_andi(p, tmp, tmp, (PTRS_PER_PGD - 1)<<3);
	uasm_i_daddu(p, ptr, ptr, tmp); /* add in pgd offset */
#ifndef __PAGETABLE_PMD_FOLDED
	uasm_i_dmfc0(p, tmp, C0_BADVADDR); /* get faulting address */
	uasm_i_ld(p, ptr, 0, ptr); /* get pmd pointer */
	uasm_i_dsrl_safe(p, tmp, tmp, PMD_SHIFT-3); /* get pmd offset in bytes */
	uasm_i_andi(p, tmp, tmp, (PTRS_PER_PMD - 1)<<3);
	uasm_i_daddu(p, ptr, ptr, tmp); /* add in pmd offset */
#endif
}

/*
 * BVADDR is the faulting address, PTR is scratch.
 * PTR will hold the pgd for vmalloc.
 */
static void
build_get_pgd_vmalloc64(u32 **p, struct uasm_label **l, struct uasm_reloc **r,
			unsigned int bvaddr, unsigned int ptr,
			enum vmalloc64_mode mode)
{
	long swpd = (long)swapper_pg_dir;
	int single_insn_swpd;
	int did_vmalloc_branch = 0;

	single_insn_swpd = uasm_in_compat_space_p(swpd) && !uasm_rel_lo(swpd);

	uasm_l_vmalloc(l, *p);

	if (mode != not_refill && check_for_high_segbits) {
		if (single_insn_swpd) {
			uasm_il_bltz(p, r, bvaddr, label_vmalloc_done);
			uasm_i_lui(p, ptr, uasm_rel_hi(swpd));
			did_vmalloc_branch = 1;
			/* fall through */
		} else {
			uasm_il_bgez(p, r, bvaddr, label_large_segbits_fault);
		}
	}
	if (!did_vmalloc_branch) {
		if (uasm_in_compat_space_p(swpd) && !uasm_rel_lo(swpd)) {
			uasm_il_b(p, r, label_vmalloc_done);
			uasm_i_lui(p, ptr, uasm_rel_hi(swpd));
		} else {
			UASM_i_LA_mostly(p, ptr, swpd);
			uasm_il_b(p, r, label_vmalloc_done);
			if (uasm_in_compat_space_p(swpd))
				uasm_i_addiu(p, ptr, ptr, uasm_rel_lo(swpd));
			else
				uasm_i_daddiu(p, ptr, ptr, uasm_rel_lo(swpd));
		}
	}
	if (mode != not_refill && check_for_high_segbits) {
		uasm_l_large_segbits_fault(l, *p);
		/*
		 * We get here if we are an xsseg address, or if we are
		 * an xuseg address above (PGDIR_SHIFT+PGDIR_BITS) boundary.
		 *
		 * Ignoring xsseg (assume disabled so would generate
		 * (address errors?), the only remaining possibility
		 * is the upper xuseg addresses.  On processors with
		 * TLB_SEGBITS <= PGDIR_SHIFT+PGDIR_BITS, these
		 * addresses would have taken an address error. We try
		 * to mimic that here by taking a load/istream page
		 * fault.
		 */
		UASM_i_LA(p, ptr, (unsigned long)tlb_do_page_fault_0);
		uasm_i_jr(p, ptr);

		if (mode == refill_scratch) {
			if (scratch_reg >= 0)
				UASM_i_MFC0(p, 1, c0_kscratch(), scratch_reg);
			else
				UASM_i_LW(p, 1, scratchpad_offset(0), 0);
		} else {
			uasm_i_nop(p);
		}
	}
}

#else /* !CONFIG_64BIT */

/*
 * TMP and PTR are scratch.
 * TMP will be clobbered, PTR will hold the pgd entry.
 */
static void __maybe_unused
build_get_pgde32(u32 **p, unsigned int tmp, unsigned int ptr)
{
	if (pgd_reg != -1) {
		/* pgd is in pgd_reg */
		uasm_i_mfc0(p, ptr, c0_kscratch(), pgd_reg);
		uasm_i_mfc0(p, tmp, C0_BADVADDR); /* get faulting address */
	} else {
		long pgdc = (long)pgd_current;

		/* 32 bit SMP has smp_processor_id() stored in CONTEXT. */
#ifdef CONFIG_SMP
		uasm_i_mfc0(p, ptr, SMP_CPUID_REG);
		UASM_i_LA_mostly(p, tmp, pgdc);
		uasm_i_srl(p, ptr, ptr, SMP_CPUID_PTRSHIFT);
		uasm_i_addu(p, ptr, tmp, ptr);
#else
		UASM_i_LA_mostly(p, ptr, pgdc);
#endif
		uasm_i_mfc0(p, tmp, C0_BADVADDR); /* get faulting address */
		uasm_i_lw(p, ptr, uasm_rel_lo(pgdc), ptr);
	}
	uasm_i_srl(p, tmp, tmp, PGDIR_SHIFT); /* get pgd only bits */
	uasm_i_sll(p, tmp, tmp, PGD_T_LOG2);
	uasm_i_addu(p, ptr, ptr, tmp); /* add in pgd offset */
}

#endif /* !CONFIG_64BIT */

static void build_adjust_context(u32 **p, unsigned int ctx)
{
	unsigned int shift = 4 - (PTE_T_LOG2 + 1) + PAGE_SHIFT - 12;
	unsigned int mask = (PTRS_PER_PTE / 2 - 1) << (PTE_T_LOG2 + 1);

	switch (current_cpu_type()) {
	case CPU_VR41XX:
	case CPU_VR4111:
	case CPU_VR4121:
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4181:
	case CPU_VR4181A:
	case CPU_VR4133:
		shift += 2;
		break;

	default:
		break;
	}

	if (shift)
		UASM_i_SRL(p, ctx, ctx, shift);
	uasm_i_andi(p, ctx, ctx, mask);
}

static void build_get_ptep(u32 **p, unsigned int tmp, unsigned int ptr)
{
	/*
	 * Bug workaround for the Nevada. It seems as if under certain
	 * circumstances the move from cp0_context might produce a
	 * bogus result when the mfc0 instruction and its consumer are
	 * in a different cacheline or a load instruction, probably any
	 * memory reference, is between them.
	 */
	switch (current_cpu_type()) {
	case CPU_NEVADA:
		UASM_i_LW(p, ptr, 0, ptr);
		GET_CONTEXT(p, tmp); /* get context reg */
		break;

	default:
		GET_CONTEXT(p, tmp); /* get context reg */
		UASM_i_LW(p, ptr, 0, ptr);
		break;
	}

	build_adjust_context(p, tmp);
	UASM_i_ADDU(p, ptr, ptr, tmp); /* add in offset */
}

static void build_update_entries(u32 **p, unsigned int tmp, unsigned int ptep)
{
	/*
	 * 64bit address support (36bit on a 32bit CPU) in a 32bit
	 * Kernel is a special case. Only a few CPUs use it.
	 */
#ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits) {
		uasm_i_ld(p, tmp, 0, ptep); /* get even pte */
		uasm_i_ld(p, ptep, sizeof(pte_t), ptep); /* get odd pte */
		if (cpu_has_rixi) {
			UASM_i_ROTR(p, tmp, tmp, ilog2(_PAGE_GLOBAL));
			UASM_i_MTC0(p, tmp, C0_ENTRYLO0); /* load it */
			UASM_i_ROTR(p, ptep, ptep, ilog2(_PAGE_GLOBAL));
		} else {
			uasm_i_dsrl_safe(p, tmp, tmp, ilog2(_PAGE_GLOBAL)); /* convert to entrylo0 */
			UASM_i_MTC0(p, tmp, C0_ENTRYLO0); /* load it */
			uasm_i_dsrl_safe(p, ptep, ptep, ilog2(_PAGE_GLOBAL)); /* convert to entrylo1 */
		}
		UASM_i_MTC0(p, ptep, C0_ENTRYLO1); /* load it */
	} else {
		int pte_off_even = sizeof(pte_t) / 2;
		int pte_off_odd = pte_off_even + sizeof(pte_t);

		/* The pte entries are pre-shifted */
		uasm_i_lw(p, tmp, pte_off_even, ptep); /* get even pte */
		UASM_i_MTC0(p, tmp, C0_ENTRYLO0); /* load it */
		uasm_i_lw(p, ptep, pte_off_odd, ptep); /* get odd pte */
		UASM_i_MTC0(p, ptep, C0_ENTRYLO1); /* load it */
	}
#else
	UASM_i_LW(p, tmp, 0, ptep); /* get even pte */
	UASM_i_LW(p, ptep, sizeof(pte_t), ptep); /* get odd pte */
	if (r45k_bvahwbug())
		build_tlb_probe_entry(p);
	if (cpu_has_rixi) {
		UASM_i_ROTR(p, tmp, tmp, ilog2(_PAGE_GLOBAL));
		if (r4k_250MHZhwbug())
			UASM_i_MTC0(p, 0, C0_ENTRYLO0);
		UASM_i_MTC0(p, tmp, C0_ENTRYLO0); /* load it */
		UASM_i_ROTR(p, ptep, ptep, ilog2(_PAGE_GLOBAL));
	} else {
		UASM_i_SRL(p, tmp, tmp, ilog2(_PAGE_GLOBAL)); /* convert to entrylo0 */
		if (r4k_250MHZhwbug())
			UASM_i_MTC0(p, 0, C0_ENTRYLO0);
		UASM_i_MTC0(p, tmp, C0_ENTRYLO0); /* load it */
		UASM_i_SRL(p, ptep, ptep, ilog2(_PAGE_GLOBAL)); /* convert to entrylo1 */
		if (r45k_bvahwbug())
			uasm_i_mfc0(p, tmp, C0_INDEX);
	}
	if (r4k_250MHZhwbug())
		UASM_i_MTC0(p, 0, C0_ENTRYLO1);
	UASM_i_MTC0(p, ptep, C0_ENTRYLO1); /* load it */
#endif
}

struct mips_huge_tlb_info {
	int huge_pte;
	int restore_scratch;
};

static struct mips_huge_tlb_info
build_fast_tlb_refill_handler (u32 **p, struct uasm_label **l,
			       struct uasm_reloc **r, unsigned int tmp,
			       unsigned int ptr, int c0_scratch_reg)
{
	struct mips_huge_tlb_info rv;
	unsigned int even, odd;
	int vmalloc_branch_delay_filled = 0;
	const int scratch = 1; /* Our extra working register */

	rv.huge_pte = scratch;
	rv.restore_scratch = 0;

	if (check_for_high_segbits) {
		UASM_i_MFC0(p, tmp, C0_BADVADDR);

		if (pgd_reg != -1)
			UASM_i_MFC0(p, ptr, c0_kscratch(), pgd_reg);
		else
			UASM_i_MFC0(p, ptr, C0_CONTEXT);

		if (c0_scratch_reg >= 0)
			UASM_i_MTC0(p, scratch, c0_kscratch(), c0_scratch_reg);
		else
			UASM_i_SW(p, scratch, scratchpad_offset(0), 0);

		uasm_i_dsrl_safe(p, scratch, tmp,
				 PGDIR_SHIFT + PGD_ORDER + PAGE_SHIFT - 3);
		uasm_il_bnez(p, r, scratch, label_vmalloc);

		if (pgd_reg == -1) {
			vmalloc_branch_delay_filled = 1;
			/* Clear lower 23 bits of context. */
			uasm_i_dins(p, ptr, 0, 0, 23);
		}
	} else {
		if (pgd_reg != -1)
			UASM_i_MFC0(p, ptr, c0_kscratch(), pgd_reg);
		else
			UASM_i_MFC0(p, ptr, C0_CONTEXT);

		UASM_i_MFC0(p, tmp, C0_BADVADDR);

		if (c0_scratch_reg >= 0)
			UASM_i_MTC0(p, scratch, c0_kscratch(), c0_scratch_reg);
		else
			UASM_i_SW(p, scratch, scratchpad_offset(0), 0);

		if (pgd_reg == -1)
			/* Clear lower 23 bits of context. */
			uasm_i_dins(p, ptr, 0, 0, 23);

		uasm_il_bltz(p, r, tmp, label_vmalloc);
	}

	if (pgd_reg == -1) {
		vmalloc_branch_delay_filled = 1;
		/* 1 0	1 0 1  << 6  xkphys cached */
		uasm_i_ori(p, ptr, ptr, 0x540);
		uasm_i_drotr(p, ptr, ptr, 11);
	}

#ifdef __PAGETABLE_PMD_FOLDED
#define LOC_PTEP scratch
#else
#define LOC_PTEP ptr
#endif

	if (!vmalloc_branch_delay_filled)
		/* get pgd offset in bytes */
		uasm_i_dsrl_safe(p, scratch, tmp, PGDIR_SHIFT - 3);

	uasm_l_vmalloc_done(l, *p);

	/*
	 *			   tmp		ptr
	 * fall-through case =	 badvaddr  *pgd_current
	 * vmalloc case	     =	 badvaddr  swapper_pg_dir
	 */

	if (vmalloc_branch_delay_filled)
		/* get pgd offset in bytes */
		uasm_i_dsrl_safe(p, scratch, tmp, PGDIR_SHIFT - 3);

#ifdef __PAGETABLE_PMD_FOLDED
	GET_CONTEXT(p, tmp); /* get context reg */
#endif
	uasm_i_andi(p, scratch, scratch, (PTRS_PER_PGD - 1) << 3);

	if (use_lwx_insns()) {
		UASM_i_LWX(p, LOC_PTEP, scratch, ptr);
	} else {
		uasm_i_daddu(p, ptr, ptr, scratch); /* add in pgd offset */
		uasm_i_ld(p, LOC_PTEP, 0, ptr); /* get pmd pointer */
	}

#ifndef __PAGETABLE_PMD_FOLDED
	/* get pmd offset in bytes */
	uasm_i_dsrl_safe(p, scratch, tmp, PMD_SHIFT - 3);
	uasm_i_andi(p, scratch, scratch, (PTRS_PER_PMD - 1) << 3);
	GET_CONTEXT(p, tmp); /* get context reg */

	if (use_lwx_insns()) {
		UASM_i_LWX(p, scratch, scratch, ptr);
	} else {
		uasm_i_daddu(p, ptr, ptr, scratch); /* add in pmd offset */
		UASM_i_LW(p, scratch, 0, ptr);
	}
#endif
	/* Adjust the context during the load latency. */
	build_adjust_context(p, tmp);

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	uasm_il_bbit1(p, r, scratch, ilog2(_PAGE_HUGE), label_tlb_huge_update);
	/*
	 * The in the LWX case we don't want to do the load in the
	 * delay slot.	It cannot issue in the same cycle and may be
	 * speculative and unneeded.
	 */
	if (use_lwx_insns())
		uasm_i_nop(p);
#endif /* CONFIG_MIPS_HUGE_TLB_SUPPORT */


	/* build_update_entries */
	if (use_lwx_insns()) {
		even = ptr;
		odd = tmp;
		UASM_i_LWX(p, even, scratch, tmp);
		UASM_i_ADDIU(p, tmp, tmp, sizeof(pte_t));
		UASM_i_LWX(p, odd, scratch, tmp);
	} else {
		UASM_i_ADDU(p, ptr, scratch, tmp); /* add in offset */
		even = tmp;
		odd = ptr;
		UASM_i_LW(p, even, 0, ptr); /* get even pte */
		UASM_i_LW(p, odd, sizeof(pte_t), ptr); /* get odd pte */
	}
	if (cpu_has_rixi) {
		uasm_i_drotr(p, even, even, ilog2(_PAGE_GLOBAL));
		UASM_i_MTC0(p, even, C0_ENTRYLO0); /* load it */
		uasm_i_drotr(p, odd, odd, ilog2(_PAGE_GLOBAL));
	} else {
		uasm_i_dsrl_safe(p, even, even, ilog2(_PAGE_GLOBAL));
		UASM_i_MTC0(p, even, C0_ENTRYLO0); /* load it */
		uasm_i_dsrl_safe(p, odd, odd, ilog2(_PAGE_GLOBAL));
	}
	UASM_i_MTC0(p, odd, C0_ENTRYLO1); /* load it */

	if (c0_scratch_reg >= 0) {
		UASM_i_MFC0(p, scratch, c0_kscratch(), c0_scratch_reg);
		build_tlb_write_entry(p, l, r, tlb_random);
		uasm_l_leave(l, *p);
		rv.restore_scratch = 1;
	} else if (PAGE_SHIFT == 14 || PAGE_SHIFT == 13)  {
		build_tlb_write_entry(p, l, r, tlb_random);
		uasm_l_leave(l, *p);
		UASM_i_LW(p, scratch, scratchpad_offset(0), 0);
	} else {
		UASM_i_LW(p, scratch, scratchpad_offset(0), 0);
		build_tlb_write_entry(p, l, r, tlb_random);
		uasm_l_leave(l, *p);
		rv.restore_scratch = 1;
	}

	uasm_i_eret(p); /* return from trap */

	return rv;
}

/*
 * For a 64-bit kernel, we are using the 64-bit XTLB refill exception
 * because EXL == 0.  If we wrap, we can also use the 32 instruction
 * slots before the XTLB refill exception handler which belong to the
 * unused TLB refill exception.
 */
#define MIPS64_REFILL_INSNS 32

static void build_r4000_tlb_refill_handler(void)
{
	u32 *p = tlb_handler;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	u32 *f;
	unsigned int final_len;
	struct mips_huge_tlb_info htlb_info __maybe_unused;
	enum vmalloc64_mode vmalloc_mode __maybe_unused;

	memset(tlb_handler, 0, sizeof(tlb_handler));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));
	memset(final_handler, 0, sizeof(final_handler));

	if ((scratch_reg >= 0 || scratchpad_available()) && use_bbit_insns()) {
		htlb_info = build_fast_tlb_refill_handler(&p, &l, &r, K0, K1,
							  scratch_reg);
		vmalloc_mode = refill_scratch;
	} else {
		htlb_info.huge_pte = K0;
		htlb_info.restore_scratch = 0;
		vmalloc_mode = refill_noscratch;
		/*
		 * create the plain linear handler
		 */
		if (bcm1250_m3_war()) {
			unsigned int segbits = 44;

			uasm_i_dmfc0(&p, K0, C0_BADVADDR);
			uasm_i_dmfc0(&p, K1, C0_ENTRYHI);
			uasm_i_xor(&p, K0, K0, K1);
			uasm_i_dsrl_safe(&p, K1, K0, 62);
			uasm_i_dsrl_safe(&p, K0, K0, 12 + 1);
			uasm_i_dsll_safe(&p, K0, K0, 64 + 12 + 1 - segbits);
			uasm_i_or(&p, K0, K0, K1);
			uasm_il_bnez(&p, &r, K0, label_leave);
			/* No need for uasm_i_nop */
		}

#ifdef CONFIG_64BIT
		build_get_pmde64(&p, &l, &r, K0, K1); /* get pmd in K1 */
#else
		build_get_pgde32(&p, K0, K1); /* get pgd in K1 */
#endif

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
		build_is_huge_pte(&p, &r, K0, K1, label_tlb_huge_update);
#endif

		build_get_ptep(&p, K0, K1);
		build_update_entries(&p, K0, K1);
		build_tlb_write_entry(&p, &l, &r, tlb_random);
		uasm_l_leave(&l, p);
		uasm_i_eret(&p); /* return from trap */
	}
#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	uasm_l_tlb_huge_update(&l, p);
	build_huge_update_entries(&p, htlb_info.huge_pte, K1);
	build_huge_tlb_write_entry(&p, &l, &r, K0, tlb_random,
				   htlb_info.restore_scratch);
#endif

#ifdef CONFIG_64BIT
	build_get_pgd_vmalloc64(&p, &l, &r, K0, K1, vmalloc_mode);
#endif

	/*
	 * Overflow check: For the 64bit handler, we need at least one
	 * free instruction slot for the wrap-around branch. In worst
	 * case, if the intended insertion point is a delay slot, we
	 * need three, with the second nop'ed and the third being
	 * unused.
	 */
	switch (boot_cpu_type()) {
	default:
		if (sizeof(long) == 4) {
	case CPU_LOONGSON2:
		/* Loongson2 ebase is different than r4k, we have more space */
			if ((p - tlb_handler) > 64)
				panic("TLB refill handler space exceeded");
			/*
			 * Now fold the handler in the TLB refill handler space.
			 */
			f = final_handler;
			/* Simplest case, just copy the handler. */
			uasm_copy_handler(relocs, labels, tlb_handler, p, f);
			final_len = p - tlb_handler;
			break;
		} else {
			if (((p - tlb_handler) > (MIPS64_REFILL_INSNS * 2) - 1)
			    || (((p - tlb_handler) > (MIPS64_REFILL_INSNS * 2) - 3)
				&& uasm_insn_has_bdelay(relocs,
							tlb_handler + MIPS64_REFILL_INSNS - 3)))
				panic("TLB refill handler space exceeded");
			/*
			 * Now fold the handler in the TLB refill handler space.
			 */
			f = final_handler + MIPS64_REFILL_INSNS;
			if ((p - tlb_handler) <= MIPS64_REFILL_INSNS) {
				/* Just copy the handler. */
				uasm_copy_handler(relocs, labels, tlb_handler, p, f);
				final_len = p - tlb_handler;
			} else {
#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
				const enum label_id ls = label_tlb_huge_update;
#else
				const enum label_id ls = label_vmalloc;
#endif
				u32 *split;
				int ov = 0;
				int i;

				for (i = 0; i < ARRAY_SIZE(labels) && labels[i].lab != ls; i++)
					;
				BUG_ON(i == ARRAY_SIZE(labels));
				split = labels[i].addr;

				/*
				 * See if we have overflown one way or the other.
				 */
				if (split > tlb_handler + MIPS64_REFILL_INSNS ||
				    split < p - MIPS64_REFILL_INSNS)
					ov = 1;

				if (ov) {
					/*
					 * Split two instructions before the end.  One
					 * for the branch and one for the instruction
					 * in the delay slot.
					 */
					split = tlb_handler + MIPS64_REFILL_INSNS - 2;

					/*
					 * If the branch would fall in a delay slot,
					 * we must back up an additional instruction
					 * so that it is no longer in a delay slot.
					 */
					if (uasm_insn_has_bdelay(relocs, split - 1))
						split--;
				}
				/* Copy first part of the handler. */
				uasm_copy_handler(relocs, labels, tlb_handler, split, f);
				f += split - tlb_handler;

				if (ov) {
					/* Insert branch. */
					uasm_l_split(&l, final_handler);
					uasm_il_b(&f, &r, label_split);
					if (uasm_insn_has_bdelay(relocs, split))
						uasm_i_nop(&f);
					else {
						uasm_copy_handler(relocs, labels,
								  split, split + 1, f);
						uasm_move_labels(labels, f, f + 1, -1);
						f++;
						split++;
					}
				}

				/* Copy the rest of the handler. */
				uasm_copy_handler(relocs, labels, split, p, final_handler);
				final_len = (f - (final_handler + MIPS64_REFILL_INSNS)) +
					    (p - split);
			}
		}
		break;
	}

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB refill handler (%u instructions).\n",
		 final_len);

	memcpy((void *)ebase, final_handler, 0x100);

	dump_handler("r4000_tlb_refill", (u32 *)ebase, 64);
}

extern u32 handle_tlbl[], handle_tlbl_end[];
extern u32 handle_tlbs[], handle_tlbs_end[];
extern u32 handle_tlbm[], handle_tlbm_end[];
extern u32 tlbmiss_handler_setup_pgd[], tlbmiss_handler_setup_pgd_end[];

static void build_setup_pgd(void)
{
	const int a0 = 4;
	const int __maybe_unused a1 = 5;
	const int __maybe_unused a2 = 6;
	u32 *p = tlbmiss_handler_setup_pgd;
	const int tlbmiss_handler_setup_pgd_size =
		tlbmiss_handler_setup_pgd_end - tlbmiss_handler_setup_pgd;
#ifndef CONFIG_MIPS_PGD_C0_CONTEXT
	long pgdc = (long)pgd_current;
#endif

	memset(tlbmiss_handler_setup_pgd, 0, tlbmiss_handler_setup_pgd_size *
					sizeof(tlbmiss_handler_setup_pgd[0]));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));
	pgd_reg = allocate_kscratch();
#ifdef CONFIG_MIPS_PGD_C0_CONTEXT
	if (pgd_reg == -1) {
		struct uasm_label *l = labels;
		struct uasm_reloc *r = relocs;

		/* PGD << 11 in c0_Context */
		/*
		 * If it is a ckseg0 address, convert to a physical
		 * address.  Shifting right by 29 and adding 4 will
		 * result in zero for these addresses.
		 *
		 */
		UASM_i_SRA(&p, a1, a0, 29);
		UASM_i_ADDIU(&p, a1, a1, 4);
		uasm_il_bnez(&p, &r, a1, label_tlbl_goaround1);
		uasm_i_nop(&p);
		uasm_i_dinsm(&p, a0, 0, 29, 64 - 29);
		uasm_l_tlbl_goaround1(&l, p);
		UASM_i_SLL(&p, a0, a0, 11);
		uasm_i_jr(&p, 31);
		UASM_i_MTC0(&p, a0, C0_CONTEXT);
	} else {
		/* PGD in c0_KScratch */
		uasm_i_jr(&p, 31);
		UASM_i_MTC0(&p, a0, c0_kscratch(), pgd_reg);
	}
#else
#ifdef CONFIG_SMP
	/* Save PGD to pgd_current[smp_processor_id()] */
	UASM_i_CPUID_MFC0(&p, a1, SMP_CPUID_REG);
	UASM_i_SRL_SAFE(&p, a1, a1, SMP_CPUID_PTRSHIFT);
	UASM_i_LA_mostly(&p, a2, pgdc);
	UASM_i_ADDU(&p, a2, a2, a1);
	UASM_i_SW(&p, a0, uasm_rel_lo(pgdc), a2);
#else
	UASM_i_LA_mostly(&p, a2, pgdc);
	UASM_i_SW(&p, a0, uasm_rel_lo(pgdc), a2);
#endif /* SMP */
	uasm_i_jr(&p, 31);

	/* if pgd_reg is allocated, save PGD also to scratch register */
	if (pgd_reg != -1)
		UASM_i_MTC0(&p, a0, c0_kscratch(), pgd_reg);
	else
		uasm_i_nop(&p);
#endif
	if (p >= tlbmiss_handler_setup_pgd_end)
		panic("tlbmiss_handler_setup_pgd space exceeded");

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote tlbmiss_handler_setup_pgd (%u instructions).\n",
		 (unsigned int)(p - tlbmiss_handler_setup_pgd));

	dump_handler("tlbmiss_handler", tlbmiss_handler_setup_pgd,
					tlbmiss_handler_setup_pgd_size);
}

static void
iPTE_LW(u32 **p, unsigned int pte, unsigned int ptr)
{
#ifdef CONFIG_SMP
# ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits)
		uasm_i_lld(p, pte, 0, ptr);
	else
# endif
		UASM_i_LL(p, pte, 0, ptr);
#else
# ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits)
		uasm_i_ld(p, pte, 0, ptr);
	else
# endif
		UASM_i_LW(p, pte, 0, ptr);
#endif
}

static void
iPTE_SW(u32 **p, struct uasm_reloc **r, unsigned int pte, unsigned int ptr,
	unsigned int mode)
{
#ifdef CONFIG_64BIT_PHYS_ADDR
	unsigned int hwmode = mode & (_PAGE_VALID | _PAGE_DIRTY);
#endif

	uasm_i_ori(p, pte, pte, mode);
#ifdef CONFIG_SMP
# ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits)
		uasm_i_scd(p, pte, 0, ptr);
	else
# endif
		UASM_i_SC(p, pte, 0, ptr);

	if (r10000_llsc_war())
		uasm_il_beqzl(p, r, pte, label_smp_pgtable_change);
	else
		uasm_il_beqz(p, r, pte, label_smp_pgtable_change);

# ifdef CONFIG_64BIT_PHYS_ADDR
	if (!cpu_has_64bits) {
		/* no uasm_i_nop needed */
		uasm_i_ll(p, pte, sizeof(pte_t) / 2, ptr);
		uasm_i_ori(p, pte, pte, hwmode);
		uasm_i_sc(p, pte, sizeof(pte_t) / 2, ptr);
		uasm_il_beqz(p, r, pte, label_smp_pgtable_change);
		/* no uasm_i_nop needed */
		uasm_i_lw(p, pte, 0, ptr);
	} else
		uasm_i_nop(p);
# else
	uasm_i_nop(p);
# endif
#else
# ifdef CONFIG_64BIT_PHYS_ADDR
	if (cpu_has_64bits)
		uasm_i_sd(p, pte, 0, ptr);
	else
# endif
		UASM_i_SW(p, pte, 0, ptr);

# ifdef CONFIG_64BIT_PHYS_ADDR
	if (!cpu_has_64bits) {
		uasm_i_lw(p, pte, sizeof(pte_t) / 2, ptr);
		uasm_i_ori(p, pte, pte, hwmode);
		uasm_i_sw(p, pte, sizeof(pte_t) / 2, ptr);
		uasm_i_lw(p, pte, 0, ptr);
	}
# endif
#endif
}

/*
 * Check if PTE is present, if not then jump to LABEL. PTR points to
 * the page table where this PTE is located, PTE will be re-loaded
 * with it's original value.
 */
static void
build_pte_present(u32 **p, struct uasm_reloc **r,
		  int pte, int ptr, int scratch, enum label_id lid)
{
	int t = scratch >= 0 ? scratch : pte;

	if (cpu_has_rixi) {
		if (use_bbit_insns()) {
			uasm_il_bbit0(p, r, pte, ilog2(_PAGE_PRESENT), lid);
			uasm_i_nop(p);
		} else {
			uasm_i_andi(p, t, pte, _PAGE_PRESENT);
			uasm_il_beqz(p, r, t, lid);
			if (pte == t)
				/* You lose the SMP race :-(*/
				iPTE_LW(p, pte, ptr);
		}
	} else {
		uasm_i_andi(p, t, pte, _PAGE_PRESENT | _PAGE_READ);
		uasm_i_xori(p, t, t, _PAGE_PRESENT | _PAGE_READ);
		uasm_il_bnez(p, r, t, lid);
		if (pte == t)
			/* You lose the SMP race :-(*/
			iPTE_LW(p, pte, ptr);
	}
}

/* Make PTE valid, store result in PTR. */
static void
build_make_valid(u32 **p, struct uasm_reloc **r, unsigned int pte,
		 unsigned int ptr)
{
	unsigned int mode = _PAGE_VALID | _PAGE_ACCESSED;

	iPTE_SW(p, r, pte, ptr, mode);
}

/*
 * Check if PTE can be written to, if not branch to LABEL. Regardless
 * restore PTE with value from PTR when done.
 */
static void
build_pte_writable(u32 **p, struct uasm_reloc **r,
		   unsigned int pte, unsigned int ptr, int scratch,
		   enum label_id lid)
{
	int t = scratch >= 0 ? scratch : pte;

	uasm_i_andi(p, t, pte, _PAGE_PRESENT | _PAGE_WRITE);
	uasm_i_xori(p, t, t, _PAGE_PRESENT | _PAGE_WRITE);
	uasm_il_bnez(p, r, t, lid);
	if (pte == t)
		/* You lose the SMP race :-(*/
		iPTE_LW(p, pte, ptr);
	else
		uasm_i_nop(p);
}

/* Make PTE writable, update software status bits as well, then store
 * at PTR.
 */
static void
build_make_write(u32 **p, struct uasm_reloc **r, unsigned int pte,
		 unsigned int ptr)
{
	unsigned int mode = (_PAGE_ACCESSED | _PAGE_MODIFIED | _PAGE_VALID
			     | _PAGE_DIRTY);

	iPTE_SW(p, r, pte, ptr, mode);
}

/*
 * Check if PTE can be modified, if not branch to LABEL. Regardless
 * restore PTE with value from PTR when done.
 */
static void
build_pte_modifiable(u32 **p, struct uasm_reloc **r,
		     unsigned int pte, unsigned int ptr, int scratch,
		     enum label_id lid)
{
	if (use_bbit_insns()) {
		uasm_il_bbit0(p, r, pte, ilog2(_PAGE_WRITE), lid);
		uasm_i_nop(p);
	} else {
		int t = scratch >= 0 ? scratch : pte;
		uasm_i_andi(p, t, pte, _PAGE_WRITE);
		uasm_il_beqz(p, r, t, lid);
		if (pte == t)
			/* You lose the SMP race :-(*/
			iPTE_LW(p, pte, ptr);
	}
}

#ifndef CONFIG_MIPS_PGD_C0_CONTEXT


/*
 * R3000 style TLB load/store/modify handlers.
 */

/*
 * This places the pte into ENTRYLO0 and writes it with tlbwi.
 * Then it returns.
 */
static void
build_r3000_pte_reload_tlbwi(u32 **p, unsigned int pte, unsigned int tmp)
{
	uasm_i_mtc0(p, pte, C0_ENTRYLO0); /* cp0 delay */
	uasm_i_mfc0(p, tmp, C0_EPC); /* cp0 delay */
	uasm_i_tlbwi(p);
	uasm_i_jr(p, tmp);
	uasm_i_rfe(p); /* branch delay */
}

/*
 * This places the pte into ENTRYLO0 and writes it with tlbwi
 * or tlbwr as appropriate.  This is because the index register
 * may have the probe fail bit set as a result of a trap on a
 * kseg2 access, i.e. without refill.  Then it returns.
 */
static void
build_r3000_tlb_reload_write(u32 **p, struct uasm_label **l,
			     struct uasm_reloc **r, unsigned int pte,
			     unsigned int tmp)
{
	uasm_i_mfc0(p, tmp, C0_INDEX);
	uasm_i_mtc0(p, pte, C0_ENTRYLO0); /* cp0 delay */
	uasm_il_bltz(p, r, tmp, label_r3000_write_probe_fail); /* cp0 delay */
	uasm_i_mfc0(p, tmp, C0_EPC); /* branch delay */
	uasm_i_tlbwi(p); /* cp0 delay */
	uasm_i_jr(p, tmp);
	uasm_i_rfe(p); /* branch delay */
	uasm_l_r3000_write_probe_fail(l, *p);
	uasm_i_tlbwr(p); /* cp0 delay */
	uasm_i_jr(p, tmp);
	uasm_i_rfe(p); /* branch delay */
}

static void
build_r3000_tlbchange_handler_head(u32 **p, unsigned int pte,
				   unsigned int ptr)
{
	long pgdc = (long)pgd_current;

	uasm_i_mfc0(p, pte, C0_BADVADDR);
	uasm_i_lui(p, ptr, uasm_rel_hi(pgdc)); /* cp0 delay */
	uasm_i_lw(p, ptr, uasm_rel_lo(pgdc), ptr);
	uasm_i_srl(p, pte, pte, 22); /* load delay */
	uasm_i_sll(p, pte, pte, 2);
	uasm_i_addu(p, ptr, ptr, pte);
	uasm_i_mfc0(p, pte, C0_CONTEXT);
	uasm_i_lw(p, ptr, 0, ptr); /* cp0 delay */
	uasm_i_andi(p, pte, pte, 0xffc); /* load delay */
	uasm_i_addu(p, ptr, ptr, pte);
	uasm_i_lw(p, pte, 0, ptr);
	uasm_i_tlbp(p); /* load delay */
}

static void build_r3000_tlb_load_handler(void)
{
	u32 *p = handle_tlbl;
	const int handle_tlbl_size = handle_tlbl_end - handle_tlbl;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;

	memset(handle_tlbl, 0, handle_tlbl_size * sizeof(handle_tlbl[0]));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	build_r3000_tlbchange_handler_head(&p, K0, K1);
	build_pte_present(&p, &r, K0, K1, -1, label_nopage_tlbl);
	uasm_i_nop(&p); /* load delay */
	build_make_valid(&p, &r, K0, K1);
	build_r3000_tlb_reload_write(&p, &l, &r, K0, K1);

	uasm_l_nopage_tlbl(&l, p);
	uasm_i_j(&p, (unsigned long)tlb_do_page_fault_0 & 0x0fffffff);
	uasm_i_nop(&p);

	if (p >= handle_tlbl_end)
		panic("TLB load handler fastpath space exceeded");

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB load handler fastpath (%u instructions).\n",
		 (unsigned int)(p - handle_tlbl));

	dump_handler("r3000_tlb_load", handle_tlbl, handle_tlbl_size);
}

static void build_r3000_tlb_store_handler(void)
{
	u32 *p = handle_tlbs;
	const int handle_tlbs_size = handle_tlbs_end - handle_tlbs;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;

	memset(handle_tlbs, 0, handle_tlbs_size * sizeof(handle_tlbs[0]));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	build_r3000_tlbchange_handler_head(&p, K0, K1);
	build_pte_writable(&p, &r, K0, K1, -1, label_nopage_tlbs);
	uasm_i_nop(&p); /* load delay */
	build_make_write(&p, &r, K0, K1);
	build_r3000_tlb_reload_write(&p, &l, &r, K0, K1);

	uasm_l_nopage_tlbs(&l, p);
	uasm_i_j(&p, (unsigned long)tlb_do_page_fault_1 & 0x0fffffff);
	uasm_i_nop(&p);

	if (p >= handle_tlbs_end)
		panic("TLB store handler fastpath space exceeded");

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB store handler fastpath (%u instructions).\n",
		 (unsigned int)(p - handle_tlbs));

	dump_handler("r3000_tlb_store", handle_tlbs, handle_tlbs_size);
}

static void build_r3000_tlb_modify_handler(void)
{
	u32 *p = handle_tlbm;
	const int handle_tlbm_size = handle_tlbm_end - handle_tlbm;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;

	memset(handle_tlbm, 0, handle_tlbm_size * sizeof(handle_tlbm[0]));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	build_r3000_tlbchange_handler_head(&p, K0, K1);
	build_pte_modifiable(&p, &r, K0, K1,  -1, label_nopage_tlbm);
	uasm_i_nop(&p); /* load delay */
	build_make_write(&p, &r, K0, K1);
	build_r3000_pte_reload_tlbwi(&p, K0, K1);

	uasm_l_nopage_tlbm(&l, p);
	uasm_i_j(&p, (unsigned long)tlb_do_page_fault_1 & 0x0fffffff);
	uasm_i_nop(&p);

	if (p >= handle_tlbm_end)
		panic("TLB modify handler fastpath space exceeded");

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB modify handler fastpath (%u instructions).\n",
		 (unsigned int)(p - handle_tlbm));

	dump_handler("r3000_tlb_modify", handle_tlbm, handle_tlbm_size);
}
#endif /* CONFIG_MIPS_PGD_C0_CONTEXT */

/*
 * R4000 style TLB load/store/modify handlers.
 */
static struct work_registers
build_r4000_tlbchange_handler_head(u32 **p, struct uasm_label **l,
				   struct uasm_reloc **r)
{
	struct work_registers wr = build_get_work_registers(p);

#ifdef CONFIG_64BIT
	build_get_pmde64(p, l, r, wr.r1, wr.r2); /* get pmd in ptr */
#else
	build_get_pgde32(p, wr.r1, wr.r2); /* get pgd in ptr */
#endif

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	/*
	 * For huge tlb entries, pmd doesn't contain an address but
	 * instead contains the tlb pte. Check the PAGE_HUGE bit and
	 * see if we need to jump to huge tlb processing.
	 */
	build_is_huge_pte(p, r, wr.r1, wr.r2, label_tlb_huge_update);
#endif

	UASM_i_MFC0(p, wr.r1, C0_BADVADDR);
	UASM_i_LW(p, wr.r2, 0, wr.r2);
	UASM_i_SRL(p, wr.r1, wr.r1, PAGE_SHIFT + PTE_ORDER - PTE_T_LOG2);
	uasm_i_andi(p, wr.r1, wr.r1, (PTRS_PER_PTE - 1) << PTE_T_LOG2);
	UASM_i_ADDU(p, wr.r2, wr.r2, wr.r1);

#ifdef CONFIG_SMP
	uasm_l_smp_pgtable_change(l, *p);
#endif
	iPTE_LW(p, wr.r1, wr.r2); /* get even pte */
	if (!m4kc_tlbp_war())
		build_tlb_probe_entry(p);
	return wr;
}

static void
build_r4000_tlbchange_handler_tail(u32 **p, struct uasm_label **l,
				   struct uasm_reloc **r, unsigned int tmp,
				   unsigned int ptr)
{
	uasm_i_ori(p, ptr, ptr, sizeof(pte_t));
	uasm_i_xori(p, ptr, ptr, sizeof(pte_t));
	build_update_entries(p, tmp, ptr);
	build_tlb_write_entry(p, l, r, tlb_indexed);
	uasm_l_leave(l, *p);
	build_restore_work_registers(p);
	uasm_i_eret(p); /* return from trap */

#ifdef CONFIG_64BIT
	build_get_pgd_vmalloc64(p, l, r, tmp, ptr, not_refill);
#endif
}

static void build_r4000_tlb_load_handler(void)
{
	u32 *p = handle_tlbl;
	const int handle_tlbl_size = handle_tlbl_end - handle_tlbl;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	struct work_registers wr;

	memset(handle_tlbl, 0, handle_tlbl_size * sizeof(handle_tlbl[0]));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	if (bcm1250_m3_war()) {
		unsigned int segbits = 44;

		uasm_i_dmfc0(&p, K0, C0_BADVADDR);
		uasm_i_dmfc0(&p, K1, C0_ENTRYHI);
		uasm_i_xor(&p, K0, K0, K1);
		uasm_i_dsrl_safe(&p, K1, K0, 62);
		uasm_i_dsrl_safe(&p, K0, K0, 12 + 1);
		uasm_i_dsll_safe(&p, K0, K0, 64 + 12 + 1 - segbits);
		uasm_i_or(&p, K0, K0, K1);
		uasm_il_bnez(&p, &r, K0, label_leave);
		/* No need for uasm_i_nop */
	}

	wr = build_r4000_tlbchange_handler_head(&p, &l, &r);
	build_pte_present(&p, &r, wr.r1, wr.r2, wr.r3, label_nopage_tlbl);
	if (m4kc_tlbp_war())
		build_tlb_probe_entry(&p);

	if (cpu_has_rixi) {
		/*
		 * If the page is not _PAGE_VALID, RI or XI could not
		 * have triggered it.  Skip the expensive test..
		 */
		if (use_bbit_insns()) {
			uasm_il_bbit0(&p, &r, wr.r1, ilog2(_PAGE_VALID),
				      label_tlbl_goaround1);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r1, _PAGE_VALID);
			uasm_il_beqz(&p, &r, wr.r3, label_tlbl_goaround1);
		}
		uasm_i_nop(&p);

		uasm_i_tlbr(&p);

		switch (current_cpu_type()) {
		default:
			if (cpu_has_mips_r2) {
				uasm_i_ehb(&p);

		case CPU_CAVIUM_OCTEON:
		case CPU_CAVIUM_OCTEON_PLUS:
		case CPU_CAVIUM_OCTEON2:
				break;
			}
		}

		/* Examine  entrylo 0 or 1 based on ptr. */
		if (use_bbit_insns()) {
			uasm_i_bbit0(&p, wr.r2, ilog2(sizeof(pte_t)), 8);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r2, sizeof(pte_t));
			uasm_i_beqz(&p, wr.r3, 8);
		}
		/* load it in the delay slot*/
		UASM_i_MFC0(&p, wr.r3, C0_ENTRYLO0);
		/* load it if ptr is odd */
		UASM_i_MFC0(&p, wr.r3, C0_ENTRYLO1);
		/*
		 * If the entryLo (now in wr.r3) is valid (bit 1), RI or
		 * XI must have triggered it.
		 */
		if (use_bbit_insns()) {
			uasm_il_bbit1(&p, &r, wr.r3, 1, label_nopage_tlbl);
			uasm_i_nop(&p);
			uasm_l_tlbl_goaround1(&l, p);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r3, 2);
			uasm_il_bnez(&p, &r, wr.r3, label_nopage_tlbl);
			uasm_i_nop(&p);
		}
		uasm_l_tlbl_goaround1(&l, p);
	}
	build_make_valid(&p, &r, wr.r1, wr.r2);
	build_r4000_tlbchange_handler_tail(&p, &l, &r, wr.r1, wr.r2);

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	/*
	 * This is the entry point when build_r4000_tlbchange_handler_head
	 * spots a huge page.
	 */
	uasm_l_tlb_huge_update(&l, p);
	iPTE_LW(&p, wr.r1, wr.r2);
	build_pte_present(&p, &r, wr.r1, wr.r2, wr.r3, label_nopage_tlbl);
	build_tlb_probe_entry(&p);

	if (cpu_has_rixi) {
		/*
		 * If the page is not _PAGE_VALID, RI or XI could not
		 * have triggered it.  Skip the expensive test..
		 */
		if (use_bbit_insns()) {
			uasm_il_bbit0(&p, &r, wr.r1, ilog2(_PAGE_VALID),
				      label_tlbl_goaround2);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r1, _PAGE_VALID);
			uasm_il_beqz(&p, &r, wr.r3, label_tlbl_goaround2);
		}
		uasm_i_nop(&p);

		uasm_i_tlbr(&p);

		switch (current_cpu_type()) {
		default:
			if (cpu_has_mips_r2) {
				uasm_i_ehb(&p);

		case CPU_CAVIUM_OCTEON:
		case CPU_CAVIUM_OCTEON_PLUS:
		case CPU_CAVIUM_OCTEON2:
				break;
			}
		}

		/* Examine  entrylo 0 or 1 based on ptr. */
		if (use_bbit_insns()) {
			uasm_i_bbit0(&p, wr.r2, ilog2(sizeof(pte_t)), 8);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r2, sizeof(pte_t));
			uasm_i_beqz(&p, wr.r3, 8);
		}
		/* load it in the delay slot*/
		UASM_i_MFC0(&p, wr.r3, C0_ENTRYLO0);
		/* load it if ptr is odd */
		UASM_i_MFC0(&p, wr.r3, C0_ENTRYLO1);
		/*
		 * If the entryLo (now in wr.r3) is valid (bit 1), RI or
		 * XI must have triggered it.
		 */
		if (use_bbit_insns()) {
			uasm_il_bbit0(&p, &r, wr.r3, 1, label_tlbl_goaround2);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r3, 2);
			uasm_il_beqz(&p, &r, wr.r3, label_tlbl_goaround2);
		}
		if (PM_DEFAULT_MASK == 0)
			uasm_i_nop(&p);
		/*
		 * We clobbered C0_PAGEMASK, restore it.  On the other branch
		 * it is restored in build_huge_tlb_write_entry.
		 */
		build_restore_pagemask(&p, &r, wr.r3, label_nopage_tlbl, 0);

		uasm_l_tlbl_goaround2(&l, p);
	}
	uasm_i_ori(&p, wr.r1, wr.r1, (_PAGE_ACCESSED | _PAGE_VALID));
	build_huge_handler_tail(&p, &r, &l, wr.r1, wr.r2);
#endif

	uasm_l_nopage_tlbl(&l, p);
	build_restore_work_registers(&p);
#ifdef CONFIG_CPU_MICROMIPS
	if ((unsigned long)tlb_do_page_fault_0 & 1) {
		uasm_i_lui(&p, K0, uasm_rel_hi((long)tlb_do_page_fault_0));
		uasm_i_addiu(&p, K0, K0, uasm_rel_lo((long)tlb_do_page_fault_0));
		uasm_i_jr(&p, K0);
	} else
#endif
	uasm_i_j(&p, (unsigned long)tlb_do_page_fault_0 & 0x0fffffff);
	uasm_i_nop(&p);

	if (p >= handle_tlbl_end)
		panic("TLB load handler fastpath space exceeded");

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB load handler fastpath (%u instructions).\n",
		 (unsigned int)(p - handle_tlbl));

	dump_handler("r4000_tlb_load", handle_tlbl, handle_tlbl_size);
}

static void build_r4000_tlb_store_handler(void)
{
	u32 *p = handle_tlbs;
	const int handle_tlbs_size = handle_tlbs_end - handle_tlbs;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	struct work_registers wr;

	memset(handle_tlbs, 0, handle_tlbs_size * sizeof(handle_tlbs[0]));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	wr = build_r4000_tlbchange_handler_head(&p, &l, &r);
	build_pte_writable(&p, &r, wr.r1, wr.r2, wr.r3, label_nopage_tlbs);
	if (m4kc_tlbp_war())
		build_tlb_probe_entry(&p);
	build_make_write(&p, &r, wr.r1, wr.r2);
	build_r4000_tlbchange_handler_tail(&p, &l, &r, wr.r1, wr.r2);

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	/*
	 * This is the entry point when
	 * build_r4000_tlbchange_handler_head spots a huge page.
	 */
	uasm_l_tlb_huge_update(&l, p);
	iPTE_LW(&p, wr.r1, wr.r2);
	build_pte_writable(&p, &r, wr.r1, wr.r2, wr.r3, label_nopage_tlbs);
	build_tlb_probe_entry(&p);
	uasm_i_ori(&p, wr.r1, wr.r1,
		   _PAGE_ACCESSED | _PAGE_MODIFIED | _PAGE_VALID | _PAGE_DIRTY);
	build_huge_handler_tail(&p, &r, &l, wr.r1, wr.r2);
#endif

	uasm_l_nopage_tlbs(&l, p);
	build_restore_work_registers(&p);
#ifdef CONFIG_CPU_MICROMIPS
	if ((unsigned long)tlb_do_page_fault_1 & 1) {
		uasm_i_lui(&p, K0, uasm_rel_hi((long)tlb_do_page_fault_1));
		uasm_i_addiu(&p, K0, K0, uasm_rel_lo((long)tlb_do_page_fault_1));
		uasm_i_jr(&p, K0);
	} else
#endif
	uasm_i_j(&p, (unsigned long)tlb_do_page_fault_1 & 0x0fffffff);
	uasm_i_nop(&p);

	if (p >= handle_tlbs_end)
		panic("TLB store handler fastpath space exceeded");

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB store handler fastpath (%u instructions).\n",
		 (unsigned int)(p - handle_tlbs));

	dump_handler("r4000_tlb_store", handle_tlbs, handle_tlbs_size);
}

static void build_r4000_tlb_modify_handler(void)
{
	u32 *p = handle_tlbm;
	const int handle_tlbm_size = handle_tlbm_end - handle_tlbm;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	struct work_registers wr;

	memset(handle_tlbm, 0, handle_tlbm_size * sizeof(handle_tlbm[0]));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	wr = build_r4000_tlbchange_handler_head(&p, &l, &r);
	build_pte_modifiable(&p, &r, wr.r1, wr.r2, wr.r3, label_nopage_tlbm);
	if (m4kc_tlbp_war())
		build_tlb_probe_entry(&p);
	/* Present and writable bits set, set accessed and dirty bits. */
	build_make_write(&p, &r, wr.r1, wr.r2);
	build_r4000_tlbchange_handler_tail(&p, &l, &r, wr.r1, wr.r2);

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	/*
	 * This is the entry point when
	 * build_r4000_tlbchange_handler_head spots a huge page.
	 */
	uasm_l_tlb_huge_update(&l, p);
	iPTE_LW(&p, wr.r1, wr.r2);
	build_pte_modifiable(&p, &r, wr.r1, wr.r2,  wr.r3, label_nopage_tlbm);
	build_tlb_probe_entry(&p);
	uasm_i_ori(&p, wr.r1, wr.r1,
		   _PAGE_ACCESSED | _PAGE_MODIFIED | _PAGE_VALID | _PAGE_DIRTY);
	build_huge_handler_tail(&p, &r, &l, wr.r1, wr.r2);
#endif

	uasm_l_nopage_tlbm(&l, p);
	build_restore_work_registers(&p);
#ifdef CONFIG_CPU_MICROMIPS
	if ((unsigned long)tlb_do_page_fault_1 & 1) {
		uasm_i_lui(&p, K0, uasm_rel_hi((long)tlb_do_page_fault_1));
		uasm_i_addiu(&p, K0, K0, uasm_rel_lo((long)tlb_do_page_fault_1));
		uasm_i_jr(&p, K0);
	} else
#endif
	uasm_i_j(&p, (unsigned long)tlb_do_page_fault_1 & 0x0fffffff);
	uasm_i_nop(&p);

	if (p >= handle_tlbm_end)
		panic("TLB modify handler fastpath space exceeded");

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB modify handler fastpath (%u instructions).\n",
		 (unsigned int)(p - handle_tlbm));

	dump_handler("r4000_tlb_modify", handle_tlbm, handle_tlbm_size);
}

static void flush_tlb_handlers(void)
{
	local_flush_icache_range((unsigned long)handle_tlbl,
			   (unsigned long)handle_tlbl_end);
	local_flush_icache_range((unsigned long)handle_tlbs,
			   (unsigned long)handle_tlbs_end);
	local_flush_icache_range((unsigned long)handle_tlbm,
			   (unsigned long)handle_tlbm_end);
	local_flush_icache_range((unsigned long)tlbmiss_handler_setup_pgd,
			   (unsigned long)tlbmiss_handler_setup_pgd_end);
}

void build_tlb_refill_handler(void)
{
	/*
	 * The refill handler is generated per-CPU, multi-node systems
	 * may have local storage for it. The other handlers are only
	 * needed once.
	 */
	static int run_once = 0;

	output_pgtable_bits_defines();

#ifdef CONFIG_64BIT
	check_for_high_segbits = current_cpu_data.vmbits > (PGDIR_SHIFT + PGD_ORDER + PAGE_SHIFT - 3);
#endif

	switch (current_cpu_type()) {
	case CPU_R2000:
	case CPU_R3000:
	case CPU_R3000A:
	case CPU_R3081E:
	case CPU_TX3912:
	case CPU_TX3922:
	case CPU_TX3927:
#ifndef CONFIG_MIPS_PGD_C0_CONTEXT
		if (cpu_has_local_ebase)
			build_r3000_tlb_refill_handler();
		if (!run_once) {
			if (!cpu_has_local_ebase)
				build_r3000_tlb_refill_handler();
			build_setup_pgd();
			build_r3000_tlb_load_handler();
			build_r3000_tlb_store_handler();
			build_r3000_tlb_modify_handler();
			flush_tlb_handlers();
			run_once++;
		}
#else
		panic("No R3000 TLB refill handler");
#endif
		break;

	case CPU_R6000:
	case CPU_R6000A:
		panic("No R6000 TLB refill handler yet");
		break;

	case CPU_R8000:
		panic("No R8000 TLB refill handler yet");
		break;

	default:
		if (!run_once) {
			scratch_reg = allocate_kscratch();
			build_setup_pgd();
			build_r4000_tlb_load_handler();
			build_r4000_tlb_store_handler();
			build_r4000_tlb_modify_handler();
			if (!cpu_has_local_ebase)
				build_r4000_tlb_refill_handler();
			flush_tlb_handlers();
			run_once++;
		}
		if (cpu_has_local_ebase)
			build_r4000_tlb_refill_handler();
	}
}
