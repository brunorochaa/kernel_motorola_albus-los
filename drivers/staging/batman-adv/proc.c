/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "proc.h"
#include "routing.h"
#include "translation-table.h"
#include "hard-interface.h"
#include "types.h"
#include "hash.h"
#include "vis.h"

static struct proc_dir_entry *proc_batman_dir, *proc_interface_file;
static struct proc_dir_entry *proc_orig_interval_file;
static struct proc_dir_entry *proc_vis_srv_file, *proc_vis_data_file;

static int proc_interfaces_read(struct seq_file *seq, void *offset)
{
	struct batman_if *batman_if;

	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		seq_printf(seq, "[%8s] %s %s\n",
			   (batman_if->if_active == IF_ACTIVE ?
			    "active" : "inactive"),
			   batman_if->dev,
			   (batman_if->if_active == IF_ACTIVE ?
			    batman_if->addr_str : " "));
	}
	rcu_read_unlock();

	return 0;
}

static int proc_interfaces_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_interfaces_read, NULL);
}

static ssize_t proc_interfaces_write(struct file *instance,
				     const char __user *userbuffer,
				     size_t count, loff_t *data)
{
	char *if_string, *colon_ptr = NULL, *cr_ptr = NULL;
	int not_copied = 0, if_num = 0, add_success;
	struct batman_if *batman_if = NULL;

	if_string = kmalloc(count, GFP_KERNEL);

	if (!if_string)
		return -ENOMEM;

	if (count > IFNAMSIZ - 1) {
		printk(KERN_WARNING "batman-adv:Can't add interface: device name is too long\n");
		goto end;
	}

	not_copied = copy_from_user(if_string, userbuffer, count);
	if_string[count - not_copied - 1] = 0;

	colon_ptr = strchr(if_string, ':');
	if (colon_ptr)
		*colon_ptr = 0;

	if (!colon_ptr) {
		cr_ptr = strchr(if_string, '\n');
		if (cr_ptr)
			*cr_ptr = 0;
	}

	if (strlen(if_string) == 0) {
		shutdown_module();
		num_ifs = 0;
		goto end;
	}

	/* add interface */
	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if (strncmp(batman_if->dev, if_string, count) == 0) {
			printk(KERN_ERR "batman-adv:Given interface is already active: %s\n", if_string);
			rcu_read_unlock();
			goto end;

		}

		if_num++;
	}
	rcu_read_unlock();

	add_success = hardif_add_interface(if_string, if_num);
	if (add_success < 0)
		goto end;

	num_ifs = if_num + 1;

	if ((atomic_read(&module_state) == MODULE_INACTIVE) &&
	    (hardif_get_active_if_num() > 0))
		activate_module();

	return count;
end:
	kfree(if_string);
	return count;
}

static int proc_orig_interval_read(struct seq_file *seq, void *offset)
{
	seq_printf(seq, "%i\n", atomic_read(&originator_interval));

	return 0;
}

static ssize_t proc_orig_interval_write(struct file *file,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	char *interval_string;
	int not_copied = 0;
	unsigned long originator_interval_tmp;
	int retval;

	interval_string = kmalloc(count, GFP_KERNEL);

	if (!interval_string)
		return -ENOMEM;

	not_copied = copy_from_user(interval_string, buffer, count);
	interval_string[count - not_copied - 1] = 0;

	retval = strict_strtoul(interval_string, 10, &originator_interval_tmp);
	if (retval) {
		printk(KERN_ERR "batman-adv:New originator interval invalid\n");
		goto end;
	}

	if (originator_interval_tmp <= JITTER * 2) {
		printk(KERN_WARNING "batman-adv:New originator interval too small: %li (min: %i)\n",
		       originator_interval_tmp, JITTER * 2);
		goto end;
	}

	printk(KERN_INFO "batman-adv:Changing originator interval from: %i to: %li\n",
	       atomic_read(&originator_interval), originator_interval_tmp);

	atomic_set(&originator_interval, originator_interval_tmp);

end:
	kfree(interval_string);
	return count;
}

static int proc_orig_interval_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_orig_interval_read, NULL);
}

/* setting the mode of the vis server by the user */
static ssize_t proc_vis_srv_write(struct file *file, const char __user * buffer,
			      size_t count, loff_t *ppos)
{
	char *vis_mode_string;
	int not_copied = 0;

	vis_mode_string = kmalloc(count, GFP_KERNEL);

	if (!vis_mode_string)
		return -ENOMEM;

	not_copied = copy_from_user(vis_mode_string, buffer, count);
	vis_mode_string[count - not_copied - 1] = 0;

	if ((strcmp(vis_mode_string, "client") == 0) ||
			(strcmp(vis_mode_string, "disabled") == 0)) {
		printk(KERN_INFO "batman-adv:Setting VIS mode to client (disabling vis server)\n");
		atomic_set(&vis_mode, VIS_TYPE_CLIENT_UPDATE);
	} else if ((strcmp(vis_mode_string, "server") == 0) ||
			(strcmp(vis_mode_string, "enabled") == 0)) {
		printk(KERN_INFO "batman-adv:Setting VIS mode to server (enabling vis server)\n");
		atomic_set(&vis_mode, VIS_TYPE_SERVER_SYNC);
	} else
		printk(KERN_ERR "batman-adv:Unknown VIS mode: %s\n",
		       vis_mode_string);

	kfree(vis_mode_string);
	return count;
}

static int proc_vis_srv_read(struct seq_file *seq, void *offset)
{
	int vis_server = atomic_read(&vis_mode);

	seq_printf(seq, "[%c] client mode (server disabled)\n",
			(vis_server == VIS_TYPE_CLIENT_UPDATE) ? 'x' : ' ');
	seq_printf(seq, "[%c] server mode (server enabled)\n",
			(vis_server == VIS_TYPE_SERVER_SYNC) ? 'x' : ' ');

	return 0;
}

static int proc_vis_srv_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_vis_srv_read, NULL);
}

static int proc_vis_data_read(struct seq_file *seq, void *offset)
{
	HASHIT(hashit);
	struct vis_info *info;
	struct vis_info_entry *entries;
	HLIST_HEAD(vis_if_list);
	struct if_list_entry *entry;
	struct hlist_node *pos, *n;
	int i;
	char tmp_addr_str[ETH_STR_LEN];
	unsigned long flags;
	int vis_server = atomic_read(&vis_mode);

	rcu_read_lock();
	if (list_empty(&if_list) || (vis_server == VIS_TYPE_CLIENT_UPDATE)) {
		rcu_read_unlock();
		goto end;
	}

	rcu_read_unlock();

	spin_lock_irqsave(&vis_hash_lock, flags);
	while (hash_iterate(vis_hash, &hashit)) {
		info = hashit.bucket->data;
		entries = (struct vis_info_entry *)
			((char *)info + sizeof(struct vis_info));

		for (i = 0; i < info->packet.entries; i++) {
			if (entries[i].quality == 0)
				continue;
			proc_vis_insert_interface(entries[i].src, &vis_if_list,
				compare_orig(entries[i].src,
						info->packet.vis_orig));
		}

		hlist_for_each_entry(entry, pos, &vis_if_list, list) {
			addr_to_string(tmp_addr_str, entry->addr);
			seq_printf(seq, "%s,", tmp_addr_str);

			for (i = 0; i < info->packet.entries; i++)
				proc_vis_read_entry(seq, &entries[i],
						entry->addr, entry->primary);

			/* add primary/secondary records */
			if (compare_orig(entry->addr, info->packet.vis_orig))
				proc_vis_read_prim_sec(seq, &vis_if_list);

			seq_printf(seq, "\n");
		}

		hlist_for_each_entry_safe(entry, pos, n, &vis_if_list, list) {
			hlist_del(&entry->list);
			kfree(entry);
		}
	}
	spin_unlock_irqrestore(&vis_hash_lock, flags);

end:
	return 0;
}

static int proc_vis_data_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_vis_data_read, NULL);
}

/* satisfying different prototypes ... */
static ssize_t proc_dummy_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos)
{
	return count;
}

static const struct file_operations proc_vis_srv_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_vis_srv_open,
	.read		= seq_read,
	.write		= proc_vis_srv_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_vis_data_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_vis_data_open,
	.read		= seq_read,
	.write		= proc_dummy_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_interfaces_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_interfaces_open,
	.read		= seq_read,
	.write		= proc_interfaces_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations proc_orig_interval_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_orig_interval_open,
	.read		= seq_read,
	.write		= proc_orig_interval_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void cleanup_procfs(void)
{
	if (proc_orig_interval_file)
		remove_proc_entry(PROC_FILE_ORIG_INTERVAL, proc_batman_dir);

	if (proc_interface_file)
		remove_proc_entry(PROC_FILE_INTERFACES, proc_batman_dir);

	if (proc_vis_data_file)
		remove_proc_entry(PROC_FILE_VIS_DATA, proc_batman_dir);

	if (proc_vis_srv_file)
		remove_proc_entry(PROC_FILE_VIS_SRV, proc_batman_dir);

	if (proc_batman_dir)
#ifdef __NET_NET_NAMESPACE_H
		remove_proc_entry(PROC_ROOT_DIR, init_net.proc_net);
#else
		remove_proc_entry(PROC_ROOT_DIR, proc_net);
#endif
}

int setup_procfs(void)
{
#ifdef __NET_NET_NAMESPACE_H
	proc_batman_dir = proc_mkdir(PROC_ROOT_DIR, init_net.proc_net);
#else
	proc_batman_dir = proc_mkdir(PROC_ROOT_DIR, proc_net);
#endif

	if (!proc_batman_dir) {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s' folder failed\n", PROC_ROOT_DIR);
		return -EFAULT;
	}

	proc_interface_file = create_proc_entry(PROC_FILE_INTERFACES,
						S_IWUSR | S_IRUGO,
						proc_batman_dir);
	if (proc_interface_file) {
		proc_interface_file->proc_fops = &proc_interfaces_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_INTERFACES);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_orig_interval_file = create_proc_entry(PROC_FILE_ORIG_INTERVAL,
						    S_IWUSR | S_IRUGO,
						    proc_batman_dir);
	if (proc_orig_interval_file) {
		proc_orig_interval_file->proc_fops = &proc_orig_interval_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_ORIG_INTERVAL);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_vis_srv_file = create_proc_entry(PROC_FILE_VIS_SRV,
						S_IWUSR | S_IRUGO,
						proc_batman_dir);
	if (proc_vis_srv_file) {
		proc_vis_srv_file->proc_fops = &proc_vis_srv_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_VIS_SRV);
		cleanup_procfs();
		return -EFAULT;
	}

	proc_vis_data_file = create_proc_entry(PROC_FILE_VIS_DATA, S_IRUGO,
					  proc_batman_dir);
	if (proc_vis_data_file) {
		proc_vis_data_file->proc_fops = &proc_vis_data_fops;
	} else {
		printk(KERN_ERR "batman-adv: Registering the '/proc/net/%s/%s' file failed\n", PROC_ROOT_DIR, PROC_FILE_VIS_DATA);
		cleanup_procfs();
		return -EFAULT;
	}

	return 0;
}
