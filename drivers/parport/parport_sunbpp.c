/* parport_sunbpp.c: Parallel-port routines for SBUS
 * 
 * Author: Derrick J. Brashear <shadow@dementia.org>
 *
 * based on work by:
 *          Phil Blundell <philb@gnu.org>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *          David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Grant Guenther <grant@torque.net>
 *          Eddie C. Dost <ecd@skynet.be>
 *          Stephen Williams (steve@icarus.com)
 *          Gus Baldauf (gbaldauf@ix.netcom.com)
 *          Peter Zaitcev
 *          Tom Dyas
 *
 * Updated to new SBUS device framework: David S. Miller <davem@davemloft.net>
 * 
 */

#include <linux/string.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <linux/parport.h>

#include <asm/ptrace.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/oplib.h>           /* OpenProm Library */
#include <asm/sbus.h>
#include <asm/dma.h>             /* BPP uses LSI 64854 for DMA */
#include <asm/irq.h>
#include <asm/sunbpp.h>

#undef __SUNBPP_DEBUG
#ifdef __SUNBPP_DEBUG
#define dprintk(x) printk x
#else
#define dprintk(x)
#endif

static irqreturn_t parport_sunbpp_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
	return IRQ_HANDLED;
}

static void parport_sunbpp_disable_irq(struct parport *p)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;
	u32 tmp;

	tmp = sbus_readl(&regs->p_csr);
	tmp &= ~DMA_INT_ENAB;
	sbus_writel(tmp, &regs->p_csr);
}

static void parport_sunbpp_enable_irq(struct parport *p)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;
	u32 tmp;

	tmp = sbus_readl(&regs->p_csr);
	tmp |= DMA_INT_ENAB;
	sbus_writel(tmp, &regs->p_csr);
}

static void parport_sunbpp_write_data(struct parport *p, unsigned char d)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;

	sbus_writeb(d, &regs->p_dr);
	dprintk((KERN_DEBUG "wrote 0x%x\n", d));
}

static unsigned char parport_sunbpp_read_data(struct parport *p)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;

	return sbus_readb(&regs->p_dr);
}

#if 0
static void control_pc_to_sunbpp(struct parport *p, unsigned char status)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;
	unsigned char value_tcr = sbus_readb(&regs->p_tcr);
	unsigned char value_or = sbus_readb(&regs->p_or);

	if (status & PARPORT_CONTROL_STROBE) 
		value_tcr |= P_TCR_DS;
	if (status & PARPORT_CONTROL_AUTOFD) 
		value_or |= P_OR_AFXN;
	if (status & PARPORT_CONTROL_INIT) 
		value_or |= P_OR_INIT;
	if (status & PARPORT_CONTROL_SELECT) 
		value_or |= P_OR_SLCT_IN;

	sbus_writeb(value_or, &regs->p_or);
	sbus_writeb(value_tcr, &regs->p_tcr);
}
#endif

static unsigned char status_sunbpp_to_pc(struct parport *p)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;
	unsigned char bits = 0;
	unsigned char value_tcr = sbus_readb(&regs->p_tcr);
	unsigned char value_ir = sbus_readb(&regs->p_ir);

	if (!(value_ir & P_IR_ERR))
		bits |= PARPORT_STATUS_ERROR;
	if (!(value_ir & P_IR_SLCT))
		bits |= PARPORT_STATUS_SELECT;
	if (!(value_ir & P_IR_PE))
		bits |= PARPORT_STATUS_PAPEROUT;
	if (value_tcr & P_TCR_ACK)
		bits |= PARPORT_STATUS_ACK;
	if (!(value_tcr & P_TCR_BUSY))
		bits |= PARPORT_STATUS_BUSY;

	dprintk((KERN_DEBUG "tcr 0x%x ir 0x%x\n", regs->p_tcr, regs->p_ir));
	dprintk((KERN_DEBUG "read status 0x%x\n", bits));
	return bits;
}

static unsigned char control_sunbpp_to_pc(struct parport *p)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;
	unsigned char bits = 0;
	unsigned char value_tcr = sbus_readb(&regs->p_tcr);
	unsigned char value_or = sbus_readb(&regs->p_or);

	if (!(value_tcr & P_TCR_DS))
		bits |= PARPORT_CONTROL_STROBE;
	if (!(value_or & P_OR_AFXN))
		bits |= PARPORT_CONTROL_AUTOFD;
	if (!(value_or & P_OR_INIT))
		bits |= PARPORT_CONTROL_INIT;
	if (value_or & P_OR_SLCT_IN)
		bits |= PARPORT_CONTROL_SELECT;

	dprintk((KERN_DEBUG "tcr 0x%x or 0x%x\n", regs->p_tcr, regs->p_or));
	dprintk((KERN_DEBUG "read control 0x%x\n", bits));
	return bits;
}

static unsigned char parport_sunbpp_read_control(struct parport *p)
{
	return control_sunbpp_to_pc(p);
}

static unsigned char parport_sunbpp_frob_control(struct parport *p,
						 unsigned char mask,
						 unsigned char val)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;
	unsigned char value_tcr = sbus_readb(&regs->p_tcr);
	unsigned char value_or = sbus_readb(&regs->p_or);

	dprintk((KERN_DEBUG "frob1: tcr 0x%x or 0x%x\n", regs->p_tcr, regs->p_or));
	if (mask & PARPORT_CONTROL_STROBE) {
		if (val & PARPORT_CONTROL_STROBE) {
			value_tcr &= ~P_TCR_DS;
		} else {
			value_tcr |= P_TCR_DS;
		}
	}
	if (mask & PARPORT_CONTROL_AUTOFD) {
		if (val & PARPORT_CONTROL_AUTOFD) {
			value_or &= ~P_OR_AFXN;
		} else {
			value_or |= P_OR_AFXN;
		}
	}
	if (mask & PARPORT_CONTROL_INIT) {
		if (val & PARPORT_CONTROL_INIT) {
			value_or &= ~P_OR_INIT;
		} else {
			value_or |= P_OR_INIT;
		}
	}
	if (mask & PARPORT_CONTROL_SELECT) {
		if (val & PARPORT_CONTROL_SELECT) {
			value_or |= P_OR_SLCT_IN;
		} else {
			value_or &= ~P_OR_SLCT_IN;
		}
	}

	sbus_writeb(value_or, &regs->p_or);
	sbus_writeb(value_tcr, &regs->p_tcr);
	dprintk((KERN_DEBUG "frob2: tcr 0x%x or 0x%x\n", regs->p_tcr, regs->p_or));
	return parport_sunbpp_read_control(p);
}

static void parport_sunbpp_write_control(struct parport *p, unsigned char d)
{
	const unsigned char wm = (PARPORT_CONTROL_STROBE |
				  PARPORT_CONTROL_AUTOFD |
				  PARPORT_CONTROL_INIT |
				  PARPORT_CONTROL_SELECT);

	parport_sunbpp_frob_control (p, wm, d & wm);
}

static unsigned char parport_sunbpp_read_status(struct parport *p)
{
	return status_sunbpp_to_pc(p);
}

static void parport_sunbpp_data_forward (struct parport *p)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;
	unsigned char value_tcr = sbus_readb(&regs->p_tcr);

	dprintk((KERN_DEBUG "forward\n"));
	value_tcr &= ~P_TCR_DIR;
	sbus_writeb(value_tcr, &regs->p_tcr);
}

static void parport_sunbpp_data_reverse (struct parport *p)
{
	struct bpp_regs __iomem *regs = (struct bpp_regs __iomem *)p->base;
	u8 val = sbus_readb(&regs->p_tcr);

	dprintk((KERN_DEBUG "reverse\n"));
	val |= P_TCR_DIR;
	sbus_writeb(val, &regs->p_tcr);
}

static void parport_sunbpp_init_state(struct pardevice *dev, struct parport_state *s)
{
	s->u.pc.ctr = 0xc;
	s->u.pc.ecr = 0x0;
}

static void parport_sunbpp_save_state(struct parport *p, struct parport_state *s)
{
	s->u.pc.ctr = parport_sunbpp_read_control(p);
}

static void parport_sunbpp_restore_state(struct parport *p, struct parport_state *s)
{
	parport_sunbpp_write_control(p, s->u.pc.ctr);
}

static struct parport_operations parport_sunbpp_ops = 
{
	.write_data	= parport_sunbpp_write_data,
	.read_data	= parport_sunbpp_read_data,

	.write_control	= parport_sunbpp_write_control,
	.read_control	= parport_sunbpp_read_control,
	.frob_control	= parport_sunbpp_frob_control,

	.read_status	= parport_sunbpp_read_status,

	.enable_irq	= parport_sunbpp_enable_irq,
	.disable_irq	= parport_sunbpp_disable_irq,

	.data_forward	= parport_sunbpp_data_forward,
	.data_reverse	= parport_sunbpp_data_reverse,

	.init_state	= parport_sunbpp_init_state,
	.save_state	= parport_sunbpp_save_state,
	.restore_state	= parport_sunbpp_restore_state,

	.epp_write_data	= parport_ieee1284_epp_write_data,
	.epp_read_data	= parport_ieee1284_epp_read_data,
	.epp_write_addr	= parport_ieee1284_epp_write_addr,
	.epp_read_addr	= parport_ieee1284_epp_read_addr,

	.ecp_write_data	= parport_ieee1284_ecp_write_data,
	.ecp_read_data	= parport_ieee1284_ecp_read_data,
	.ecp_write_addr	= parport_ieee1284_ecp_write_addr,

	.compat_write_data	= parport_ieee1284_write_compat,
	.nibble_read_data	= parport_ieee1284_read_nibble,
	.byte_read_data		= parport_ieee1284_read_byte,

	.owner		= THIS_MODULE,
};

static int __devinit init_one_port(struct sbus_dev *sdev)
{
	struct parport *p;
	/* at least in theory there may be a "we don't dma" case */
	struct parport_operations *ops;
	void __iomem *base;
	int irq, dma, err = 0, size;
	struct bpp_regs __iomem *regs;
	unsigned char value_tcr;

	irq = sdev->irqs[0];
	base = sbus_ioremap(&sdev->resource[0], 0,
			    sdev->reg_addrs[0].reg_size, 
			    "sunbpp");
	if (!base)
		return -ENODEV;

	size = sdev->reg_addrs[0].reg_size;
	dma = PARPORT_DMA_NONE;

	ops = kmalloc(sizeof(struct parport_operations), GFP_KERNEL);
        if (!ops)
		goto out_unmap;

        memcpy (ops, &parport_sunbpp_ops, sizeof (struct parport_operations));

	dprintk(("register_port\n"));
	if (!(p = parport_register_port((unsigned long)base, irq, dma, ops)))
		goto out_free_ops;

	p->size = size;

	if ((err = request_irq(p->irq, parport_sunbpp_interrupt,
			       SA_SHIRQ, p->name, p)) != 0) {
		goto out_put_port;
	}

	parport_sunbpp_enable_irq(p);

	regs = (struct bpp_regs __iomem *)p->base;

	value_tcr = sbus_readb(&regs->p_tcr);
	value_tcr &= ~P_TCR_DIR;
	sbus_writeb(value_tcr, &regs->p_tcr);

	printk(KERN_INFO "%s: sunbpp at 0x%lx\n", p->name, p->base);

	dev_set_drvdata(&sdev->ofdev.dev, p);

	parport_announce_port(p);

	return 0;

out_put_port:
	parport_put_port(p);

out_free_ops:
	kfree(ops);

out_unmap:
	sbus_iounmap(base, size);

	return err;
}

static int __devinit bpp_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct sbus_dev *sdev = to_sbus_device(&dev->dev);

	return init_one_port(sdev);
}

static int __devexit bpp_remove(struct of_device *dev)
{
	struct parport *p = dev_get_drvdata(&dev->dev);
	struct parport_operations *ops = p->ops;

	parport_remove_port(p);

	if (p->irq != PARPORT_IRQ_NONE) {
		parport_sunbpp_disable_irq(p);
		free_irq(p->irq, p);
	}

	sbus_iounmap((void __iomem *) p->base, p->size);
	parport_put_port(p);
	kfree(ops);

	dev_set_drvdata(&dev->dev, NULL);

	return 0;
}

static struct of_device_id bpp_match[] = {
	{
		.name = "SUNW,bpp",
	},
	{},
};

MODULE_DEVICE_TABLE(of, qec_sbus_match);

static struct of_platform_driver bpp_sbus_driver = {
	.name		= "bpp",
	.match_table	= bpp_match,
	.probe		= bpp_probe,
	.remove		= __devexit_p(bpp_remove),
};

static int __init parport_sunbpp_init(void)
{
	return of_register_driver(&bpp_sbus_driver, &sbus_bus_type);
}

static void __exit parport_sunbpp_exit(void)
{
	of_unregister_driver(&bpp_sbus_driver);
}

MODULE_AUTHOR("Derrick J Brashear");
MODULE_DESCRIPTION("Parport Driver for Sparc bidirectional Port");
MODULE_SUPPORTED_DEVICE("Sparc Bidirectional Parallel Port");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");

module_init(parport_sunbpp_init)
module_exit(parport_sunbpp_exit)
