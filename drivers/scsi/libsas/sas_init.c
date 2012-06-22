/*
 * Serial Attached SCSI (SAS) Transport Layer initialization
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <scsi/sas_ata.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>

#include "sas_internal.h"

#include "../scsi_sas_internal.h"

static struct kmem_cache *sas_task_cache;

struct sas_task *sas_alloc_task(gfp_t flags)
{
	struct sas_task *task = kmem_cache_zalloc(sas_task_cache, flags);

	if (task) {
		INIT_LIST_HEAD(&task->list);
		spin_lock_init(&task->task_state_lock);
		task->task_state_flags = SAS_TASK_STATE_PENDING;
		init_timer(&task->timer);
		init_completion(&task->completion);
	}

	return task;
}
EXPORT_SYMBOL_GPL(sas_alloc_task);

void sas_free_task(struct sas_task *task)
{
	if (task) {
		BUG_ON(!list_empty(&task->list));
		kmem_cache_free(sas_task_cache, task);
	}
}
EXPORT_SYMBOL_GPL(sas_free_task);

/*------------ SAS addr hash -----------*/
void sas_hash_addr(u8 *hashed, const u8 *sas_addr)
{
        const u32 poly = 0x00DB2777;
        u32     r = 0;
        int     i;

        for (i = 0; i < 8; i++) {
                int b;
                for (b = 7; b >= 0; b--) {
                        r <<= 1;
                        if ((1 << b) & sas_addr[i]) {
                                if (!(r & 0x01000000))
                                        r ^= poly;
                        } else if (r & 0x01000000)
                                r ^= poly;
                }
        }

        hashed[0] = (r >> 16) & 0xFF;
        hashed[1] = (r >> 8) & 0xFF ;
        hashed[2] = r & 0xFF;
}


/* ---------- HA events ---------- */

void sas_hae_reset(struct work_struct *work)
{
	struct sas_ha_event *ev = to_sas_ha_event(work);
	struct sas_ha_struct *ha = ev->ha;

	clear_bit(HAE_RESET, &ha->pending);
}

int sas_register_ha(struct sas_ha_struct *sas_ha)
{
	int error = 0;

	mutex_init(&sas_ha->disco_mutex);
	spin_lock_init(&sas_ha->phy_port_lock);
	sas_hash_addr(sas_ha->hashed_sas_addr, sas_ha->sas_addr);

	if (sas_ha->lldd_queue_size == 0)
		sas_ha->lldd_queue_size = 1;
	else if (sas_ha->lldd_queue_size == -1)
		sas_ha->lldd_queue_size = 128; /* Sanity */

	set_bit(SAS_HA_REGISTERED, &sas_ha->state);
	spin_lock_init(&sas_ha->lock);
	mutex_init(&sas_ha->drain_mutex);
	init_waitqueue_head(&sas_ha->eh_wait_q);
	INIT_LIST_HEAD(&sas_ha->defer_q);
	INIT_LIST_HEAD(&sas_ha->eh_dev_q);

	error = sas_register_phys(sas_ha);
	if (error) {
		printk(KERN_NOTICE "couldn't register sas phys:%d\n", error);
		return error;
	}

	error = sas_register_ports(sas_ha);
	if (error) {
		printk(KERN_NOTICE "couldn't register sas ports:%d\n", error);
		goto Undo_phys;
	}

	error = sas_init_events(sas_ha);
	if (error) {
		printk(KERN_NOTICE "couldn't start event thread:%d\n", error);
		goto Undo_ports;
	}

	if (sas_ha->lldd_max_execute_num > 1) {
		error = sas_init_queue(sas_ha);
		if (error) {
			printk(KERN_NOTICE "couldn't start queue thread:%d, "
			       "running in direct mode\n", error);
			sas_ha->lldd_max_execute_num = 1;
		}
	}

	INIT_LIST_HEAD(&sas_ha->eh_done_q);
	INIT_LIST_HEAD(&sas_ha->eh_ata_q);

	return 0;

Undo_ports:
	sas_unregister_ports(sas_ha);
Undo_phys:

	return error;
}

int sas_unregister_ha(struct sas_ha_struct *sas_ha)
{
	/* Set the state to unregistered to avoid further unchained
	 * events to be queued, and flush any in-progress drainers
	 */
	mutex_lock(&sas_ha->drain_mutex);
	spin_lock_irq(&sas_ha->lock);
	clear_bit(SAS_HA_REGISTERED, &sas_ha->state);
	spin_unlock_irq(&sas_ha->lock);
	__sas_drain_work(sas_ha);
	mutex_unlock(&sas_ha->drain_mutex);

	sas_unregister_ports(sas_ha);

	/* flush unregistration work */
	mutex_lock(&sas_ha->drain_mutex);
	__sas_drain_work(sas_ha);
	mutex_unlock(&sas_ha->drain_mutex);

	if (sas_ha->lldd_max_execute_num > 1) {
		sas_shutdown_queue(sas_ha);
		sas_ha->lldd_max_execute_num = 1;
	}

	return 0;
}

static int sas_get_linkerrors(struct sas_phy *phy)
{
	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->core.shost->transportt);

		return i->dft->lldd_control_phy(asd_phy, PHY_FUNC_GET_EVENTS, NULL);
	}

	return sas_smp_get_phy_events(phy);
}

int sas_try_ata_reset(struct asd_sas_phy *asd_phy)
{
	struct domain_device *dev = NULL;

	/* try to route user requested link resets through libata */
	if (asd_phy->port)
		dev = asd_phy->port->port_dev;

	/* validate that dev has been probed */
	if (dev)
		dev = sas_find_dev_by_rphy(dev->rphy);

	if (dev && dev_is_sata(dev)) {
		sas_ata_schedule_reset(dev);
		sas_ata_wait_eh(dev);
		return 0;
	}

	return -ENODEV;
}

/**
 * transport_sas_phy_reset - reset a phy and permit libata to manage the link
 *
 * phy reset request via sysfs in host workqueue context so we know we
 * can block on eh and safely traverse the domain_device topology
 */
static int transport_sas_phy_reset(struct sas_phy *phy, int hard_reset)
{
	enum phy_func reset_type;

	if (hard_reset)
		reset_type = PHY_FUNC_HARD_RESET;
	else
		reset_type = PHY_FUNC_LINK_RESET;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->core.shost->transportt);

		if (!hard_reset && sas_try_ata_reset(asd_phy) == 0)
			return 0;
		return i->dft->lldd_control_phy(asd_phy, reset_type, NULL);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);
		struct domain_device *ata_dev = sas_ex_to_ata(ddev, phy->number);

		if (ata_dev && !hard_reset) {
			sas_ata_schedule_reset(ata_dev);
			sas_ata_wait_eh(ata_dev);
			return 0;
		} else
			return sas_smp_phy_control(ddev, phy->number, reset_type, NULL);
	}
}

static int sas_phy_enable(struct sas_phy *phy, int enable)
{
	int ret;
	enum phy_func cmd;

	if (enable)
		cmd = PHY_FUNC_LINK_RESET;
	else
		cmd = PHY_FUNC_DISABLE;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->core.shost->transportt);

		if (enable)
			ret = transport_sas_phy_reset(phy, 0);
		else
			ret = i->dft->lldd_control_phy(asd_phy, cmd, NULL);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);

		if (enable)
			ret = transport_sas_phy_reset(phy, 0);
		else
			ret = sas_smp_phy_control(ddev, phy->number, cmd, NULL);
	}
	return ret;
}

int sas_phy_reset(struct sas_phy *phy, int hard_reset)
{
	int ret;
	enum phy_func reset_type;

	if (!phy->enabled)
		return -ENODEV;

	if (hard_reset)
		reset_type = PHY_FUNC_HARD_RESET;
	else
		reset_type = PHY_FUNC_LINK_RESET;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->core.shost->transportt);

		ret = i->dft->lldd_control_phy(asd_phy, reset_type, NULL);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);
		ret = sas_smp_phy_control(ddev, phy->number, reset_type, NULL);
	}
	return ret;
}

int sas_set_phy_speed(struct sas_phy *phy,
		      struct sas_phy_linkrates *rates)
{
	int ret;

	if ((rates->minimum_linkrate &&
	     rates->minimum_linkrate > phy->maximum_linkrate) ||
	    (rates->maximum_linkrate &&
	     rates->maximum_linkrate < phy->minimum_linkrate))
		return -EINVAL;

	if (rates->minimum_linkrate &&
	    rates->minimum_linkrate < phy->minimum_linkrate_hw)
		rates->minimum_linkrate = phy->minimum_linkrate_hw;

	if (rates->maximum_linkrate &&
	    rates->maximum_linkrate > phy->maximum_linkrate_hw)
		rates->maximum_linkrate = phy->maximum_linkrate_hw;

	if (scsi_is_sas_phy_local(phy)) {
		struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
		struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(shost);
		struct asd_sas_phy *asd_phy = sas_ha->sas_phy[phy->number];
		struct sas_internal *i =
			to_sas_internal(sas_ha->core.shost->transportt);

		ret = i->dft->lldd_control_phy(asd_phy, PHY_FUNC_SET_LINK_RATE,
					       rates);
	} else {
		struct sas_rphy *rphy = dev_to_rphy(phy->dev.parent);
		struct domain_device *ddev = sas_find_dev_by_rphy(rphy);
		ret = sas_smp_phy_control(ddev, phy->number,
					  PHY_FUNC_LINK_RESET, rates);

	}

	return ret;
}

static void sas_phy_release(struct sas_phy *phy)
{
	kfree(phy->hostdata);
	phy->hostdata = NULL;
}

static void phy_reset_work(struct work_struct *work)
{
	struct sas_phy_data *d = container_of(work, typeof(*d), reset_work.work);

	d->reset_result = transport_sas_phy_reset(d->phy, d->hard_reset);
}

static void phy_enable_work(struct work_struct *work)
{
	struct sas_phy_data *d = container_of(work, typeof(*d), enable_work.work);

	d->enable_result = sas_phy_enable(d->phy, d->enable);
}

static int sas_phy_setup(struct sas_phy *phy)
{
	struct sas_phy_data *d = kzalloc(sizeof(*d), GFP_KERNEL);

	if (!d)
		return -ENOMEM;

	mutex_init(&d->event_lock);
	INIT_SAS_WORK(&d->reset_work, phy_reset_work);
	INIT_SAS_WORK(&d->enable_work, phy_enable_work);
	d->phy = phy;
	phy->hostdata = d;

	return 0;
}

static int queue_phy_reset(struct sas_phy *phy, int hard_reset)
{
	struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	struct sas_phy_data *d = phy->hostdata;
	int rc;

	if (!d)
		return -ENOMEM;

	/* libsas workqueue coordinates ata-eh reset with discovery */
	mutex_lock(&d->event_lock);
	d->reset_result = 0;
	d->hard_reset = hard_reset;

	spin_lock_irq(&ha->lock);
	sas_queue_work(ha, &d->reset_work);
	spin_unlock_irq(&ha->lock);

	rc = sas_drain_work(ha);
	if (rc == 0)
		rc = d->reset_result;
	mutex_unlock(&d->event_lock);

	return rc;
}

static int queue_phy_enable(struct sas_phy *phy, int enable)
{
	struct Scsi_Host *shost = dev_to_shost(phy->dev.parent);
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	struct sas_phy_data *d = phy->hostdata;
	int rc;

	if (!d)
		return -ENOMEM;

	/* libsas workqueue coordinates ata-eh reset with discovery */
	mutex_lock(&d->event_lock);
	d->enable_result = 0;
	d->enable = enable;

	spin_lock_irq(&ha->lock);
	sas_queue_work(ha, &d->enable_work);
	spin_unlock_irq(&ha->lock);

	rc = sas_drain_work(ha);
	if (rc == 0)
		rc = d->enable_result;
	mutex_unlock(&d->event_lock);

	return rc;
}

static struct sas_function_template sft = {
	.phy_enable = queue_phy_enable,
	.phy_reset = queue_phy_reset,
	.phy_setup = sas_phy_setup,
	.phy_release = sas_phy_release,
	.set_phy_speed = sas_set_phy_speed,
	.get_linkerrors = sas_get_linkerrors,
	.smp_handler = sas_smp_handler,
};

struct scsi_transport_template *
sas_domain_attach_transport(struct sas_domain_function_template *dft)
{
	struct scsi_transport_template *stt = sas_attach_transport(&sft);
	struct sas_internal *i;

	if (!stt)
		return stt;

	i = to_sas_internal(stt);
	i->dft = dft;
	stt->create_work_queue = 1;
	stt->eh_timed_out = sas_scsi_timed_out;
	stt->eh_strategy_handler = sas_scsi_recover_host;

	return stt;
}
EXPORT_SYMBOL_GPL(sas_domain_attach_transport);


void sas_domain_release_transport(struct scsi_transport_template *stt)
{
	sas_release_transport(stt);
}
EXPORT_SYMBOL_GPL(sas_domain_release_transport);

/* ---------- SAS Class register/unregister ---------- */

static int __init sas_class_init(void)
{
	sas_task_cache = KMEM_CACHE(sas_task, SLAB_HWCACHE_ALIGN);
	if (!sas_task_cache)
		return -ENOMEM;

	return 0;
}

static void __exit sas_class_exit(void)
{
	kmem_cache_destroy(sas_task_cache);
}

MODULE_AUTHOR("Luben Tuikov <luben_tuikov@adaptec.com>");
MODULE_DESCRIPTION("SAS Transport Layer");
MODULE_LICENSE("GPL v2");

module_init(sas_class_init);
module_exit(sas_class_exit);

EXPORT_SYMBOL_GPL(sas_register_ha);
EXPORT_SYMBOL_GPL(sas_unregister_ha);
