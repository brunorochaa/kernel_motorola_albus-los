/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_64_H__
#define __ASM_KVM_BOOK3S_64_H__

#ifdef CONFIG_KVM_BOOK3S_PR
static inline struct kvmppc_book3s_shadow_vcpu *svcpu_get(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	return &get_paca()->shadow_vcpu;
}

static inline void svcpu_put(struct kvmppc_book3s_shadow_vcpu *svcpu)
{
	preempt_enable();
}
#endif

#define SPAPR_TCE_SHIFT		12

#ifdef CONFIG_KVM_BOOK3S_64_HV
/* For now use fixed-size 16MB page table */
#define HPT_ORDER	24
#define HPT_NPTEG	(1ul << (HPT_ORDER - 7))	/* 128B per pteg */
#define HPT_NPTE	(HPT_NPTEG << 3)		/* 8 PTEs per PTEG */
#define HPT_HASH_MASK	(HPT_NPTEG - 1)
#endif

/*
 * We use a lock bit in HPTE dword 0 to synchronize updates and
 * accesses to each HPTE, and another bit to indicate non-present
 * HPTEs.
 */
#define HPTE_V_HVLOCK	0x40UL

static inline long try_lock_hpte(unsigned long *hpte, unsigned long bits)
{
	unsigned long tmp, old;

	asm volatile("	ldarx	%0,0,%2\n"
		     "	and.	%1,%0,%3\n"
		     "	bne	2f\n"
		     "	ori	%0,%0,%4\n"
		     "  stdcx.	%0,0,%2\n"
		     "	beq+	2f\n"
		     "	li	%1,%3\n"
		     "2:	isync"
		     : "=&r" (tmp), "=&r" (old)
		     : "r" (hpte), "r" (bits), "i" (HPTE_V_HVLOCK)
		     : "cc", "memory");
	return old == 0;
}

static inline unsigned long compute_tlbie_rb(unsigned long v, unsigned long r,
					     unsigned long pte_index)
{
	unsigned long rb, va_low;

	rb = (v & ~0x7fUL) << 16;		/* AVA field */
	va_low = pte_index >> 3;
	if (v & HPTE_V_SECONDARY)
		va_low = ~va_low;
	/* xor vsid from AVA */
	if (!(v & HPTE_V_1TB_SEG))
		va_low ^= v >> 12;
	else
		va_low ^= v >> 24;
	va_low &= 0x7ff;
	if (v & HPTE_V_LARGE) {
		rb |= 1;			/* L field */
		if (cpu_has_feature(CPU_FTR_ARCH_206) &&
		    (r & 0xff000)) {
			/* non-16MB large page, must be 64k */
			/* (masks depend on page size) */
			rb |= 0x1000;		/* page encoding in LP field */
			rb |= (va_low & 0x7f) << 16; /* 7b of VA in AVA/LP field */
			rb |= (va_low & 0xfe);	/* AVAL field (P7 doesn't seem to care) */
		}
	} else {
		/* 4kB page */
		rb |= (va_low & 0x7ff) << 12;	/* remaining 11b of VA */
	}
	rb |= (v >> 54) & 0x300;		/* B field */
	return rb;
}

#endif /* __ASM_KVM_BOOK3S_64_H__ */
