/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "kvm.h"
#include "x86_emulate.h"
#include "irq.h"
#include "vmx.h"
#include "segment_descriptor.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/profile.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/desc.h>

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

struct vmcs {
	u32 revision_id;
	u32 abort;
	char data[0];
};

struct vcpu_vmx {
	struct kvm_vcpu       vcpu;
	int                   launched;
	struct kvm_msr_entry *guest_msrs;
	struct kvm_msr_entry *host_msrs;
	int                   nmsrs;
	int                   save_nmsrs;
	int                   msr_offset_efer;
#ifdef CONFIG_X86_64
	int                   msr_offset_kernel_gs_base;
#endif
	struct vmcs          *vmcs;
	struct {
		int           loaded;
		u16           fs_sel, gs_sel, ldt_sel;
		int           gs_ldt_reload_needed;
		int           fs_reload_needed;
	}host_state;

};

static inline struct vcpu_vmx *to_vmx(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct vcpu_vmx, vcpu);
}

static int init_rmode_tss(struct kvm *kvm);

static DEFINE_PER_CPU(struct vmcs *, vmxarea);
static DEFINE_PER_CPU(struct vmcs *, current_vmcs);

static struct page *vmx_io_bitmap_a;
static struct page *vmx_io_bitmap_b;

#define EFER_SAVE_RESTORE_BITS ((u64)EFER_SCE)

static struct vmcs_config {
	int size;
	int order;
	u32 revision_id;
	u32 pin_based_exec_ctrl;
	u32 cpu_based_exec_ctrl;
	u32 vmexit_ctrl;
	u32 vmentry_ctrl;
} vmcs_config;

#define VMX_SEGMENT_FIELD(seg)					\
	[VCPU_SREG_##seg] = {                                   \
		.selector = GUEST_##seg##_SELECTOR,		\
		.base = GUEST_##seg##_BASE,		   	\
		.limit = GUEST_##seg##_LIMIT,		   	\
		.ar_bytes = GUEST_##seg##_AR_BYTES,	   	\
	}

static struct kvm_vmx_segment_field {
	unsigned selector;
	unsigned base;
	unsigned limit;
	unsigned ar_bytes;
} kvm_vmx_segment_fields[] = {
	VMX_SEGMENT_FIELD(CS),
	VMX_SEGMENT_FIELD(DS),
	VMX_SEGMENT_FIELD(ES),
	VMX_SEGMENT_FIELD(FS),
	VMX_SEGMENT_FIELD(GS),
	VMX_SEGMENT_FIELD(SS),
	VMX_SEGMENT_FIELD(TR),
	VMX_SEGMENT_FIELD(LDTR),
};

/*
 * Keep MSR_K6_STAR at the end, as setup_msrs() will try to optimize it
 * away by decrementing the array size.
 */
static const u32 vmx_msr_index[] = {
#ifdef CONFIG_X86_64
	MSR_SYSCALL_MASK, MSR_LSTAR, MSR_CSTAR, MSR_KERNEL_GS_BASE,
#endif
	MSR_EFER, MSR_K6_STAR,
};
#define NR_VMX_MSR ARRAY_SIZE(vmx_msr_index)

static void load_msrs(struct kvm_msr_entry *e, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		wrmsrl(e[i].index, e[i].data);
}

static void save_msrs(struct kvm_msr_entry *e, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		rdmsrl(e[i].index, e[i].data);
}

static inline u64 msr_efer_save_restore_bits(struct kvm_msr_entry msr)
{
	return (u64)msr.data & EFER_SAVE_RESTORE_BITS;
}

static inline int msr_efer_need_save_restore(struct vcpu_vmx *vmx)
{
	int efer_offset = vmx->msr_offset_efer;
	return msr_efer_save_restore_bits(vmx->host_msrs[efer_offset]) !=
		msr_efer_save_restore_bits(vmx->guest_msrs[efer_offset]);
}

static inline int is_page_fault(u32 intr_info)
{
	return (intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VECTOR_MASK |
			     INTR_INFO_VALID_MASK)) ==
		(INTR_TYPE_EXCEPTION | PF_VECTOR | INTR_INFO_VALID_MASK);
}

static inline int is_no_device(u32 intr_info)
{
	return (intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VECTOR_MASK |
			     INTR_INFO_VALID_MASK)) ==
		(INTR_TYPE_EXCEPTION | NM_VECTOR | INTR_INFO_VALID_MASK);
}

static inline int is_external_interrupt(u32 intr_info)
{
	return (intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VALID_MASK))
		== (INTR_TYPE_EXT_INTR | INTR_INFO_VALID_MASK);
}

static inline int cpu_has_vmx_tpr_shadow(void)
{
	return (vmcs_config.cpu_based_exec_ctrl & CPU_BASED_TPR_SHADOW);
}

static inline int vm_need_tpr_shadow(struct kvm *kvm)
{
	return ((cpu_has_vmx_tpr_shadow()) && (irqchip_in_kernel(kvm)));
}

static int __find_msr_index(struct vcpu_vmx *vmx, u32 msr)
{
	int i;

	for (i = 0; i < vmx->nmsrs; ++i)
		if (vmx->guest_msrs[i].index == msr)
			return i;
	return -1;
}

static struct kvm_msr_entry *find_msr_entry(struct vcpu_vmx *vmx, u32 msr)
{
	int i;

	i = __find_msr_index(vmx, msr);
	if (i >= 0)
		return &vmx->guest_msrs[i];
	return NULL;
}

static void vmcs_clear(struct vmcs *vmcs)
{
	u64 phys_addr = __pa(vmcs);
	u8 error;

	asm volatile (ASM_VMX_VMCLEAR_RAX "; setna %0"
		      : "=g"(error) : "a"(&phys_addr), "m"(phys_addr)
		      : "cc", "memory");
	if (error)
		printk(KERN_ERR "kvm: vmclear fail: %p/%llx\n",
		       vmcs, phys_addr);
}

static void __vcpu_clear(void *arg)
{
	struct vcpu_vmx *vmx = arg;
	int cpu = raw_smp_processor_id();

	if (vmx->vcpu.cpu == cpu)
		vmcs_clear(vmx->vmcs);
	if (per_cpu(current_vmcs, cpu) == vmx->vmcs)
		per_cpu(current_vmcs, cpu) = NULL;
	rdtscll(vmx->vcpu.host_tsc);
}

static void vcpu_clear(struct vcpu_vmx *vmx)
{
	if (vmx->vcpu.cpu != raw_smp_processor_id() && vmx->vcpu.cpu != -1)
		smp_call_function_single(vmx->vcpu.cpu, __vcpu_clear,
					 vmx, 0, 1);
	else
		__vcpu_clear(vmx);
	vmx->launched = 0;
}

static unsigned long vmcs_readl(unsigned long field)
{
	unsigned long value;

	asm volatile (ASM_VMX_VMREAD_RDX_RAX
		      : "=a"(value) : "d"(field) : "cc");
	return value;
}

static u16 vmcs_read16(unsigned long field)
{
	return vmcs_readl(field);
}

static u32 vmcs_read32(unsigned long field)
{
	return vmcs_readl(field);
}

static u64 vmcs_read64(unsigned long field)
{
#ifdef CONFIG_X86_64
	return vmcs_readl(field);
#else
	return vmcs_readl(field) | ((u64)vmcs_readl(field+1) << 32);
#endif
}

static noinline void vmwrite_error(unsigned long field, unsigned long value)
{
	printk(KERN_ERR "vmwrite error: reg %lx value %lx (err %d)\n",
	       field, value, vmcs_read32(VM_INSTRUCTION_ERROR));
	dump_stack();
}

static void vmcs_writel(unsigned long field, unsigned long value)
{
	u8 error;

	asm volatile (ASM_VMX_VMWRITE_RAX_RDX "; setna %0"
		       : "=q"(error) : "a"(value), "d"(field) : "cc" );
	if (unlikely(error))
		vmwrite_error(field, value);
}

static void vmcs_write16(unsigned long field, u16 value)
{
	vmcs_writel(field, value);
}

static void vmcs_write32(unsigned long field, u32 value)
{
	vmcs_writel(field, value);
}

static void vmcs_write64(unsigned long field, u64 value)
{
#ifdef CONFIG_X86_64
	vmcs_writel(field, value);
#else
	vmcs_writel(field, value);
	asm volatile ("");
	vmcs_writel(field+1, value >> 32);
#endif
}

static void vmcs_clear_bits(unsigned long field, u32 mask)
{
	vmcs_writel(field, vmcs_readl(field) & ~mask);
}

static void vmcs_set_bits(unsigned long field, u32 mask)
{
	vmcs_writel(field, vmcs_readl(field) | mask);
}

static void update_exception_bitmap(struct kvm_vcpu *vcpu)
{
	u32 eb;

	eb = 1u << PF_VECTOR;
	if (!vcpu->fpu_active)
		eb |= 1u << NM_VECTOR;
	if (vcpu->guest_debug.enabled)
		eb |= 1u << 1;
	if (vcpu->rmode.active)
		eb = ~0;
	vmcs_write32(EXCEPTION_BITMAP, eb);
}

static void reload_tss(void)
{
#ifndef CONFIG_X86_64

	/*
	 * VT restores TR but not its size.  Useless.
	 */
	struct descriptor_table gdt;
	struct segment_descriptor *descs;

	get_gdt(&gdt);
	descs = (void *)gdt.base;
	descs[GDT_ENTRY_TSS].type = 9; /* available TSS */
	load_TR_desc();
#endif
}

static void load_transition_efer(struct vcpu_vmx *vmx)
{
	u64 trans_efer;
	int efer_offset = vmx->msr_offset_efer;

	trans_efer = vmx->host_msrs[efer_offset].data;
	trans_efer &= ~EFER_SAVE_RESTORE_BITS;
	trans_efer |= msr_efer_save_restore_bits(vmx->guest_msrs[efer_offset]);
	wrmsrl(MSR_EFER, trans_efer);
	vmx->vcpu.stat.efer_reload++;
}

static void vmx_save_host_state(struct vcpu_vmx *vmx)
{
	if (vmx->host_state.loaded)
		return;

	vmx->host_state.loaded = 1;
	/*
	 * Set host fs and gs selectors.  Unfortunately, 22.2.3 does not
	 * allow segment selectors with cpl > 0 or ti == 1.
	 */
	vmx->host_state.ldt_sel = read_ldt();
	vmx->host_state.gs_ldt_reload_needed = vmx->host_state.ldt_sel;
	vmx->host_state.fs_sel = read_fs();
	if (!(vmx->host_state.fs_sel & 7)) {
		vmcs_write16(HOST_FS_SELECTOR, vmx->host_state.fs_sel);
		vmx->host_state.fs_reload_needed = 0;
	} else {
		vmcs_write16(HOST_FS_SELECTOR, 0);
		vmx->host_state.fs_reload_needed = 1;
	}
	vmx->host_state.gs_sel = read_gs();
	if (!(vmx->host_state.gs_sel & 7))
		vmcs_write16(HOST_GS_SELECTOR, vmx->host_state.gs_sel);
	else {
		vmcs_write16(HOST_GS_SELECTOR, 0);
		vmx->host_state.gs_ldt_reload_needed = 1;
	}

#ifdef CONFIG_X86_64
	vmcs_writel(HOST_FS_BASE, read_msr(MSR_FS_BASE));
	vmcs_writel(HOST_GS_BASE, read_msr(MSR_GS_BASE));
#else
	vmcs_writel(HOST_FS_BASE, segment_base(vmx->host_state.fs_sel));
	vmcs_writel(HOST_GS_BASE, segment_base(vmx->host_state.gs_sel));
#endif

#ifdef CONFIG_X86_64
	if (is_long_mode(&vmx->vcpu)) {
		save_msrs(vmx->host_msrs +
			  vmx->msr_offset_kernel_gs_base, 1);
	}
#endif
	load_msrs(vmx->guest_msrs, vmx->save_nmsrs);
	if (msr_efer_need_save_restore(vmx))
		load_transition_efer(vmx);
}

static void vmx_load_host_state(struct vcpu_vmx *vmx)
{
	unsigned long flags;

	if (!vmx->host_state.loaded)
		return;

	vmx->host_state.loaded = 0;
	if (vmx->host_state.fs_reload_needed)
		load_fs(vmx->host_state.fs_sel);
	if (vmx->host_state.gs_ldt_reload_needed) {
		load_ldt(vmx->host_state.ldt_sel);
		/*
		 * If we have to reload gs, we must take care to
		 * preserve our gs base.
		 */
		local_irq_save(flags);
		load_gs(vmx->host_state.gs_sel);
#ifdef CONFIG_X86_64
		wrmsrl(MSR_GS_BASE, vmcs_readl(HOST_GS_BASE));
#endif
		local_irq_restore(flags);
	}
	reload_tss();
	save_msrs(vmx->guest_msrs, vmx->save_nmsrs);
	load_msrs(vmx->host_msrs, vmx->save_nmsrs);
	if (msr_efer_need_save_restore(vmx))
		load_msrs(vmx->host_msrs + vmx->msr_offset_efer, 1);
}

/*
 * Switches to specified vcpu, until a matching vcpu_put(), but assumes
 * vcpu mutex is already taken.
 */
static void vmx_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u64 phys_addr = __pa(vmx->vmcs);
	u64 tsc_this, delta;

	if (vcpu->cpu != cpu) {
		vcpu_clear(vmx);
		kvm_migrate_apic_timer(vcpu);
	}

	if (per_cpu(current_vmcs, cpu) != vmx->vmcs) {
		u8 error;

		per_cpu(current_vmcs, cpu) = vmx->vmcs;
		asm volatile (ASM_VMX_VMPTRLD_RAX "; setna %0"
			      : "=g"(error) : "a"(&phys_addr), "m"(phys_addr)
			      : "cc");
		if (error)
			printk(KERN_ERR "kvm: vmptrld %p/%llx fail\n",
			       vmx->vmcs, phys_addr);
	}

	if (vcpu->cpu != cpu) {
		struct descriptor_table dt;
		unsigned long sysenter_esp;

		vcpu->cpu = cpu;
		/*
		 * Linux uses per-cpu TSS and GDT, so set these when switching
		 * processors.
		 */
		vmcs_writel(HOST_TR_BASE, read_tr_base()); /* 22.2.4 */
		get_gdt(&dt);
		vmcs_writel(HOST_GDTR_BASE, dt.base);   /* 22.2.4 */

		rdmsrl(MSR_IA32_SYSENTER_ESP, sysenter_esp);
		vmcs_writel(HOST_IA32_SYSENTER_ESP, sysenter_esp); /* 22.2.3 */

		/*
		 * Make sure the time stamp counter is monotonous.
		 */
		rdtscll(tsc_this);
		delta = vcpu->host_tsc - tsc_this;
		vmcs_write64(TSC_OFFSET, vmcs_read64(TSC_OFFSET) + delta);
	}
}

static void vmx_vcpu_put(struct kvm_vcpu *vcpu)
{
	vmx_load_host_state(to_vmx(vcpu));
	kvm_put_guest_fpu(vcpu);
}

static void vmx_fpu_activate(struct kvm_vcpu *vcpu)
{
	if (vcpu->fpu_active)
		return;
	vcpu->fpu_active = 1;
	vmcs_clear_bits(GUEST_CR0, X86_CR0_TS);
	if (vcpu->cr0 & X86_CR0_TS)
		vmcs_set_bits(GUEST_CR0, X86_CR0_TS);
	update_exception_bitmap(vcpu);
}

static void vmx_fpu_deactivate(struct kvm_vcpu *vcpu)
{
	if (!vcpu->fpu_active)
		return;
	vcpu->fpu_active = 0;
	vmcs_set_bits(GUEST_CR0, X86_CR0_TS);
	update_exception_bitmap(vcpu);
}

static void vmx_vcpu_decache(struct kvm_vcpu *vcpu)
{
	vcpu_clear(to_vmx(vcpu));
}

static unsigned long vmx_get_rflags(struct kvm_vcpu *vcpu)
{
	return vmcs_readl(GUEST_RFLAGS);
}

static void vmx_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
	vmcs_writel(GUEST_RFLAGS, rflags);
}

static void skip_emulated_instruction(struct kvm_vcpu *vcpu)
{
	unsigned long rip;
	u32 interruptibility;

	rip = vmcs_readl(GUEST_RIP);
	rip += vmcs_read32(VM_EXIT_INSTRUCTION_LEN);
	vmcs_writel(GUEST_RIP, rip);

	/*
	 * We emulated an instruction, so temporary interrupt blocking
	 * should be removed, if set.
	 */
	interruptibility = vmcs_read32(GUEST_INTERRUPTIBILITY_INFO);
	if (interruptibility & 3)
		vmcs_write32(GUEST_INTERRUPTIBILITY_INFO,
			     interruptibility & ~3);
	vcpu->interrupt_window_open = 1;
}

static void vmx_inject_gp(struct kvm_vcpu *vcpu, unsigned error_code)
{
	printk(KERN_DEBUG "inject_general_protection: rip 0x%lx\n",
	       vmcs_readl(GUEST_RIP));
	vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, error_code);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
		     GP_VECTOR |
		     INTR_TYPE_EXCEPTION |
		     INTR_INFO_DELIEVER_CODE_MASK |
		     INTR_INFO_VALID_MASK);
}

/*
 * Swap MSR entry in host/guest MSR entry array.
 */
#ifdef CONFIG_X86_64
static void move_msr_up(struct vcpu_vmx *vmx, int from, int to)
{
	struct kvm_msr_entry tmp;

	tmp = vmx->guest_msrs[to];
	vmx->guest_msrs[to] = vmx->guest_msrs[from];
	vmx->guest_msrs[from] = tmp;
	tmp = vmx->host_msrs[to];
	vmx->host_msrs[to] = vmx->host_msrs[from];
	vmx->host_msrs[from] = tmp;
}
#endif

/*
 * Set up the vmcs to automatically save and restore system
 * msrs.  Don't touch the 64-bit msrs if the guest is in legacy
 * mode, as fiddling with msrs is very expensive.
 */
static void setup_msrs(struct vcpu_vmx *vmx)
{
	int save_nmsrs;

	save_nmsrs = 0;
#ifdef CONFIG_X86_64
	if (is_long_mode(&vmx->vcpu)) {
		int index;

		index = __find_msr_index(vmx, MSR_SYSCALL_MASK);
		if (index >= 0)
			move_msr_up(vmx, index, save_nmsrs++);
		index = __find_msr_index(vmx, MSR_LSTAR);
		if (index >= 0)
			move_msr_up(vmx, index, save_nmsrs++);
		index = __find_msr_index(vmx, MSR_CSTAR);
		if (index >= 0)
			move_msr_up(vmx, index, save_nmsrs++);
		index = __find_msr_index(vmx, MSR_KERNEL_GS_BASE);
		if (index >= 0)
			move_msr_up(vmx, index, save_nmsrs++);
		/*
		 * MSR_K6_STAR is only needed on long mode guests, and only
		 * if efer.sce is enabled.
		 */
		index = __find_msr_index(vmx, MSR_K6_STAR);
		if ((index >= 0) && (vmx->vcpu.shadow_efer & EFER_SCE))
			move_msr_up(vmx, index, save_nmsrs++);
	}
#endif
	vmx->save_nmsrs = save_nmsrs;

#ifdef CONFIG_X86_64
	vmx->msr_offset_kernel_gs_base =
		__find_msr_index(vmx, MSR_KERNEL_GS_BASE);
#endif
	vmx->msr_offset_efer = __find_msr_index(vmx, MSR_EFER);
}

/*
 * reads and returns guest's timestamp counter "register"
 * guest_tsc = host_tsc + tsc_offset    -- 21.3
 */
static u64 guest_read_tsc(void)
{
	u64 host_tsc, tsc_offset;

	rdtscll(host_tsc);
	tsc_offset = vmcs_read64(TSC_OFFSET);
	return host_tsc + tsc_offset;
}

/*
 * writes 'guest_tsc' into guest's timestamp counter "register"
 * guest_tsc = host_tsc + tsc_offset ==> tsc_offset = guest_tsc - host_tsc
 */
static void guest_write_tsc(u64 guest_tsc)
{
	u64 host_tsc;

	rdtscll(host_tsc);
	vmcs_write64(TSC_OFFSET, guest_tsc - host_tsc);
}

/*
 * Reads an msr value (of 'msr_index') into 'pdata'.
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int vmx_get_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata)
{
	u64 data;
	struct kvm_msr_entry *msr;

	if (!pdata) {
		printk(KERN_ERR "BUG: get_msr called with NULL pdata\n");
		return -EINVAL;
	}

	switch (msr_index) {
#ifdef CONFIG_X86_64
	case MSR_FS_BASE:
		data = vmcs_readl(GUEST_FS_BASE);
		break;
	case MSR_GS_BASE:
		data = vmcs_readl(GUEST_GS_BASE);
		break;
	case MSR_EFER:
		return kvm_get_msr_common(vcpu, msr_index, pdata);
#endif
	case MSR_IA32_TIME_STAMP_COUNTER:
		data = guest_read_tsc();
		break;
	case MSR_IA32_SYSENTER_CS:
		data = vmcs_read32(GUEST_SYSENTER_CS);
		break;
	case MSR_IA32_SYSENTER_EIP:
		data = vmcs_readl(GUEST_SYSENTER_EIP);
		break;
	case MSR_IA32_SYSENTER_ESP:
		data = vmcs_readl(GUEST_SYSENTER_ESP);
		break;
	default:
		msr = find_msr_entry(to_vmx(vcpu), msr_index);
		if (msr) {
			data = msr->data;
			break;
		}
		return kvm_get_msr_common(vcpu, msr_index, pdata);
	}

	*pdata = data;
	return 0;
}

/*
 * Writes msr value into into the appropriate "register".
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int vmx_set_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 data)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct kvm_msr_entry *msr;
	int ret = 0;

	switch (msr_index) {
#ifdef CONFIG_X86_64
	case MSR_EFER:
		ret = kvm_set_msr_common(vcpu, msr_index, data);
		if (vmx->host_state.loaded)
			load_transition_efer(vmx);
		break;
	case MSR_FS_BASE:
		vmcs_writel(GUEST_FS_BASE, data);
		break;
	case MSR_GS_BASE:
		vmcs_writel(GUEST_GS_BASE, data);
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		vmcs_write32(GUEST_SYSENTER_CS, data);
		break;
	case MSR_IA32_SYSENTER_EIP:
		vmcs_writel(GUEST_SYSENTER_EIP, data);
		break;
	case MSR_IA32_SYSENTER_ESP:
		vmcs_writel(GUEST_SYSENTER_ESP, data);
		break;
	case MSR_IA32_TIME_STAMP_COUNTER:
		guest_write_tsc(data);
		break;
	default:
		msr = find_msr_entry(vmx, msr_index);
		if (msr) {
			msr->data = data;
			if (vmx->host_state.loaded)
				load_msrs(vmx->guest_msrs, vmx->save_nmsrs);
			break;
		}
		ret = kvm_set_msr_common(vcpu, msr_index, data);
	}

	return ret;
}

/*
 * Sync the rsp and rip registers into the vcpu structure.  This allows
 * registers to be accessed by indexing vcpu->regs.
 */
static void vcpu_load_rsp_rip(struct kvm_vcpu *vcpu)
{
	vcpu->regs[VCPU_REGS_RSP] = vmcs_readl(GUEST_RSP);
	vcpu->rip = vmcs_readl(GUEST_RIP);
}

/*
 * Syncs rsp and rip back into the vmcs.  Should be called after possible
 * modification.
 */
static void vcpu_put_rsp_rip(struct kvm_vcpu *vcpu)
{
	vmcs_writel(GUEST_RSP, vcpu->regs[VCPU_REGS_RSP]);
	vmcs_writel(GUEST_RIP, vcpu->rip);
}

static int set_guest_debug(struct kvm_vcpu *vcpu, struct kvm_debug_guest *dbg)
{
	unsigned long dr7 = 0x400;
	int old_singlestep;

	old_singlestep = vcpu->guest_debug.singlestep;

	vcpu->guest_debug.enabled = dbg->enabled;
	if (vcpu->guest_debug.enabled) {
		int i;

		dr7 |= 0x200;  /* exact */
		for (i = 0; i < 4; ++i) {
			if (!dbg->breakpoints[i].enabled)
				continue;
			vcpu->guest_debug.bp[i] = dbg->breakpoints[i].address;
			dr7 |= 2 << (i*2);    /* global enable */
			dr7 |= 0 << (i*4+16); /* execution breakpoint */
		}

		vcpu->guest_debug.singlestep = dbg->singlestep;
	} else
		vcpu->guest_debug.singlestep = 0;

	if (old_singlestep && !vcpu->guest_debug.singlestep) {
		unsigned long flags;

		flags = vmcs_readl(GUEST_RFLAGS);
		flags &= ~(X86_EFLAGS_TF | X86_EFLAGS_RF);
		vmcs_writel(GUEST_RFLAGS, flags);
	}

	update_exception_bitmap(vcpu);
	vmcs_writel(GUEST_DR7, dr7);

	return 0;
}

static int vmx_get_irq(struct kvm_vcpu *vcpu)
{
	u32 idtv_info_field;

	idtv_info_field = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	if (idtv_info_field & INTR_INFO_VALID_MASK) {
		if (is_external_interrupt(idtv_info_field))
			return idtv_info_field & VECTORING_INFO_VECTOR_MASK;
		else
			printk("pending exception: not handled yet\n");
	}
	return -1;
}

static __init int cpu_has_kvm_support(void)
{
	unsigned long ecx = cpuid_ecx(1);
	return test_bit(5, &ecx); /* CPUID.1:ECX.VMX[bit 5] -> VT */
}

static __init int vmx_disabled_by_bios(void)
{
	u64 msr;

	rdmsrl(MSR_IA32_FEATURE_CONTROL, msr);
	return (msr & (MSR_IA32_FEATURE_CONTROL_LOCKED |
		       MSR_IA32_FEATURE_CONTROL_VMXON_ENABLED))
	    == MSR_IA32_FEATURE_CONTROL_LOCKED;
	/* locked but not enabled */
}

static void hardware_enable(void *garbage)
{
	int cpu = raw_smp_processor_id();
	u64 phys_addr = __pa(per_cpu(vmxarea, cpu));
	u64 old;

	rdmsrl(MSR_IA32_FEATURE_CONTROL, old);
	if ((old & (MSR_IA32_FEATURE_CONTROL_LOCKED |
		    MSR_IA32_FEATURE_CONTROL_VMXON_ENABLED))
	    != (MSR_IA32_FEATURE_CONTROL_LOCKED |
		MSR_IA32_FEATURE_CONTROL_VMXON_ENABLED))
		/* enable and lock */
		wrmsrl(MSR_IA32_FEATURE_CONTROL, old |
		       MSR_IA32_FEATURE_CONTROL_LOCKED |
		       MSR_IA32_FEATURE_CONTROL_VMXON_ENABLED);
	write_cr4(read_cr4() | X86_CR4_VMXE); /* FIXME: not cpu hotplug safe */
	asm volatile (ASM_VMX_VMXON_RAX : : "a"(&phys_addr), "m"(phys_addr)
		      : "memory", "cc");
}

static void hardware_disable(void *garbage)
{
	asm volatile (ASM_VMX_VMXOFF : : : "cc");
}

static __init int adjust_vmx_controls(u32 ctl_min, u32 ctl_opt,
				      u32 msr, u32* result)
{
	u32 vmx_msr_low, vmx_msr_high;
	u32 ctl = ctl_min | ctl_opt;

	rdmsr(msr, vmx_msr_low, vmx_msr_high);

	ctl &= vmx_msr_high; /* bit == 0 in high word ==> must be zero */
	ctl |= vmx_msr_low;  /* bit == 1 in low word  ==> must be one  */

	/* Ensure minimum (required) set of control bits are supported. */
	if (ctl_min & ~ctl)
		return -EIO;

	*result = ctl;
	return 0;
}

static __init int setup_vmcs_config(struct vmcs_config *vmcs_conf)
{
	u32 vmx_msr_low, vmx_msr_high;
	u32 min, opt;
	u32 _pin_based_exec_control = 0;
	u32 _cpu_based_exec_control = 0;
	u32 _vmexit_control = 0;
	u32 _vmentry_control = 0;

	min = PIN_BASED_EXT_INTR_MASK | PIN_BASED_NMI_EXITING;
	opt = 0;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_PINBASED_CTLS,
				&_pin_based_exec_control) < 0)
		return -EIO;

	min = CPU_BASED_HLT_EXITING |
#ifdef CONFIG_X86_64
	      CPU_BASED_CR8_LOAD_EXITING |
	      CPU_BASED_CR8_STORE_EXITING |
#endif
	      CPU_BASED_USE_IO_BITMAPS |
	      CPU_BASED_MOV_DR_EXITING |
	      CPU_BASED_USE_TSC_OFFSETING;
#ifdef CONFIG_X86_64
	opt = CPU_BASED_TPR_SHADOW;
#else
	opt = 0;
#endif
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_PROCBASED_CTLS,
				&_cpu_based_exec_control) < 0)
		return -EIO;
#ifdef CONFIG_X86_64
	if ((_cpu_based_exec_control & CPU_BASED_TPR_SHADOW))
		_cpu_based_exec_control &= ~CPU_BASED_CR8_LOAD_EXITING &
					   ~CPU_BASED_CR8_STORE_EXITING;
#endif

	min = 0;
#ifdef CONFIG_X86_64
	min |= VM_EXIT_HOST_ADDR_SPACE_SIZE;
#endif
	opt = 0;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_EXIT_CTLS,
				&_vmexit_control) < 0)
		return -EIO;

	min = opt = 0;
	if (adjust_vmx_controls(min, opt, MSR_IA32_VMX_ENTRY_CTLS,
				&_vmentry_control) < 0)
		return -EIO;

	rdmsr(MSR_IA32_VMX_BASIC, vmx_msr_low, vmx_msr_high);

	/* IA-32 SDM Vol 3B: VMCS size is never greater than 4kB. */
	if ((vmx_msr_high & 0x1fff) > PAGE_SIZE)
		return -EIO;

#ifdef CONFIG_X86_64
	/* IA-32 SDM Vol 3B: 64-bit CPUs always have VMX_BASIC_MSR[48]==0. */
	if (vmx_msr_high & (1u<<16))
		return -EIO;
#endif

	/* Require Write-Back (WB) memory type for VMCS accesses. */
	if (((vmx_msr_high >> 18) & 15) != 6)
		return -EIO;

	vmcs_conf->size = vmx_msr_high & 0x1fff;
	vmcs_conf->order = get_order(vmcs_config.size);
	vmcs_conf->revision_id = vmx_msr_low;

	vmcs_conf->pin_based_exec_ctrl = _pin_based_exec_control;
	vmcs_conf->cpu_based_exec_ctrl = _cpu_based_exec_control;
	vmcs_conf->vmexit_ctrl         = _vmexit_control;
	vmcs_conf->vmentry_ctrl        = _vmentry_control;

	return 0;
}

static struct vmcs *alloc_vmcs_cpu(int cpu)
{
	int node = cpu_to_node(cpu);
	struct page *pages;
	struct vmcs *vmcs;

	pages = alloc_pages_node(node, GFP_KERNEL, vmcs_config.order);
	if (!pages)
		return NULL;
	vmcs = page_address(pages);
	memset(vmcs, 0, vmcs_config.size);
	vmcs->revision_id = vmcs_config.revision_id; /* vmcs revision id */
	return vmcs;
}

static struct vmcs *alloc_vmcs(void)
{
	return alloc_vmcs_cpu(raw_smp_processor_id());
}

static void free_vmcs(struct vmcs *vmcs)
{
	free_pages((unsigned long)vmcs, vmcs_config.order);
}

static void free_kvm_area(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		free_vmcs(per_cpu(vmxarea, cpu));
}

static __init int alloc_kvm_area(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct vmcs *vmcs;

		vmcs = alloc_vmcs_cpu(cpu);
		if (!vmcs) {
			free_kvm_area();
			return -ENOMEM;
		}

		per_cpu(vmxarea, cpu) = vmcs;
	}
	return 0;
}

static __init int hardware_setup(void)
{
	if (setup_vmcs_config(&vmcs_config) < 0)
		return -EIO;
	return alloc_kvm_area();
}

static __exit void hardware_unsetup(void)
{
	free_kvm_area();
}

static void fix_pmode_dataseg(int seg, struct kvm_save_segment *save)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	if (vmcs_readl(sf->base) == save->base && (save->base & AR_S_MASK)) {
		vmcs_write16(sf->selector, save->selector);
		vmcs_writel(sf->base, save->base);
		vmcs_write32(sf->limit, save->limit);
		vmcs_write32(sf->ar_bytes, save->ar);
	} else {
		u32 dpl = (vmcs_read16(sf->selector) & SELECTOR_RPL_MASK)
			<< AR_DPL_SHIFT;
		vmcs_write32(sf->ar_bytes, 0x93 | dpl);
	}
}

static void enter_pmode(struct kvm_vcpu *vcpu)
{
	unsigned long flags;

	vcpu->rmode.active = 0;

	vmcs_writel(GUEST_TR_BASE, vcpu->rmode.tr.base);
	vmcs_write32(GUEST_TR_LIMIT, vcpu->rmode.tr.limit);
	vmcs_write32(GUEST_TR_AR_BYTES, vcpu->rmode.tr.ar);

	flags = vmcs_readl(GUEST_RFLAGS);
	flags &= ~(IOPL_MASK | X86_EFLAGS_VM);
	flags |= (vcpu->rmode.save_iopl << IOPL_SHIFT);
	vmcs_writel(GUEST_RFLAGS, flags);

	vmcs_writel(GUEST_CR4, (vmcs_readl(GUEST_CR4) & ~X86_CR4_VME) |
			(vmcs_readl(CR4_READ_SHADOW) & X86_CR4_VME));

	update_exception_bitmap(vcpu);

	fix_pmode_dataseg(VCPU_SREG_ES, &vcpu->rmode.es);
	fix_pmode_dataseg(VCPU_SREG_DS, &vcpu->rmode.ds);
	fix_pmode_dataseg(VCPU_SREG_GS, &vcpu->rmode.gs);
	fix_pmode_dataseg(VCPU_SREG_FS, &vcpu->rmode.fs);

	vmcs_write16(GUEST_SS_SELECTOR, 0);
	vmcs_write32(GUEST_SS_AR_BYTES, 0x93);

	vmcs_write16(GUEST_CS_SELECTOR,
		     vmcs_read16(GUEST_CS_SELECTOR) & ~SELECTOR_RPL_MASK);
	vmcs_write32(GUEST_CS_AR_BYTES, 0x9b);
}

static gva_t rmode_tss_base(struct kvm* kvm)
{
	gfn_t base_gfn = kvm->memslots[0].base_gfn + kvm->memslots[0].npages - 3;
	return base_gfn << PAGE_SHIFT;
}

static void fix_rmode_seg(int seg, struct kvm_save_segment *save)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	save->selector = vmcs_read16(sf->selector);
	save->base = vmcs_readl(sf->base);
	save->limit = vmcs_read32(sf->limit);
	save->ar = vmcs_read32(sf->ar_bytes);
	vmcs_write16(sf->selector, vmcs_readl(sf->base) >> 4);
	vmcs_write32(sf->limit, 0xffff);
	vmcs_write32(sf->ar_bytes, 0xf3);
}

static void enter_rmode(struct kvm_vcpu *vcpu)
{
	unsigned long flags;

	vcpu->rmode.active = 1;

	vcpu->rmode.tr.base = vmcs_readl(GUEST_TR_BASE);
	vmcs_writel(GUEST_TR_BASE, rmode_tss_base(vcpu->kvm));

	vcpu->rmode.tr.limit = vmcs_read32(GUEST_TR_LIMIT);
	vmcs_write32(GUEST_TR_LIMIT, RMODE_TSS_SIZE - 1);

	vcpu->rmode.tr.ar = vmcs_read32(GUEST_TR_AR_BYTES);
	vmcs_write32(GUEST_TR_AR_BYTES, 0x008b);

	flags = vmcs_readl(GUEST_RFLAGS);
	vcpu->rmode.save_iopl = (flags & IOPL_MASK) >> IOPL_SHIFT;

	flags |= IOPL_MASK | X86_EFLAGS_VM;

	vmcs_writel(GUEST_RFLAGS, flags);
	vmcs_writel(GUEST_CR4, vmcs_readl(GUEST_CR4) | X86_CR4_VME);
	update_exception_bitmap(vcpu);

	vmcs_write16(GUEST_SS_SELECTOR, vmcs_readl(GUEST_SS_BASE) >> 4);
	vmcs_write32(GUEST_SS_LIMIT, 0xffff);
	vmcs_write32(GUEST_SS_AR_BYTES, 0xf3);

	vmcs_write32(GUEST_CS_AR_BYTES, 0xf3);
	vmcs_write32(GUEST_CS_LIMIT, 0xffff);
	if (vmcs_readl(GUEST_CS_BASE) == 0xffff0000)
		vmcs_writel(GUEST_CS_BASE, 0xf0000);
	vmcs_write16(GUEST_CS_SELECTOR, vmcs_readl(GUEST_CS_BASE) >> 4);

	fix_rmode_seg(VCPU_SREG_ES, &vcpu->rmode.es);
	fix_rmode_seg(VCPU_SREG_DS, &vcpu->rmode.ds);
	fix_rmode_seg(VCPU_SREG_GS, &vcpu->rmode.gs);
	fix_rmode_seg(VCPU_SREG_FS, &vcpu->rmode.fs);

	init_rmode_tss(vcpu->kvm);
}

#ifdef CONFIG_X86_64

static void enter_lmode(struct kvm_vcpu *vcpu)
{
	u32 guest_tr_ar;

	guest_tr_ar = vmcs_read32(GUEST_TR_AR_BYTES);
	if ((guest_tr_ar & AR_TYPE_MASK) != AR_TYPE_BUSY_64_TSS) {
		printk(KERN_DEBUG "%s: tss fixup for long mode. \n",
		       __FUNCTION__);
		vmcs_write32(GUEST_TR_AR_BYTES,
			     (guest_tr_ar & ~AR_TYPE_MASK)
			     | AR_TYPE_BUSY_64_TSS);
	}

	vcpu->shadow_efer |= EFER_LMA;

	find_msr_entry(to_vmx(vcpu), MSR_EFER)->data |= EFER_LMA | EFER_LME;
	vmcs_write32(VM_ENTRY_CONTROLS,
		     vmcs_read32(VM_ENTRY_CONTROLS)
		     | VM_ENTRY_IA32E_MODE);
}

static void exit_lmode(struct kvm_vcpu *vcpu)
{
	vcpu->shadow_efer &= ~EFER_LMA;

	vmcs_write32(VM_ENTRY_CONTROLS,
		     vmcs_read32(VM_ENTRY_CONTROLS)
		     & ~VM_ENTRY_IA32E_MODE);
}

#endif

static void vmx_decache_cr4_guest_bits(struct kvm_vcpu *vcpu)
{
	vcpu->cr4 &= KVM_GUEST_CR4_MASK;
	vcpu->cr4 |= vmcs_readl(GUEST_CR4) & ~KVM_GUEST_CR4_MASK;
}

static void vmx_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	vmx_fpu_deactivate(vcpu);

	if (vcpu->rmode.active && (cr0 & X86_CR0_PE))
		enter_pmode(vcpu);

	if (!vcpu->rmode.active && !(cr0 & X86_CR0_PE))
		enter_rmode(vcpu);

#ifdef CONFIG_X86_64
	if (vcpu->shadow_efer & EFER_LME) {
		if (!is_paging(vcpu) && (cr0 & X86_CR0_PG))
			enter_lmode(vcpu);
		if (is_paging(vcpu) && !(cr0 & X86_CR0_PG))
			exit_lmode(vcpu);
	}
#endif

	vmcs_writel(CR0_READ_SHADOW, cr0);
	vmcs_writel(GUEST_CR0,
		    (cr0 & ~KVM_GUEST_CR0_MASK) | KVM_VM_CR0_ALWAYS_ON);
	vcpu->cr0 = cr0;

	if (!(cr0 & X86_CR0_TS) || !(cr0 & X86_CR0_PE))
		vmx_fpu_activate(vcpu);
}

static void vmx_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	vmcs_writel(GUEST_CR3, cr3);
	if (vcpu->cr0 & X86_CR0_PE)
		vmx_fpu_deactivate(vcpu);
}

static void vmx_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	vmcs_writel(CR4_READ_SHADOW, cr4);
	vmcs_writel(GUEST_CR4, cr4 | (vcpu->rmode.active ?
		    KVM_RMODE_VM_CR4_ALWAYS_ON : KVM_PMODE_VM_CR4_ALWAYS_ON));
	vcpu->cr4 = cr4;
}

#ifdef CONFIG_X86_64

static void vmx_set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	struct kvm_msr_entry *msr = find_msr_entry(vmx, MSR_EFER);

	vcpu->shadow_efer = efer;
	if (efer & EFER_LMA) {
		vmcs_write32(VM_ENTRY_CONTROLS,
				     vmcs_read32(VM_ENTRY_CONTROLS) |
				     VM_ENTRY_IA32E_MODE);
		msr->data = efer;

	} else {
		vmcs_write32(VM_ENTRY_CONTROLS,
				     vmcs_read32(VM_ENTRY_CONTROLS) &
				     ~VM_ENTRY_IA32E_MODE);

		msr->data = efer & ~EFER_LME;
	}
	setup_msrs(vmx);
}

#endif

static u64 vmx_get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	return vmcs_readl(sf->base);
}

static void vmx_get_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];
	u32 ar;

	var->base = vmcs_readl(sf->base);
	var->limit = vmcs_read32(sf->limit);
	var->selector = vmcs_read16(sf->selector);
	ar = vmcs_read32(sf->ar_bytes);
	if (ar & AR_UNUSABLE_MASK)
		ar = 0;
	var->type = ar & 15;
	var->s = (ar >> 4) & 1;
	var->dpl = (ar >> 5) & 3;
	var->present = (ar >> 7) & 1;
	var->avl = (ar >> 12) & 1;
	var->l = (ar >> 13) & 1;
	var->db = (ar >> 14) & 1;
	var->g = (ar >> 15) & 1;
	var->unusable = (ar >> 16) & 1;
}

static u32 vmx_segment_access_rights(struct kvm_segment *var)
{
	u32 ar;

	if (var->unusable)
		ar = 1 << 16;
	else {
		ar = var->type & 15;
		ar |= (var->s & 1) << 4;
		ar |= (var->dpl & 3) << 5;
		ar |= (var->present & 1) << 7;
		ar |= (var->avl & 1) << 12;
		ar |= (var->l & 1) << 13;
		ar |= (var->db & 1) << 14;
		ar |= (var->g & 1) << 15;
	}
	if (ar == 0) /* a 0 value means unusable */
		ar = AR_UNUSABLE_MASK;

	return ar;
}

static void vmx_set_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];
	u32 ar;

	if (vcpu->rmode.active && seg == VCPU_SREG_TR) {
		vcpu->rmode.tr.selector = var->selector;
		vcpu->rmode.tr.base = var->base;
		vcpu->rmode.tr.limit = var->limit;
		vcpu->rmode.tr.ar = vmx_segment_access_rights(var);
		return;
	}
	vmcs_writel(sf->base, var->base);
	vmcs_write32(sf->limit, var->limit);
	vmcs_write16(sf->selector, var->selector);
	if (vcpu->rmode.active && var->s) {
		/*
		 * Hack real-mode segments into vm86 compatibility.
		 */
		if (var->base == 0xffff0000 && var->selector == 0xf000)
			vmcs_writel(sf->base, 0xf0000);
		ar = 0xf3;
	} else
		ar = vmx_segment_access_rights(var);
	vmcs_write32(sf->ar_bytes, ar);
}

static void vmx_get_cs_db_l_bits(struct kvm_vcpu *vcpu, int *db, int *l)
{
	u32 ar = vmcs_read32(GUEST_CS_AR_BYTES);

	*db = (ar >> 14) & 1;
	*l = (ar >> 13) & 1;
}

static void vmx_get_idt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	dt->limit = vmcs_read32(GUEST_IDTR_LIMIT);
	dt->base = vmcs_readl(GUEST_IDTR_BASE);
}

static void vmx_set_idt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	vmcs_write32(GUEST_IDTR_LIMIT, dt->limit);
	vmcs_writel(GUEST_IDTR_BASE, dt->base);
}

static void vmx_get_gdt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	dt->limit = vmcs_read32(GUEST_GDTR_LIMIT);
	dt->base = vmcs_readl(GUEST_GDTR_BASE);
}

static void vmx_set_gdt(struct kvm_vcpu *vcpu, struct descriptor_table *dt)
{
	vmcs_write32(GUEST_GDTR_LIMIT, dt->limit);
	vmcs_writel(GUEST_GDTR_BASE, dt->base);
}

static int init_rmode_tss(struct kvm* kvm)
{
	struct page *p1, *p2, *p3;
	gfn_t fn = rmode_tss_base(kvm) >> PAGE_SHIFT;
	char *page;

	p1 = gfn_to_page(kvm, fn++);
	p2 = gfn_to_page(kvm, fn++);
	p3 = gfn_to_page(kvm, fn);

	if (!p1 || !p2 || !p3) {
		kvm_printf(kvm,"%s: gfn_to_page failed\n", __FUNCTION__);
		return 0;
	}

	page = kmap_atomic(p1, KM_USER0);
	clear_page(page);
	*(u16*)(page + 0x66) = TSS_BASE_SIZE + TSS_REDIRECTION_SIZE;
	kunmap_atomic(page, KM_USER0);

	page = kmap_atomic(p2, KM_USER0);
	clear_page(page);
	kunmap_atomic(page, KM_USER0);

	page = kmap_atomic(p3, KM_USER0);
	clear_page(page);
	*(page + RMODE_TSS_SIZE - 2 * PAGE_SIZE - 1) = ~0;
	kunmap_atomic(page, KM_USER0);

	return 1;
}

static void seg_setup(int seg)
{
	struct kvm_vmx_segment_field *sf = &kvm_vmx_segment_fields[seg];

	vmcs_write16(sf->selector, 0);
	vmcs_writel(sf->base, 0);
	vmcs_write32(sf->limit, 0xffff);
	vmcs_write32(sf->ar_bytes, 0x93);
}

/*
 * Sets up the vmcs for emulated real mode.
 */
static int vmx_vcpu_setup(struct vcpu_vmx *vmx)
{
	u32 host_sysenter_cs;
	u32 junk;
	unsigned long a;
	struct descriptor_table dt;
	int i;
	int ret = 0;
	unsigned long kvm_vmx_return;
	u64 msr;
	u32 exec_control;

	if (!init_rmode_tss(vmx->vcpu.kvm)) {
		ret = -ENOMEM;
		goto out;
	}

	vmx->vcpu.rmode.active = 0;

	vmx->vcpu.regs[VCPU_REGS_RDX] = get_rdx_init_val();
	set_cr8(&vmx->vcpu, 0);
	msr = 0xfee00000 | MSR_IA32_APICBASE_ENABLE;
	if (vmx->vcpu.vcpu_id == 0)
		msr |= MSR_IA32_APICBASE_BSP;
	kvm_set_apic_base(&vmx->vcpu, msr);

	fx_init(&vmx->vcpu);

	/*
	 * GUEST_CS_BASE should really be 0xffff0000, but VT vm86 mode
	 * insists on having GUEST_CS_BASE == GUEST_CS_SELECTOR << 4.  Sigh.
	 */
	if (vmx->vcpu.vcpu_id == 0) {
		vmcs_write16(GUEST_CS_SELECTOR, 0xf000);
		vmcs_writel(GUEST_CS_BASE, 0x000f0000);
	} else {
		vmcs_write16(GUEST_CS_SELECTOR, vmx->vcpu.sipi_vector << 8);
		vmcs_writel(GUEST_CS_BASE, vmx->vcpu.sipi_vector << 12);
	}
	vmcs_write32(GUEST_CS_LIMIT, 0xffff);
	vmcs_write32(GUEST_CS_AR_BYTES, 0x9b);

	seg_setup(VCPU_SREG_DS);
	seg_setup(VCPU_SREG_ES);
	seg_setup(VCPU_SREG_FS);
	seg_setup(VCPU_SREG_GS);
	seg_setup(VCPU_SREG_SS);

	vmcs_write16(GUEST_TR_SELECTOR, 0);
	vmcs_writel(GUEST_TR_BASE, 0);
	vmcs_write32(GUEST_TR_LIMIT, 0xffff);
	vmcs_write32(GUEST_TR_AR_BYTES, 0x008b);

	vmcs_write16(GUEST_LDTR_SELECTOR, 0);
	vmcs_writel(GUEST_LDTR_BASE, 0);
	vmcs_write32(GUEST_LDTR_LIMIT, 0xffff);
	vmcs_write32(GUEST_LDTR_AR_BYTES, 0x00082);

	vmcs_write32(GUEST_SYSENTER_CS, 0);
	vmcs_writel(GUEST_SYSENTER_ESP, 0);
	vmcs_writel(GUEST_SYSENTER_EIP, 0);

	vmcs_writel(GUEST_RFLAGS, 0x02);
	if (vmx->vcpu.vcpu_id == 0)
		vmcs_writel(GUEST_RIP, 0xfff0);
	else
		vmcs_writel(GUEST_RIP, 0);
	vmcs_writel(GUEST_RSP, 0);

	//todo: dr0 = dr1 = dr2 = dr3 = 0; dr6 = 0xffff0ff0
	vmcs_writel(GUEST_DR7, 0x400);

	vmcs_writel(GUEST_GDTR_BASE, 0);
	vmcs_write32(GUEST_GDTR_LIMIT, 0xffff);

	vmcs_writel(GUEST_IDTR_BASE, 0);
	vmcs_write32(GUEST_IDTR_LIMIT, 0xffff);

	vmcs_write32(GUEST_ACTIVITY_STATE, 0);
	vmcs_write32(GUEST_INTERRUPTIBILITY_INFO, 0);
	vmcs_write32(GUEST_PENDING_DBG_EXCEPTIONS, 0);

	/* I/O */
	vmcs_write64(IO_BITMAP_A, page_to_phys(vmx_io_bitmap_a));
	vmcs_write64(IO_BITMAP_B, page_to_phys(vmx_io_bitmap_b));

	guest_write_tsc(0);

	vmcs_write64(VMCS_LINK_POINTER, -1ull); /* 22.3.1.5 */

	/* Special registers */
	vmcs_write64(GUEST_IA32_DEBUGCTL, 0);

	/* Control */
	vmcs_write32(PIN_BASED_VM_EXEC_CONTROL,
		vmcs_config.pin_based_exec_ctrl);

	exec_control = vmcs_config.cpu_based_exec_ctrl;
	if (!vm_need_tpr_shadow(vmx->vcpu.kvm)) {
		exec_control &= ~CPU_BASED_TPR_SHADOW;
#ifdef CONFIG_X86_64
		exec_control |= CPU_BASED_CR8_STORE_EXITING |
				CPU_BASED_CR8_LOAD_EXITING;
#endif
	}
	vmcs_write32(CPU_BASED_VM_EXEC_CONTROL, exec_control);

	vmcs_write32(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmcs_write32(PAGE_FAULT_ERROR_CODE_MATCH, 0);
	vmcs_write32(CR3_TARGET_COUNT, 0);           /* 22.2.1 */

	vmcs_writel(HOST_CR0, read_cr0());  /* 22.2.3 */
	vmcs_writel(HOST_CR4, read_cr4());  /* 22.2.3, 22.2.5 */
	vmcs_writel(HOST_CR3, read_cr3());  /* 22.2.3  FIXME: shadow tables */

	vmcs_write16(HOST_CS_SELECTOR, __KERNEL_CS);  /* 22.2.4 */
	vmcs_write16(HOST_DS_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
	vmcs_write16(HOST_ES_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
	vmcs_write16(HOST_FS_SELECTOR, read_fs());    /* 22.2.4 */
	vmcs_write16(HOST_GS_SELECTOR, read_gs());    /* 22.2.4 */
	vmcs_write16(HOST_SS_SELECTOR, __KERNEL_DS);  /* 22.2.4 */
#ifdef CONFIG_X86_64
	rdmsrl(MSR_FS_BASE, a);
	vmcs_writel(HOST_FS_BASE, a); /* 22.2.4 */
	rdmsrl(MSR_GS_BASE, a);
	vmcs_writel(HOST_GS_BASE, a); /* 22.2.4 */
#else
	vmcs_writel(HOST_FS_BASE, 0); /* 22.2.4 */
	vmcs_writel(HOST_GS_BASE, 0); /* 22.2.4 */
#endif

	vmcs_write16(HOST_TR_SELECTOR, GDT_ENTRY_TSS*8);  /* 22.2.4 */

	get_idt(&dt);
	vmcs_writel(HOST_IDTR_BASE, dt.base);   /* 22.2.4 */

	asm ("mov $.Lkvm_vmx_return, %0" : "=r"(kvm_vmx_return));
	vmcs_writel(HOST_RIP, kvm_vmx_return); /* 22.2.5 */
	vmcs_write32(VM_EXIT_MSR_STORE_COUNT, 0);
	vmcs_write32(VM_EXIT_MSR_LOAD_COUNT, 0);
	vmcs_write32(VM_ENTRY_MSR_LOAD_COUNT, 0);

	rdmsr(MSR_IA32_SYSENTER_CS, host_sysenter_cs, junk);
	vmcs_write32(HOST_IA32_SYSENTER_CS, host_sysenter_cs);
	rdmsrl(MSR_IA32_SYSENTER_ESP, a);
	vmcs_writel(HOST_IA32_SYSENTER_ESP, a);   /* 22.2.3 */
	rdmsrl(MSR_IA32_SYSENTER_EIP, a);
	vmcs_writel(HOST_IA32_SYSENTER_EIP, a);   /* 22.2.3 */

	for (i = 0; i < NR_VMX_MSR; ++i) {
		u32 index = vmx_msr_index[i];
		u32 data_low, data_high;
		u64 data;
		int j = vmx->nmsrs;

		if (rdmsr_safe(index, &data_low, &data_high) < 0)
			continue;
		if (wrmsr_safe(index, data_low, data_high) < 0)
			continue;
		data = data_low | ((u64)data_high << 32);
		vmx->host_msrs[j].index = index;
		vmx->host_msrs[j].reserved = 0;
		vmx->host_msrs[j].data = data;
		vmx->guest_msrs[j] = vmx->host_msrs[j];
		++vmx->nmsrs;
	}

	setup_msrs(vmx);

	vmcs_write32(VM_EXIT_CONTROLS, vmcs_config.vmexit_ctrl);

	/* 22.2.1, 20.8.1 */
	vmcs_write32(VM_ENTRY_CONTROLS, vmcs_config.vmentry_ctrl);

	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, 0);  /* 22.2.1 */

#ifdef CONFIG_X86_64
	vmcs_write64(VIRTUAL_APIC_PAGE_ADDR, 0);
	if (vm_need_tpr_shadow(vmx->vcpu.kvm))
		vmcs_write64(VIRTUAL_APIC_PAGE_ADDR,
			     page_to_phys(vmx->vcpu.apic->regs_page));
	vmcs_write32(TPR_THRESHOLD, 0);
#endif

	vmcs_writel(CR0_GUEST_HOST_MASK, ~0UL);
	vmcs_writel(CR4_GUEST_HOST_MASK, KVM_GUEST_CR4_MASK);

	vmx->vcpu.cr0 = 0x60000010;
	vmx_set_cr0(&vmx->vcpu, vmx->vcpu.cr0); // enter rmode
	vmx_set_cr4(&vmx->vcpu, 0);
#ifdef CONFIG_X86_64
	vmx_set_efer(&vmx->vcpu, 0);
#endif
	vmx_fpu_activate(&vmx->vcpu);
	update_exception_bitmap(&vmx->vcpu);

	return 0;

out:
	return ret;
}

static void inject_rmode_irq(struct kvm_vcpu *vcpu, int irq)
{
	u16 ent[2];
	u16 cs;
	u16 ip;
	unsigned long flags;
	unsigned long ss_base = vmcs_readl(GUEST_SS_BASE);
	u16 sp =  vmcs_readl(GUEST_RSP);
	u32 ss_limit = vmcs_read32(GUEST_SS_LIMIT);

	if (sp > ss_limit || sp < 6 ) {
		vcpu_printf(vcpu, "%s: #SS, rsp 0x%lx ss 0x%lx limit 0x%x\n",
			    __FUNCTION__,
			    vmcs_readl(GUEST_RSP),
			    vmcs_readl(GUEST_SS_BASE),
			    vmcs_read32(GUEST_SS_LIMIT));
		return;
	}

	if (emulator_read_std(irq * sizeof(ent), &ent, sizeof(ent), vcpu) !=
							X86EMUL_CONTINUE) {
		vcpu_printf(vcpu, "%s: read guest err\n", __FUNCTION__);
		return;
	}

	flags =  vmcs_readl(GUEST_RFLAGS);
	cs =  vmcs_readl(GUEST_CS_BASE) >> 4;
	ip =  vmcs_readl(GUEST_RIP);


	if (emulator_write_emulated(ss_base + sp - 2, &flags, 2, vcpu) != X86EMUL_CONTINUE ||
	    emulator_write_emulated(ss_base + sp - 4, &cs, 2, vcpu) != X86EMUL_CONTINUE ||
	    emulator_write_emulated(ss_base + sp - 6, &ip, 2, vcpu) != X86EMUL_CONTINUE) {
		vcpu_printf(vcpu, "%s: write guest err\n", __FUNCTION__);
		return;
	}

	vmcs_writel(GUEST_RFLAGS, flags &
		    ~( X86_EFLAGS_IF | X86_EFLAGS_AC | X86_EFLAGS_TF));
	vmcs_write16(GUEST_CS_SELECTOR, ent[1]) ;
	vmcs_writel(GUEST_CS_BASE, ent[1] << 4);
	vmcs_writel(GUEST_RIP, ent[0]);
	vmcs_writel(GUEST_RSP, (vmcs_readl(GUEST_RSP) & ~0xffff) | (sp - 6));
}

static void vmx_inject_irq(struct kvm_vcpu *vcpu, int irq)
{
	if (vcpu->rmode.active) {
		inject_rmode_irq(vcpu, irq);
		return;
	}
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
			irq | INTR_TYPE_EXT_INTR | INTR_INFO_VALID_MASK);
}

static void kvm_do_inject_irq(struct kvm_vcpu *vcpu)
{
	int word_index = __ffs(vcpu->irq_summary);
	int bit_index = __ffs(vcpu->irq_pending[word_index]);
	int irq = word_index * BITS_PER_LONG + bit_index;

	clear_bit(bit_index, &vcpu->irq_pending[word_index]);
	if (!vcpu->irq_pending[word_index])
		clear_bit(word_index, &vcpu->irq_summary);
	vmx_inject_irq(vcpu, irq);
}


static void do_interrupt_requests(struct kvm_vcpu *vcpu,
				       struct kvm_run *kvm_run)
{
	u32 cpu_based_vm_exec_control;

	vcpu->interrupt_window_open =
		((vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF) &&
		 (vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) & 3) == 0);

	if (vcpu->interrupt_window_open &&
	    vcpu->irq_summary &&
	    !(vmcs_read32(VM_ENTRY_INTR_INFO_FIELD) & INTR_INFO_VALID_MASK))
		/*
		 * If interrupts enabled, and not blocked by sti or mov ss. Good.
		 */
		kvm_do_inject_irq(vcpu);

	cpu_based_vm_exec_control = vmcs_read32(CPU_BASED_VM_EXEC_CONTROL);
	if (!vcpu->interrupt_window_open &&
	    (vcpu->irq_summary || kvm_run->request_interrupt_window))
		/*
		 * Interrupts blocked.  Wait for unblock.
		 */
		cpu_based_vm_exec_control |= CPU_BASED_VIRTUAL_INTR_PENDING;
	else
		cpu_based_vm_exec_control &= ~CPU_BASED_VIRTUAL_INTR_PENDING;
	vmcs_write32(CPU_BASED_VM_EXEC_CONTROL, cpu_based_vm_exec_control);
}

static void kvm_guest_debug_pre(struct kvm_vcpu *vcpu)
{
	struct kvm_guest_debug *dbg = &vcpu->guest_debug;

	set_debugreg(dbg->bp[0], 0);
	set_debugreg(dbg->bp[1], 1);
	set_debugreg(dbg->bp[2], 2);
	set_debugreg(dbg->bp[3], 3);

	if (dbg->singlestep) {
		unsigned long flags;

		flags = vmcs_readl(GUEST_RFLAGS);
		flags |= X86_EFLAGS_TF | X86_EFLAGS_RF;
		vmcs_writel(GUEST_RFLAGS, flags);
	}
}

static int handle_rmode_exception(struct kvm_vcpu *vcpu,
				  int vec, u32 err_code)
{
	if (!vcpu->rmode.active)
		return 0;

	/*
	 * Instruction with address size override prefix opcode 0x67
	 * Cause the #SS fault with 0 error code in VM86 mode.
	 */
	if (((vec == GP_VECTOR) || (vec == SS_VECTOR)) && err_code == 0)
		if (emulate_instruction(vcpu, NULL, 0, 0) == EMULATE_DONE)
			return 1;
	return 0;
}

static int handle_exception(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u32 intr_info, error_code;
	unsigned long cr2, rip;
	u32 vect_info;
	enum emulation_result er;
	int r;

	vect_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	intr_info = vmcs_read32(VM_EXIT_INTR_INFO);

	if ((vect_info & VECTORING_INFO_VALID_MASK) &&
						!is_page_fault(intr_info)) {
		printk(KERN_ERR "%s: unexpected, vectoring info 0x%x "
		       "intr info 0x%x\n", __FUNCTION__, vect_info, intr_info);
	}

	if (!irqchip_in_kernel(vcpu->kvm) && is_external_interrupt(vect_info)) {
		int irq = vect_info & VECTORING_INFO_VECTOR_MASK;
		set_bit(irq, vcpu->irq_pending);
		set_bit(irq / BITS_PER_LONG, &vcpu->irq_summary);
	}

	if ((intr_info & INTR_INFO_INTR_TYPE_MASK) == 0x200) { /* nmi */
		asm ("int $2");
		return 1;
	}

	if (is_no_device(intr_info)) {
		vmx_fpu_activate(vcpu);
		return 1;
	}

	error_code = 0;
	rip = vmcs_readl(GUEST_RIP);
	if (intr_info & INTR_INFO_DELIEVER_CODE_MASK)
		error_code = vmcs_read32(VM_EXIT_INTR_ERROR_CODE);
	if (is_page_fault(intr_info)) {
		cr2 = vmcs_readl(EXIT_QUALIFICATION);

		mutex_lock(&vcpu->kvm->lock);
		r = kvm_mmu_page_fault(vcpu, cr2, error_code);
		if (r < 0) {
			mutex_unlock(&vcpu->kvm->lock);
			return r;
		}
		if (!r) {
			mutex_unlock(&vcpu->kvm->lock);
			return 1;
		}

		er = emulate_instruction(vcpu, kvm_run, cr2, error_code);
		mutex_unlock(&vcpu->kvm->lock);

		switch (er) {
		case EMULATE_DONE:
			return 1;
		case EMULATE_DO_MMIO:
			++vcpu->stat.mmio_exits;
			return 0;
		 case EMULATE_FAIL:
			vcpu_printf(vcpu, "%s: emulate fail\n", __FUNCTION__);
			break;
		default:
			BUG();
		}
	}

	if (vcpu->rmode.active &&
	    handle_rmode_exception(vcpu, intr_info & INTR_INFO_VECTOR_MASK,
								error_code)) {
		if (vcpu->halt_request) {
			vcpu->halt_request = 0;
			return kvm_emulate_halt(vcpu);
		}
		return 1;
	}

	if ((intr_info & (INTR_INFO_INTR_TYPE_MASK | INTR_INFO_VECTOR_MASK)) == (INTR_TYPE_EXCEPTION | 1)) {
		kvm_run->exit_reason = KVM_EXIT_DEBUG;
		return 0;
	}
	kvm_run->exit_reason = KVM_EXIT_EXCEPTION;
	kvm_run->ex.exception = intr_info & INTR_INFO_VECTOR_MASK;
	kvm_run->ex.error_code = error_code;
	return 0;
}

static int handle_external_interrupt(struct kvm_vcpu *vcpu,
				     struct kvm_run *kvm_run)
{
	++vcpu->stat.irq_exits;
	return 1;
}

static int handle_triple_fault(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	kvm_run->exit_reason = KVM_EXIT_SHUTDOWN;
	return 0;
}

static int handle_io(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u64 exit_qualification;
	int size, down, in, string, rep;
	unsigned port;

	++vcpu->stat.io_exits;
	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	string = (exit_qualification & 16) != 0;

	if (string) {
		if (emulate_instruction(vcpu, kvm_run, 0, 0) == EMULATE_DO_MMIO)
			return 0;
		return 1;
	}

	size = (exit_qualification & 7) + 1;
	in = (exit_qualification & 8) != 0;
	down = (vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_DF) != 0;
	rep = (exit_qualification & 32) != 0;
	port = exit_qualification >> 16;

	return kvm_emulate_pio(vcpu, kvm_run, in, size, port);
}

static void
vmx_patch_hypercall(struct kvm_vcpu *vcpu, unsigned char *hypercall)
{
	/*
	 * Patch in the VMCALL instruction:
	 */
	hypercall[0] = 0x0f;
	hypercall[1] = 0x01;
	hypercall[2] = 0xc1;
	hypercall[3] = 0xc3;
}

static int handle_cr(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u64 exit_qualification;
	int cr;
	int reg;

	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	cr = exit_qualification & 15;
	reg = (exit_qualification >> 8) & 15;
	switch ((exit_qualification >> 4) & 3) {
	case 0: /* mov to cr */
		switch (cr) {
		case 0:
			vcpu_load_rsp_rip(vcpu);
			set_cr0(vcpu, vcpu->regs[reg]);
			skip_emulated_instruction(vcpu);
			return 1;
		case 3:
			vcpu_load_rsp_rip(vcpu);
			set_cr3(vcpu, vcpu->regs[reg]);
			skip_emulated_instruction(vcpu);
			return 1;
		case 4:
			vcpu_load_rsp_rip(vcpu);
			set_cr4(vcpu, vcpu->regs[reg]);
			skip_emulated_instruction(vcpu);
			return 1;
		case 8:
			vcpu_load_rsp_rip(vcpu);
			set_cr8(vcpu, vcpu->regs[reg]);
			skip_emulated_instruction(vcpu);
			kvm_run->exit_reason = KVM_EXIT_SET_TPR;
			return 0;
		};
		break;
	case 2: /* clts */
		vcpu_load_rsp_rip(vcpu);
		vmx_fpu_deactivate(vcpu);
		vcpu->cr0 &= ~X86_CR0_TS;
		vmcs_writel(CR0_READ_SHADOW, vcpu->cr0);
		vmx_fpu_activate(vcpu);
		skip_emulated_instruction(vcpu);
		return 1;
	case 1: /*mov from cr*/
		switch (cr) {
		case 3:
			vcpu_load_rsp_rip(vcpu);
			vcpu->regs[reg] = vcpu->cr3;
			vcpu_put_rsp_rip(vcpu);
			skip_emulated_instruction(vcpu);
			return 1;
		case 8:
			vcpu_load_rsp_rip(vcpu);
			vcpu->regs[reg] = get_cr8(vcpu);
			vcpu_put_rsp_rip(vcpu);
			skip_emulated_instruction(vcpu);
			return 1;
		}
		break;
	case 3: /* lmsw */
		lmsw(vcpu, (exit_qualification >> LMSW_SOURCE_DATA_SHIFT) & 0x0f);

		skip_emulated_instruction(vcpu);
		return 1;
	default:
		break;
	}
	kvm_run->exit_reason = 0;
	pr_unimpl(vcpu, "unhandled control register: op %d cr %d\n",
	       (int)(exit_qualification >> 4) & 3, cr);
	return 0;
}

static int handle_dr(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u64 exit_qualification;
	unsigned long val;
	int dr, reg;

	/*
	 * FIXME: this code assumes the host is debugging the guest.
	 *        need to deal with guest debugging itself too.
	 */
	exit_qualification = vmcs_read64(EXIT_QUALIFICATION);
	dr = exit_qualification & 7;
	reg = (exit_qualification >> 8) & 15;
	vcpu_load_rsp_rip(vcpu);
	if (exit_qualification & 16) {
		/* mov from dr */
		switch (dr) {
		case 6:
			val = 0xffff0ff0;
			break;
		case 7:
			val = 0x400;
			break;
		default:
			val = 0;
		}
		vcpu->regs[reg] = val;
	} else {
		/* mov to dr */
	}
	vcpu_put_rsp_rip(vcpu);
	skip_emulated_instruction(vcpu);
	return 1;
}

static int handle_cpuid(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	kvm_emulate_cpuid(vcpu);
	return 1;
}

static int handle_rdmsr(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u32 ecx = vcpu->regs[VCPU_REGS_RCX];
	u64 data;

	if (vmx_get_msr(vcpu, ecx, &data)) {
		vmx_inject_gp(vcpu, 0);
		return 1;
	}

	/* FIXME: handling of bits 32:63 of rax, rdx */
	vcpu->regs[VCPU_REGS_RAX] = data & -1u;
	vcpu->regs[VCPU_REGS_RDX] = (data >> 32) & -1u;
	skip_emulated_instruction(vcpu);
	return 1;
}

static int handle_wrmsr(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	u32 ecx = vcpu->regs[VCPU_REGS_RCX];
	u64 data = (vcpu->regs[VCPU_REGS_RAX] & -1u)
		| ((u64)(vcpu->regs[VCPU_REGS_RDX] & -1u) << 32);

	if (vmx_set_msr(vcpu, ecx, data) != 0) {
		vmx_inject_gp(vcpu, 0);
		return 1;
	}

	skip_emulated_instruction(vcpu);
	return 1;
}

static int handle_tpr_below_threshold(struct kvm_vcpu *vcpu,
				      struct kvm_run *kvm_run)
{
	return 1;
}

static void post_kvm_run_save(struct kvm_vcpu *vcpu,
			      struct kvm_run *kvm_run)
{
	kvm_run->if_flag = (vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF) != 0;
	kvm_run->cr8 = get_cr8(vcpu);
	kvm_run->apic_base = kvm_get_apic_base(vcpu);
	if (irqchip_in_kernel(vcpu->kvm))
		kvm_run->ready_for_interrupt_injection = 1;
	else
		kvm_run->ready_for_interrupt_injection =
					(vcpu->interrupt_window_open &&
					 vcpu->irq_summary == 0);
}

static int handle_interrupt_window(struct kvm_vcpu *vcpu,
				   struct kvm_run *kvm_run)
{
	u32 cpu_based_vm_exec_control;

	/* clear pending irq */
	cpu_based_vm_exec_control = vmcs_read32(CPU_BASED_VM_EXEC_CONTROL);
	cpu_based_vm_exec_control &= ~CPU_BASED_VIRTUAL_INTR_PENDING;
	vmcs_write32(CPU_BASED_VM_EXEC_CONTROL, cpu_based_vm_exec_control);
	/*
	 * If the user space waits to inject interrupts, exit as soon as
	 * possible
	 */
	if (kvm_run->request_interrupt_window &&
	    !vcpu->irq_summary) {
		kvm_run->exit_reason = KVM_EXIT_IRQ_WINDOW_OPEN;
		++vcpu->stat.irq_window_exits;
		return 0;
	}
	return 1;
}

static int handle_halt(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	skip_emulated_instruction(vcpu);
	return kvm_emulate_halt(vcpu);
}

static int handle_vmcall(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	skip_emulated_instruction(vcpu);
	return kvm_hypercall(vcpu, kvm_run);
}

/*
 * The exit handlers return 1 if the exit was handled fully and guest execution
 * may resume.  Otherwise they set the kvm_run parameter to indicate what needs
 * to be done to userspace and return 0.
 */
static int (*kvm_vmx_exit_handlers[])(struct kvm_vcpu *vcpu,
				      struct kvm_run *kvm_run) = {
	[EXIT_REASON_EXCEPTION_NMI]           = handle_exception,
	[EXIT_REASON_EXTERNAL_INTERRUPT]      = handle_external_interrupt,
	[EXIT_REASON_TRIPLE_FAULT]            = handle_triple_fault,
	[EXIT_REASON_IO_INSTRUCTION]          = handle_io,
	[EXIT_REASON_CR_ACCESS]               = handle_cr,
	[EXIT_REASON_DR_ACCESS]               = handle_dr,
	[EXIT_REASON_CPUID]                   = handle_cpuid,
	[EXIT_REASON_MSR_READ]                = handle_rdmsr,
	[EXIT_REASON_MSR_WRITE]               = handle_wrmsr,
	[EXIT_REASON_PENDING_INTERRUPT]       = handle_interrupt_window,
	[EXIT_REASON_HLT]                     = handle_halt,
	[EXIT_REASON_VMCALL]                  = handle_vmcall,
	[EXIT_REASON_TPR_BELOW_THRESHOLD]     = handle_tpr_below_threshold
};

static const int kvm_vmx_max_exit_handlers =
	ARRAY_SIZE(kvm_vmx_exit_handlers);

/*
 * The guest has exited.  See if we can fix it or if we need userspace
 * assistance.
 */
static int kvm_handle_exit(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	u32 vectoring_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	u32 exit_reason = vmcs_read32(VM_EXIT_REASON);

	if ( (vectoring_info & VECTORING_INFO_VALID_MASK) &&
				exit_reason != EXIT_REASON_EXCEPTION_NMI )
		printk(KERN_WARNING "%s: unexpected, valid vectoring info and "
		       "exit reason is 0x%x\n", __FUNCTION__, exit_reason);
	if (exit_reason < kvm_vmx_max_exit_handlers
	    && kvm_vmx_exit_handlers[exit_reason])
		return kvm_vmx_exit_handlers[exit_reason](vcpu, kvm_run);
	else {
		kvm_run->exit_reason = KVM_EXIT_UNKNOWN;
		kvm_run->hw.hardware_exit_reason = exit_reason;
	}
	return 0;
}

/*
 * Check if userspace requested an interrupt window, and that the
 * interrupt window is open.
 *
 * No need to exit to userspace if we already have an interrupt queued.
 */
static int dm_request_for_irq_injection(struct kvm_vcpu *vcpu,
					  struct kvm_run *kvm_run)
{
	return (!vcpu->irq_summary &&
		kvm_run->request_interrupt_window &&
		vcpu->interrupt_window_open &&
		(vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF));
}

static void vmx_flush_tlb(struct kvm_vcpu *vcpu)
{
}

static void update_tpr_threshold(struct kvm_vcpu *vcpu)
{
	int max_irr, tpr;

	if (!vm_need_tpr_shadow(vcpu->kvm))
		return;

	if (!kvm_lapic_enabled(vcpu) ||
	    ((max_irr = kvm_lapic_find_highest_irr(vcpu)) == -1)) {
		vmcs_write32(TPR_THRESHOLD, 0);
		return;
	}

	tpr = (kvm_lapic_get_cr8(vcpu) & 0x0f) << 4;
	vmcs_write32(TPR_THRESHOLD, (max_irr > tpr) ? tpr >> 4 : max_irr >> 4);
}

static void enable_irq_window(struct kvm_vcpu *vcpu)
{
	u32 cpu_based_vm_exec_control;

	cpu_based_vm_exec_control = vmcs_read32(CPU_BASED_VM_EXEC_CONTROL);
	cpu_based_vm_exec_control |= CPU_BASED_VIRTUAL_INTR_PENDING;
	vmcs_write32(CPU_BASED_VM_EXEC_CONTROL, cpu_based_vm_exec_control);
}

static void vmx_intr_assist(struct kvm_vcpu *vcpu)
{
	u32 idtv_info_field, intr_info_field;
	int has_ext_irq, interrupt_window_open;
	int vector;

	kvm_inject_pending_timer_irqs(vcpu);
	update_tpr_threshold(vcpu);

	has_ext_irq = kvm_cpu_has_interrupt(vcpu);
	intr_info_field = vmcs_read32(VM_ENTRY_INTR_INFO_FIELD);
	idtv_info_field = vmcs_read32(IDT_VECTORING_INFO_FIELD);
	if (intr_info_field & INTR_INFO_VALID_MASK) {
		if (idtv_info_field & INTR_INFO_VALID_MASK) {
			/* TODO: fault when IDT_Vectoring */
			printk(KERN_ERR "Fault when IDT_Vectoring\n");
		}
		if (has_ext_irq)
			enable_irq_window(vcpu);
		return;
	}
	if (unlikely(idtv_info_field & INTR_INFO_VALID_MASK)) {
		vmcs_write32(VM_ENTRY_INTR_INFO_FIELD, idtv_info_field);
		vmcs_write32(VM_ENTRY_INSTRUCTION_LEN,
				vmcs_read32(VM_EXIT_INSTRUCTION_LEN));

		if (unlikely(idtv_info_field & INTR_INFO_DELIEVER_CODE_MASK))
			vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE,
				vmcs_read32(IDT_VECTORING_ERROR_CODE));
		if (unlikely(has_ext_irq))
			enable_irq_window(vcpu);
		return;
	}
	if (!has_ext_irq)
		return;
	interrupt_window_open =
		((vmcs_readl(GUEST_RFLAGS) & X86_EFLAGS_IF) &&
		 (vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) & 3) == 0);
	if (interrupt_window_open) {
		vector = kvm_cpu_get_interrupt(vcpu);
		vmx_inject_irq(vcpu, vector);
		kvm_timer_intr_post(vcpu, vector);
	} else
		enable_irq_window(vcpu);
}

static int vmx_vcpu_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u8 fail;
	int r;

	if (unlikely(vcpu->mp_state == VCPU_MP_STATE_SIPI_RECEIVED)) {
		printk("vcpu %d received sipi with vector # %x\n",
		       vcpu->vcpu_id, vcpu->sipi_vector);
		kvm_lapic_reset(vcpu);
		vmx_vcpu_setup(vmx);
		vcpu->mp_state = VCPU_MP_STATE_RUNNABLE;
	}

preempted:
	if (vcpu->guest_debug.enabled)
		kvm_guest_debug_pre(vcpu);

again:
	r = kvm_mmu_reload(vcpu);
	if (unlikely(r))
		goto out;

	preempt_disable();

	vmx_save_host_state(vmx);
	kvm_load_guest_fpu(vcpu);

	/*
	 * Loading guest fpu may have cleared host cr0.ts
	 */
	vmcs_writel(HOST_CR0, read_cr0());

	local_irq_disable();

	if (signal_pending(current)) {
		local_irq_enable();
		preempt_enable();
		r = -EINTR;
		kvm_run->exit_reason = KVM_EXIT_INTR;
		++vcpu->stat.signal_exits;
		goto out;
	}

	if (irqchip_in_kernel(vcpu->kvm))
		vmx_intr_assist(vcpu);
	else if (!vcpu->mmio_read_completed)
		do_interrupt_requests(vcpu, kvm_run);

	vcpu->guest_mode = 1;
	if (vcpu->requests)
		if (test_and_clear_bit(KVM_TLB_FLUSH, &vcpu->requests))
		    vmx_flush_tlb(vcpu);

	asm (
		/* Store host registers */
#ifdef CONFIG_X86_64
		"push %%rax; push %%rbx; push %%rdx;"
		"push %%rsi; push %%rdi; push %%rbp;"
		"push %%r8;  push %%r9;  push %%r10; push %%r11;"
		"push %%r12; push %%r13; push %%r14; push %%r15;"
		"push %%rcx \n\t"
		ASM_VMX_VMWRITE_RSP_RDX "\n\t"
#else
		"pusha; push %%ecx \n\t"
		ASM_VMX_VMWRITE_RSP_RDX "\n\t"
#endif
		/* Check if vmlaunch of vmresume is needed */
		"cmp $0, %1 \n\t"
		/* Load guest registers.  Don't clobber flags. */
#ifdef CONFIG_X86_64
		"mov %c[cr2](%3), %%rax \n\t"
		"mov %%rax, %%cr2 \n\t"
		"mov %c[rax](%3), %%rax \n\t"
		"mov %c[rbx](%3), %%rbx \n\t"
		"mov %c[rdx](%3), %%rdx \n\t"
		"mov %c[rsi](%3), %%rsi \n\t"
		"mov %c[rdi](%3), %%rdi \n\t"
		"mov %c[rbp](%3), %%rbp \n\t"
		"mov %c[r8](%3),  %%r8  \n\t"
		"mov %c[r9](%3),  %%r9  \n\t"
		"mov %c[r10](%3), %%r10 \n\t"
		"mov %c[r11](%3), %%r11 \n\t"
		"mov %c[r12](%3), %%r12 \n\t"
		"mov %c[r13](%3), %%r13 \n\t"
		"mov %c[r14](%3), %%r14 \n\t"
		"mov %c[r15](%3), %%r15 \n\t"
		"mov %c[rcx](%3), %%rcx \n\t" /* kills %3 (rcx) */
#else
		"mov %c[cr2](%3), %%eax \n\t"
		"mov %%eax,   %%cr2 \n\t"
		"mov %c[rax](%3), %%eax \n\t"
		"mov %c[rbx](%3), %%ebx \n\t"
		"mov %c[rdx](%3), %%edx \n\t"
		"mov %c[rsi](%3), %%esi \n\t"
		"mov %c[rdi](%3), %%edi \n\t"
		"mov %c[rbp](%3), %%ebp \n\t"
		"mov %c[rcx](%3), %%ecx \n\t" /* kills %3 (ecx) */
#endif
		/* Enter guest mode */
		"jne .Llaunched \n\t"
		ASM_VMX_VMLAUNCH "\n\t"
		"jmp .Lkvm_vmx_return \n\t"
		".Llaunched: " ASM_VMX_VMRESUME "\n\t"
		".Lkvm_vmx_return: "
		/* Save guest registers, load host registers, keep flags */
#ifdef CONFIG_X86_64
		"xchg %3,     (%%rsp) \n\t"
		"mov %%rax, %c[rax](%3) \n\t"
		"mov %%rbx, %c[rbx](%3) \n\t"
		"pushq (%%rsp); popq %c[rcx](%3) \n\t"
		"mov %%rdx, %c[rdx](%3) \n\t"
		"mov %%rsi, %c[rsi](%3) \n\t"
		"mov %%rdi, %c[rdi](%3) \n\t"
		"mov %%rbp, %c[rbp](%3) \n\t"
		"mov %%r8,  %c[r8](%3) \n\t"
		"mov %%r9,  %c[r9](%3) \n\t"
		"mov %%r10, %c[r10](%3) \n\t"
		"mov %%r11, %c[r11](%3) \n\t"
		"mov %%r12, %c[r12](%3) \n\t"
		"mov %%r13, %c[r13](%3) \n\t"
		"mov %%r14, %c[r14](%3) \n\t"
		"mov %%r15, %c[r15](%3) \n\t"
		"mov %%cr2, %%rax   \n\t"
		"mov %%rax, %c[cr2](%3) \n\t"
		"mov (%%rsp), %3 \n\t"

		"pop  %%rcx; pop  %%r15; pop  %%r14; pop  %%r13; pop  %%r12;"
		"pop  %%r11; pop  %%r10; pop  %%r9;  pop  %%r8;"
		"pop  %%rbp; pop  %%rdi; pop  %%rsi;"
		"pop  %%rdx; pop  %%rbx; pop  %%rax \n\t"
#else
		"xchg %3, (%%esp) \n\t"
		"mov %%eax, %c[rax](%3) \n\t"
		"mov %%ebx, %c[rbx](%3) \n\t"
		"pushl (%%esp); popl %c[rcx](%3) \n\t"
		"mov %%edx, %c[rdx](%3) \n\t"
		"mov %%esi, %c[rsi](%3) \n\t"
		"mov %%edi, %c[rdi](%3) \n\t"
		"mov %%ebp, %c[rbp](%3) \n\t"
		"mov %%cr2, %%eax  \n\t"
		"mov %%eax, %c[cr2](%3) \n\t"
		"mov (%%esp), %3 \n\t"

		"pop %%ecx; popa \n\t"
#endif
		"setbe %0 \n\t"
	      : "=q" (fail)
	      : "r"(vmx->launched), "d"((unsigned long)HOST_RSP),
		"c"(vcpu),
		[rax]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RAX])),
		[rbx]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RBX])),
		[rcx]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RCX])),
		[rdx]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RDX])),
		[rsi]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RSI])),
		[rdi]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RDI])),
		[rbp]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_RBP])),
#ifdef CONFIG_X86_64
		[r8 ]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R8 ])),
		[r9 ]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R9 ])),
		[r10]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R10])),
		[r11]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R11])),
		[r12]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R12])),
		[r13]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R13])),
		[r14]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R14])),
		[r15]"i"(offsetof(struct kvm_vcpu, regs[VCPU_REGS_R15])),
#endif
		[cr2]"i"(offsetof(struct kvm_vcpu, cr2))
	      : "cc", "memory" );

	vcpu->guest_mode = 0;
	local_irq_enable();

	++vcpu->stat.exits;

	vcpu->interrupt_window_open = (vmcs_read32(GUEST_INTERRUPTIBILITY_INFO) & 3) == 0;

	asm ("mov %0, %%ds; mov %0, %%es" : : "r"(__USER_DS));
	vmx->launched = 1;

	preempt_enable();

	if (unlikely(fail)) {
		kvm_run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		kvm_run->fail_entry.hardware_entry_failure_reason
			= vmcs_read32(VM_INSTRUCTION_ERROR);
		r = 0;
		goto out;
	}
	/*
	 * Profile KVM exit RIPs:
	 */
	if (unlikely(prof_on == KVM_PROFILING))
		profile_hit(KVM_PROFILING, (void *)vmcs_readl(GUEST_RIP));

	r = kvm_handle_exit(kvm_run, vcpu);
	if (r > 0) {
		if (dm_request_for_irq_injection(vcpu, kvm_run)) {
			r = -EINTR;
			kvm_run->exit_reason = KVM_EXIT_INTR;
			++vcpu->stat.request_irq_exits;
			goto out;
		}
		if (!need_resched()) {
			++vcpu->stat.light_exits;
			goto again;
		}
	}

out:
	if (r > 0) {
		kvm_resched(vcpu);
		goto preempted;
	}

	post_kvm_run_save(vcpu, kvm_run);
	return r;
}

static void vmx_inject_page_fault(struct kvm_vcpu *vcpu,
				  unsigned long addr,
				  u32 err_code)
{
	u32 vect_info = vmcs_read32(IDT_VECTORING_INFO_FIELD);

	++vcpu->stat.pf_guest;

	if (is_page_fault(vect_info)) {
		printk(KERN_DEBUG "inject_page_fault: "
		       "double fault 0x%lx @ 0x%lx\n",
		       addr, vmcs_readl(GUEST_RIP));
		vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, 0);
		vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
			     DF_VECTOR |
			     INTR_TYPE_EXCEPTION |
			     INTR_INFO_DELIEVER_CODE_MASK |
			     INTR_INFO_VALID_MASK);
		return;
	}
	vcpu->cr2 = addr;
	vmcs_write32(VM_ENTRY_EXCEPTION_ERROR_CODE, err_code);
	vmcs_write32(VM_ENTRY_INTR_INFO_FIELD,
		     PF_VECTOR |
		     INTR_TYPE_EXCEPTION |
		     INTR_INFO_DELIEVER_CODE_MASK |
		     INTR_INFO_VALID_MASK);

}

static void vmx_free_vmcs(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	if (vmx->vmcs) {
		on_each_cpu(__vcpu_clear, vmx, 0, 1);
		free_vmcs(vmx->vmcs);
		vmx->vmcs = NULL;
	}
}

static void vmx_free_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	vmx_free_vmcs(vcpu);
	kfree(vmx->host_msrs);
	kfree(vmx->guest_msrs);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vmx);
}

static struct kvm_vcpu *vmx_create_vcpu(struct kvm *kvm, unsigned int id)
{
	int err;
	struct vcpu_vmx *vmx = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	int cpu;

	if (!vmx)
		return ERR_PTR(-ENOMEM);

	err = kvm_vcpu_init(&vmx->vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	if (irqchip_in_kernel(kvm)) {
		err = kvm_create_lapic(&vmx->vcpu);
		if (err < 0)
			goto free_vcpu;
	}

	vmx->guest_msrs = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!vmx->guest_msrs) {
		err = -ENOMEM;
		goto uninit_vcpu;
	}

	vmx->host_msrs = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!vmx->host_msrs)
		goto free_guest_msrs;

	vmx->vmcs = alloc_vmcs();
	if (!vmx->vmcs)
		goto free_msrs;

	vmcs_clear(vmx->vmcs);

	cpu = get_cpu();
	vmx_vcpu_load(&vmx->vcpu, cpu);
	err = vmx_vcpu_setup(vmx);
	vmx_vcpu_put(&vmx->vcpu);
	put_cpu();
	if (err)
		goto free_vmcs;

	return &vmx->vcpu;

free_vmcs:
	free_vmcs(vmx->vmcs);
free_msrs:
	kfree(vmx->host_msrs);
free_guest_msrs:
	kfree(vmx->guest_msrs);
uninit_vcpu:
	kvm_vcpu_uninit(&vmx->vcpu);
free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vmx);
	return ERR_PTR(err);
}

static void __init vmx_check_processor_compat(void *rtn)
{
	struct vmcs_config vmcs_conf;

	*(int *)rtn = 0;
	if (setup_vmcs_config(&vmcs_conf) < 0)
		*(int *)rtn = -EIO;
	if (memcmp(&vmcs_config, &vmcs_conf, sizeof(struct vmcs_config)) != 0) {
		printk(KERN_ERR "kvm: CPU %d feature inconsistency!\n",
				smp_processor_id());
		*(int *)rtn = -EIO;
	}
}

static struct kvm_arch_ops vmx_arch_ops = {
	.cpu_has_kvm_support = cpu_has_kvm_support,
	.disabled_by_bios = vmx_disabled_by_bios,
	.hardware_setup = hardware_setup,
	.hardware_unsetup = hardware_unsetup,
	.check_processor_compatibility = vmx_check_processor_compat,
	.hardware_enable = hardware_enable,
	.hardware_disable = hardware_disable,

	.vcpu_create = vmx_create_vcpu,
	.vcpu_free = vmx_free_vcpu,

	.vcpu_load = vmx_vcpu_load,
	.vcpu_put = vmx_vcpu_put,
	.vcpu_decache = vmx_vcpu_decache,

	.set_guest_debug = set_guest_debug,
	.get_msr = vmx_get_msr,
	.set_msr = vmx_set_msr,
	.get_segment_base = vmx_get_segment_base,
	.get_segment = vmx_get_segment,
	.set_segment = vmx_set_segment,
	.get_cs_db_l_bits = vmx_get_cs_db_l_bits,
	.decache_cr4_guest_bits = vmx_decache_cr4_guest_bits,
	.set_cr0 = vmx_set_cr0,
	.set_cr3 = vmx_set_cr3,
	.set_cr4 = vmx_set_cr4,
#ifdef CONFIG_X86_64
	.set_efer = vmx_set_efer,
#endif
	.get_idt = vmx_get_idt,
	.set_idt = vmx_set_idt,
	.get_gdt = vmx_get_gdt,
	.set_gdt = vmx_set_gdt,
	.cache_regs = vcpu_load_rsp_rip,
	.decache_regs = vcpu_put_rsp_rip,
	.get_rflags = vmx_get_rflags,
	.set_rflags = vmx_set_rflags,

	.tlb_flush = vmx_flush_tlb,
	.inject_page_fault = vmx_inject_page_fault,

	.inject_gp = vmx_inject_gp,

	.run = vmx_vcpu_run,
	.skip_emulated_instruction = skip_emulated_instruction,
	.patch_hypercall = vmx_patch_hypercall,
	.get_irq = vmx_get_irq,
	.set_irq = vmx_inject_irq,
};

static int __init vmx_init(void)
{
	void *iova;
	int r;

	vmx_io_bitmap_a = alloc_page(GFP_KERNEL | __GFP_HIGHMEM);
	if (!vmx_io_bitmap_a)
		return -ENOMEM;

	vmx_io_bitmap_b = alloc_page(GFP_KERNEL | __GFP_HIGHMEM);
	if (!vmx_io_bitmap_b) {
		r = -ENOMEM;
		goto out;
	}

	/*
	 * Allow direct access to the PC debug port (it is often used for I/O
	 * delays, but the vmexits simply slow things down).
	 */
	iova = kmap(vmx_io_bitmap_a);
	memset(iova, 0xff, PAGE_SIZE);
	clear_bit(0x80, iova);
	kunmap(vmx_io_bitmap_a);

	iova = kmap(vmx_io_bitmap_b);
	memset(iova, 0xff, PAGE_SIZE);
	kunmap(vmx_io_bitmap_b);

	r = kvm_init_arch(&vmx_arch_ops, sizeof(struct vcpu_vmx), THIS_MODULE);
	if (r)
		goto out1;

	return 0;

out1:
	__free_page(vmx_io_bitmap_b);
out:
	__free_page(vmx_io_bitmap_a);
	return r;
}

static void __exit vmx_exit(void)
{
	__free_page(vmx_io_bitmap_b);
	__free_page(vmx_io_bitmap_a);

	kvm_exit_arch();
}

module_init(vmx_init)
module_exit(vmx_exit)
