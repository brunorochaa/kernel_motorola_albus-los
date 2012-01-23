/*
 * /proc/sys support
 */
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/namei.h>
#include <linux/module.h>
#include "internal.h"

static const struct dentry_operations proc_sys_dentry_operations;
static const struct file_operations proc_sys_file_operations;
static const struct inode_operations proc_sys_inode_operations;
static const struct file_operations proc_sys_dir_file_operations;
static const struct inode_operations proc_sys_dir_operations;

void proc_sys_poll_notify(struct ctl_table_poll *poll)
{
	if (!poll)
		return;

	atomic_inc(&poll->event);
	wake_up_interruptible(&poll->wait);
}

static struct ctl_table root_table[] = {
	{
		.procname = "",
		.mode = S_IRUGO|S_IXUGO,
		.child = &root_table[1],
	},
	{ }
};
static struct ctl_table_root sysctl_table_root;
static struct ctl_table_header root_table_header = {
	{{.count = 1,
	  .nreg = 1,
	  .ctl_table = root_table,
	  .ctl_entry = LIST_HEAD_INIT(sysctl_table_root.default_set.list),}},
	.root = &sysctl_table_root,
	.set = &sysctl_table_root.default_set,
};
static struct ctl_table_root sysctl_table_root = {
	.root_list = LIST_HEAD_INIT(sysctl_table_root.root_list),
	.default_set.list = LIST_HEAD_INIT(root_table_header.ctl_entry),
	.default_set.root = &sysctl_table_root,
};

static DEFINE_SPINLOCK(sysctl_lock);

static int namecmp(const char *name1, int len1, const char *name2, int len2)
{
	int minlen;
	int cmp;

	minlen = len1;
	if (minlen > len2)
		minlen = len2;

	cmp = memcmp(name1, name2, minlen);
	if (cmp == 0)
		cmp = len1 - len2;
	return cmp;
}

static struct ctl_table *find_entry(struct ctl_table_header **phead,
	struct ctl_table_set *set,
	struct ctl_table_header *dir_head, struct ctl_table *dir,
	const char *name, int namelen)
{
	struct ctl_table_header *head;
	struct ctl_table *entry;

	if (dir_head->set == set) {
		for (entry = dir; entry->procname; entry++) {
			const char *procname = entry->procname;
			if (namecmp(procname, strlen(procname), name, namelen) == 0) {
				*phead = dir_head;
				return entry;
			}
		}
	}

	list_for_each_entry(head, &set->list, ctl_entry) {
		if (head->unregistering)
			continue;
		if (head->attached_to != dir)
			continue;
		for (entry = head->attached_by; entry->procname; entry++) {
			const char *procname = entry->procname;
			if (namecmp(procname, strlen(procname), name, namelen) == 0) {
				*phead = head;
				return entry;
			}
		}
	}
	return NULL;
}

static void init_header(struct ctl_table_header *head,
	struct ctl_table_root *root, struct ctl_table_set *set,
	struct ctl_table *table)
{
	head->ctl_table_arg = table;
	INIT_LIST_HEAD(&head->ctl_entry);
	head->used = 0;
	head->count = 1;
	head->nreg = 1;
	head->unregistering = NULL;
	head->root = root;
	head->set = set;
	head->parent = NULL;
}

static void erase_header(struct ctl_table_header *head)
{
	list_del_init(&head->ctl_entry);
}

static void insert_header(struct ctl_table_header *header)
{
	header->parent->count++;
	list_add_tail(&header->ctl_entry, &header->set->list);
}

/* called under sysctl_lock */
static int use_table(struct ctl_table_header *p)
{
	if (unlikely(p->unregistering))
		return 0;
	p->used++;
	return 1;
}

/* called under sysctl_lock */
static void unuse_table(struct ctl_table_header *p)
{
	if (!--p->used)
		if (unlikely(p->unregistering))
			complete(p->unregistering);
}

/* called under sysctl_lock, will reacquire if has to wait */
static void start_unregistering(struct ctl_table_header *p)
{
	/*
	 * if p->used is 0, nobody will ever touch that entry again;
	 * we'll eliminate all paths to it before dropping sysctl_lock
	 */
	if (unlikely(p->used)) {
		struct completion wait;
		init_completion(&wait);
		p->unregistering = &wait;
		spin_unlock(&sysctl_lock);
		wait_for_completion(&wait);
		spin_lock(&sysctl_lock);
	} else {
		/* anything non-NULL; we'll never dereference it */
		p->unregistering = ERR_PTR(-EINVAL);
	}
	/*
	 * do not remove from the list until nobody holds it; walking the
	 * list in do_sysctl() relies on that.
	 */
	erase_header(p);
}

static void sysctl_head_get(struct ctl_table_header *head)
{
	spin_lock(&sysctl_lock);
	head->count++;
	spin_unlock(&sysctl_lock);
}

void sysctl_head_put(struct ctl_table_header *head)
{
	spin_lock(&sysctl_lock);
	if (!--head->count)
		kfree_rcu(head, rcu);
	spin_unlock(&sysctl_lock);
}

static struct ctl_table_header *sysctl_head_grab(struct ctl_table_header *head)
{
	if (!head)
		BUG();
	spin_lock(&sysctl_lock);
	if (!use_table(head))
		head = ERR_PTR(-ENOENT);
	spin_unlock(&sysctl_lock);
	return head;
}

static void sysctl_head_finish(struct ctl_table_header *head)
{
	if (!head)
		return;
	spin_lock(&sysctl_lock);
	unuse_table(head);
	spin_unlock(&sysctl_lock);
}

static struct ctl_table_set *
lookup_header_set(struct ctl_table_root *root, struct nsproxy *namespaces)
{
	struct ctl_table_set *set = &root->default_set;
	if (root->lookup)
		set = root->lookup(root, namespaces);
	return set;
}

static struct list_head *
lookup_header_list(struct ctl_table_root *root, struct nsproxy *namespaces)
{
	struct ctl_table_set *set = lookup_header_set(root, namespaces);
	return &set->list;
}

static struct ctl_table *lookup_entry(struct ctl_table_header **phead,
				      struct ctl_table_header *dir_head,
				      struct ctl_table *dir,
				      const char *name, int namelen)
{
	struct ctl_table_header *head;
	struct ctl_table *entry;
	struct ctl_table_root *root;
	struct ctl_table_set *set;

	spin_lock(&sysctl_lock);
	root = &sysctl_table_root;
	do {
		set = lookup_header_set(root, current->nsproxy);
		entry = find_entry(&head, set, dir_head, dir, name, namelen);
		if (entry && use_table(head))
			*phead = head;
		else
			entry = NULL;
		root = list_entry(root->root_list.next,
				  struct ctl_table_root, root_list);
	} while (!entry && root != &sysctl_table_root);
	spin_unlock(&sysctl_lock);
	return entry;
}

static struct ctl_table_header *next_usable_entry(struct ctl_table *dir,
	struct ctl_table_root *root, struct list_head *tmp)
{
	struct nsproxy *namespaces = current->nsproxy;
	struct list_head *header_list;
	struct ctl_table_header *head;

	goto next;
	for (;;) {
		head = list_entry(tmp, struct ctl_table_header, ctl_entry);
		root = head->root;

		if (head->attached_to != dir ||
		    !head->attached_by->procname ||
		    !use_table(head))
			goto next;

		return head;
	next:
		tmp = tmp->next;
		header_list = lookup_header_list(root, namespaces);
		if (tmp != header_list)
			continue;

		do {
			root = list_entry(root->root_list.next,
					struct ctl_table_root, root_list);
			if (root == &sysctl_table_root)
				goto out;
			header_list = lookup_header_list(root, namespaces);
		} while (list_empty(header_list));
		tmp = header_list->next;
	}
out:
	return NULL;
}

static void first_entry(
	struct ctl_table_header *dir_head, struct ctl_table *dir,
	struct ctl_table_header **phead, struct ctl_table **pentry)
{
	struct ctl_table_header *head = dir_head;
	struct ctl_table *entry = dir;

	spin_lock(&sysctl_lock);
	if (entry->procname) {
		use_table(head);
	} else {
		head = next_usable_entry(dir, &sysctl_table_root,
					 &sysctl_table_root.default_set.list);
		if (head)
			entry = head->attached_by;
	}
	spin_unlock(&sysctl_lock);
	*phead = head;
	*pentry = entry;
}

static void next_entry(struct ctl_table *dir,
	struct ctl_table_header **phead, struct ctl_table **pentry)
{
	struct ctl_table_header *head = *phead;
	struct ctl_table *entry = *pentry;

	entry++;
	if (!entry->procname) {
		struct ctl_table_root *root = head->root;
		struct list_head *tmp = &head->ctl_entry;
		if (head->attached_to != dir) {
			root = &sysctl_table_root;
			tmp = &sysctl_table_root.default_set.list;
		}
		spin_lock(&sysctl_lock);
		unuse_table(head);
		head = next_usable_entry(dir, root, tmp);
		spin_unlock(&sysctl_lock);
		if (head)
			entry = head->attached_by;
	}
	*phead = head;
	*pentry = entry;
}

void register_sysctl_root(struct ctl_table_root *root)
{
	spin_lock(&sysctl_lock);
	list_add_tail(&root->root_list, &sysctl_table_root.root_list);
	spin_unlock(&sysctl_lock);
}

/*
 * sysctl_perm does NOT grant the superuser all rights automatically, because
 * some sysctl variables are readonly even to root.
 */

static int test_perm(int mode, int op)
{
	if (!current_euid())
		mode >>= 6;
	else if (in_egroup_p(0))
		mode >>= 3;
	if ((op & ~mode & (MAY_READ|MAY_WRITE|MAY_EXEC)) == 0)
		return 0;
	return -EACCES;
}

static int sysctl_perm(struct ctl_table_root *root, struct ctl_table *table, int op)
{
	int mode;

	if (root->permissions)
		mode = root->permissions(root, current->nsproxy, table);
	else
		mode = table->mode;

	return test_perm(mode, op);
}

static struct inode *proc_sys_make_inode(struct super_block *sb,
		struct ctl_table_header *head, struct ctl_table *table)
{
	struct inode *inode;
	struct proc_inode *ei;

	inode = new_inode(sb);
	if (!inode)
		goto out;

	inode->i_ino = get_next_ino();

	sysctl_head_get(head);
	ei = PROC_I(inode);
	ei->sysctl = head;
	ei->sysctl_entry = table;

	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_mode = table->mode;
	if (!table->child) {
		inode->i_mode |= S_IFREG;
		inode->i_op = &proc_sys_inode_operations;
		inode->i_fop = &proc_sys_file_operations;
	} else {
		inode->i_mode |= S_IFDIR;
		inode->i_op = &proc_sys_dir_operations;
		inode->i_fop = &proc_sys_dir_file_operations;
	}
out:
	return inode;
}

static struct ctl_table_header *grab_header(struct inode *inode)
{
	struct ctl_table_header *head = PROC_I(inode)->sysctl;
	if (!head)
		head = &root_table_header;
	return sysctl_head_grab(head);
}

static struct dentry *proc_sys_lookup(struct inode *dir, struct dentry *dentry,
					struct nameidata *nd)
{
	struct ctl_table_header *head = grab_header(dir);
	struct ctl_table *table = PROC_I(dir)->sysctl_entry;
	struct ctl_table_header *h = NULL;
	struct qstr *name = &dentry->d_name;
	struct ctl_table *p;
	struct inode *inode;
	struct dentry *err = ERR_PTR(-ENOENT);

	if (IS_ERR(head))
		return ERR_CAST(head);

	if (table && !table->child) {
		WARN_ON(1);
		goto out;
	}

	table = table ? table->child : &head->ctl_table[1];

	p = lookup_entry(&h, head, table, name->name, name->len);
	if (!p)
		goto out;

	err = ERR_PTR(-ENOMEM);
	inode = proc_sys_make_inode(dir->i_sb, h ? h : head, p);
	if (h)
		sysctl_head_finish(h);

	if (!inode)
		goto out;

	err = NULL;
	d_set_d_op(dentry, &proc_sys_dentry_operations);
	d_add(dentry, inode);

out:
	sysctl_head_finish(head);
	return err;
}

static ssize_t proc_sys_call_handler(struct file *filp, void __user *buf,
		size_t count, loff_t *ppos, int write)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct ctl_table_header *head = grab_header(inode);
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;
	ssize_t error;
	size_t res;

	if (IS_ERR(head))
		return PTR_ERR(head);

	/*
	 * At this point we know that the sysctl was not unregistered
	 * and won't be until we finish.
	 */
	error = -EPERM;
	if (sysctl_perm(head->root, table, write ? MAY_WRITE : MAY_READ))
		goto out;

	/* if that can happen at all, it should be -EINVAL, not -EISDIR */
	error = -EINVAL;
	if (!table->proc_handler)
		goto out;

	/* careful: calling conventions are nasty here */
	res = count;
	error = table->proc_handler(table, write, buf, &res, ppos);
	if (!error)
		error = res;
out:
	sysctl_head_finish(head);

	return error;
}

static ssize_t proc_sys_read(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	return proc_sys_call_handler(filp, (void __user *)buf, count, ppos, 0);
}

static ssize_t proc_sys_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	return proc_sys_call_handler(filp, (void __user *)buf, count, ppos, 1);
}

static int proc_sys_open(struct inode *inode, struct file *filp)
{
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;

	if (table->poll)
		filp->private_data = proc_sys_poll_event(table->poll);

	return 0;
}

static unsigned int proc_sys_poll(struct file *filp, poll_table *wait)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;
	unsigned long event = (unsigned long)filp->private_data;
	unsigned int ret = DEFAULT_POLLMASK;

	if (!table->proc_handler)
		goto out;

	if (!table->poll)
		goto out;

	poll_wait(filp, &table->poll->wait, wait);

	if (event != atomic_read(&table->poll->event)) {
		filp->private_data = proc_sys_poll_event(table->poll);
		ret = POLLIN | POLLRDNORM | POLLERR | POLLPRI;
	}

out:
	return ret;
}

static int proc_sys_fill_cache(struct file *filp, void *dirent,
				filldir_t filldir,
				struct ctl_table_header *head,
				struct ctl_table *table)
{
	struct dentry *child, *dir = filp->f_path.dentry;
	struct inode *inode;
	struct qstr qname;
	ino_t ino = 0;
	unsigned type = DT_UNKNOWN;

	qname.name = table->procname;
	qname.len  = strlen(table->procname);
	qname.hash = full_name_hash(qname.name, qname.len);

	child = d_lookup(dir, &qname);
	if (!child) {
		child = d_alloc(dir, &qname);
		if (child) {
			inode = proc_sys_make_inode(dir->d_sb, head, table);
			if (!inode) {
				dput(child);
				return -ENOMEM;
			} else {
				d_set_d_op(child, &proc_sys_dentry_operations);
				d_add(child, inode);
			}
		} else {
			return -ENOMEM;
		}
	}
	inode = child->d_inode;
	ino  = inode->i_ino;
	type = inode->i_mode >> 12;
	dput(child);
	return !!filldir(dirent, qname.name, qname.len, filp->f_pos, ino, type);
}

static int scan(struct ctl_table_header *head, ctl_table *table,
		unsigned long *pos, struct file *file,
		void *dirent, filldir_t filldir)
{
	int res;

	if ((*pos)++ < file->f_pos)
		return 0;

	res = proc_sys_fill_cache(file, dirent, filldir, head, table);

	if (res == 0)
		file->f_pos = *pos;

	return res;
}

static int proc_sys_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	struct ctl_table_header *head = grab_header(inode);
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;
	struct ctl_table_header *h = NULL;
	struct ctl_table *entry;
	unsigned long pos;
	int ret = -EINVAL;

	if (IS_ERR(head))
		return PTR_ERR(head);

	if (table && !table->child) {
		WARN_ON(1);
		goto out;
	}

	table = table ? table->child : &head->ctl_table[1];

	ret = 0;
	/* Avoid a switch here: arm builds fail with missing __cmpdi2 */
	if (filp->f_pos == 0) {
		if (filldir(dirent, ".", 1, filp->f_pos,
				inode->i_ino, DT_DIR) < 0)
			goto out;
		filp->f_pos++;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent, "..", 2, filp->f_pos,
				parent_ino(dentry), DT_DIR) < 0)
			goto out;
		filp->f_pos++;
	}
	pos = 2;

	for (first_entry(head, table, &h, &entry); h; next_entry(table, &h, &entry)) {
		ret = scan(h, entry, &pos, filp, dirent, filldir);
		if (ret) {
			sysctl_head_finish(h);
			break;
		}
	}
	ret = 1;
out:
	sysctl_head_finish(head);
	return ret;
}

static int proc_sys_permission(struct inode *inode, int mask)
{
	/*
	 * sysctl entries that are not writeable,
	 * are _NOT_ writeable, capabilities or not.
	 */
	struct ctl_table_header *head;
	struct ctl_table *table;
	int error;

	/* Executable files are not allowed under /proc/sys/ */
	if ((mask & MAY_EXEC) && S_ISREG(inode->i_mode))
		return -EACCES;

	head = grab_header(inode);
	if (IS_ERR(head))
		return PTR_ERR(head);

	table = PROC_I(inode)->sysctl_entry;
	if (!table) /* global root - r-xr-xr-x */
		error = mask & MAY_WRITE ? -EACCES : 0;
	else /* Use the permissions on the sysctl table entry */
		error = sysctl_perm(head->root, table, mask & ~MAY_NOT_BLOCK);

	sysctl_head_finish(head);
	return error;
}

static int proc_sys_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	if (attr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID))
		return -EPERM;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = vmtruncate(inode, attr->ia_size);
		if (error)
			return error;
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

static int proc_sys_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct ctl_table_header *head = grab_header(inode);
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;

	if (IS_ERR(head))
		return PTR_ERR(head);

	generic_fillattr(inode, stat);
	if (table)
		stat->mode = (stat->mode & S_IFMT) | table->mode;

	sysctl_head_finish(head);
	return 0;
}

static const struct file_operations proc_sys_file_operations = {
	.open		= proc_sys_open,
	.poll		= proc_sys_poll,
	.read		= proc_sys_read,
	.write		= proc_sys_write,
	.llseek		= default_llseek,
};

static const struct file_operations proc_sys_dir_file_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_sys_readdir,
	.llseek		= generic_file_llseek,
};

static const struct inode_operations proc_sys_inode_operations = {
	.permission	= proc_sys_permission,
	.setattr	= proc_sys_setattr,
	.getattr	= proc_sys_getattr,
};

static const struct inode_operations proc_sys_dir_operations = {
	.lookup		= proc_sys_lookup,
	.permission	= proc_sys_permission,
	.setattr	= proc_sys_setattr,
	.getattr	= proc_sys_getattr,
};

static int proc_sys_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	if (nd->flags & LOOKUP_RCU)
		return -ECHILD;
	return !PROC_I(dentry->d_inode)->sysctl->unregistering;
}

static int proc_sys_delete(const struct dentry *dentry)
{
	return !!PROC_I(dentry->d_inode)->sysctl->unregistering;
}

static int sysctl_is_seen(struct ctl_table_header *p)
{
	struct ctl_table_set *set = p->set;
	int res;
	spin_lock(&sysctl_lock);
	if (p->unregistering)
		res = 0;
	else if (!set->is_seen)
		res = 1;
	else
		res = set->is_seen(set);
	spin_unlock(&sysctl_lock);
	return res;
}

static int proc_sys_compare(const struct dentry *parent,
		const struct inode *pinode,
		const struct dentry *dentry, const struct inode *inode,
		unsigned int len, const char *str, const struct qstr *name)
{
	struct ctl_table_header *head;
	/* Although proc doesn't have negative dentries, rcu-walk means
	 * that inode here can be NULL */
	/* AV: can it, indeed? */
	if (!inode)
		return 1;
	if (name->len != len)
		return 1;
	if (memcmp(name->name, str, len))
		return 1;
	head = rcu_dereference(PROC_I(inode)->sysctl);
	return !head || !sysctl_is_seen(head);
}

static const struct dentry_operations proc_sys_dentry_operations = {
	.d_revalidate	= proc_sys_revalidate,
	.d_delete	= proc_sys_delete,
	.d_compare	= proc_sys_compare,
};

static struct ctl_table *is_branch_in(struct ctl_table *branch,
				      struct ctl_table *table)
{
	struct ctl_table *p;
	const char *s = branch->procname;

	/* branch should have named subdirectory as its first element */
	if (!s || !branch->child)
		return NULL;

	/* ... and nothing else */
	if (branch[1].procname)
		return NULL;

	/* table should contain subdirectory with the same name */
	for (p = table; p->procname; p++) {
		if (!p->child)
			continue;
		if (p->procname && strcmp(p->procname, s) == 0)
			return p;
	}
	return NULL;
}

/* see if attaching q to p would be an improvement */
static void try_attach(struct ctl_table_header *p, struct ctl_table_header *q)
{
	struct ctl_table *to = p->ctl_table, *by = q->ctl_table;
	struct ctl_table *next;
	int is_better = 0;
	int not_in_parent = !p->attached_by;

	while ((next = is_branch_in(by, to)) != NULL) {
		if (by == q->attached_by)
			is_better = 1;
		if (to == p->attached_by)
			not_in_parent = 1;
		by = by->child;
		to = next->child;
	}

	if (is_better && not_in_parent) {
		q->attached_by = by;
		q->attached_to = to;
		q->parent = p;
	}
}

static int sysctl_check_table_dups(const char *path, struct ctl_table *old,
	struct ctl_table *table)
{
	struct ctl_table *entry, *test;
	int error = 0;

	for (entry = old; entry->procname; entry++) {
		for (test = table; test->procname; test++) {
			if (strcmp(entry->procname, test->procname) == 0) {
				printk(KERN_ERR "sysctl duplicate entry: %s/%s\n",
					path, test->procname);
				error = -EEXIST;
			}
		}
	}
	return error;
}

static int sysctl_check_dups(struct nsproxy *namespaces,
	struct ctl_table_header *header,
	const char *path, struct ctl_table *table)
{
	struct ctl_table_root *root;
	struct ctl_table_set *set;
	struct ctl_table_header *dir_head, *head;
	struct ctl_table *dir_table;
	int error = 0;

	/* No dups if we are the only member of our directory */
	if (header->attached_by != table)
		return 0;

	dir_head = header->parent;
	dir_table = header->attached_to;

	error = sysctl_check_table_dups(path, dir_table, table);

	root = &sysctl_table_root;
	do {
		set = lookup_header_set(root, namespaces);

		list_for_each_entry(head, &set->list, ctl_entry) {
			if (head->unregistering)
				continue;
			if (head->attached_to != dir_table)
				continue;
			error = sysctl_check_table_dups(path, head->attached_by,
							table);
		}
		root = list_entry(root->root_list.next,
				  struct ctl_table_root, root_list);
	} while (root != &sysctl_table_root);
	return error;
}

static int sysctl_err(const char *path, struct ctl_table *table, char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_ERR "sysctl table check failed: %s/%s %pV\n",
		path, table->procname, &vaf);

	va_end(args);
	return -EINVAL;
}

static int sysctl_check_table(const char *path, struct ctl_table *table)
{
	int err = 0;
	for (; table->procname; table++) {
		if (table->child)
			err = sysctl_err(path, table, "Not a file");

		if ((table->proc_handler == proc_dostring) ||
		    (table->proc_handler == proc_dointvec) ||
		    (table->proc_handler == proc_dointvec_minmax) ||
		    (table->proc_handler == proc_dointvec_jiffies) ||
		    (table->proc_handler == proc_dointvec_userhz_jiffies) ||
		    (table->proc_handler == proc_dointvec_ms_jiffies) ||
		    (table->proc_handler == proc_doulongvec_minmax) ||
		    (table->proc_handler == proc_doulongvec_ms_jiffies_minmax)) {
			if (!table->data)
				err = sysctl_err(path, table, "No data");
			if (!table->maxlen)
				err = sysctl_err(path, table, "No maxlen");
		}
		if (!table->proc_handler)
			err = sysctl_err(path, table, "No proc_handler");

		if ((table->mode & (S_IRUGO|S_IWUGO)) != table->mode)
			err = sysctl_err(path, table, "bogus .mode 0%o",
				table->mode);
	}
	return err;
}

/**
 * __register_sysctl_table - register a leaf sysctl table
 * @root: List of sysctl headers to register on
 * @namespaces: Data to compute which lists of sysctl entries are visible
 * @path: The path to the directory the sysctl table is in.
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * The members of the &struct ctl_table structure are used as follows:
 *
 * procname - the name of the sysctl file under /proc/sys. Set to %NULL to not
 *            enter a sysctl file
 *
 * data - a pointer to data for use by proc_handler
 *
 * maxlen - the maximum size in bytes of the data
 *
 * mode - the file permissions for the /proc/sys file
 *
 * child - must be %NULL.
 *
 * proc_handler - the text handler routine (described below)
 *
 * extra1, extra2 - extra pointers usable by the proc handler routines
 *
 * Leaf nodes in the sysctl tree will be represented by a single file
 * under /proc; non-leaf nodes will be represented by directories.
 *
 * There must be a proc_handler routine for any terminal nodes.
 * Several default handlers are available to cover common cases -
 *
 * proc_dostring(), proc_dointvec(), proc_dointvec_jiffies(),
 * proc_dointvec_userhz_jiffies(), proc_dointvec_minmax(),
 * proc_doulongvec_ms_jiffies_minmax(), proc_doulongvec_minmax()
 *
 * It is the handler's job to read the input buffer from user memory
 * and process it. The handler should return 0 on success.
 *
 * This routine returns %NULL on a failure to register, and a pointer
 * to the table header on success.
 */
struct ctl_table_header *__register_sysctl_table(
	struct ctl_table_root *root,
	struct nsproxy *namespaces,
	const char *path, struct ctl_table *table)
{
	struct ctl_table_header *header;
	struct ctl_table *new, **prevp;
	const char *name, *nextname;
	unsigned int npath = 0;
	struct ctl_table_set *set;
	size_t path_bytes = 0;
	char *new_name;

	/* Count the path components */
	for (name = path; name; name = nextname) {
		int namelen;
		nextname = strchr(name, '/');
		if (nextname) {
			namelen = nextname - name;
			nextname++;
		} else {
			namelen = strlen(name);
		}
		if (namelen == 0)
			continue;
		path_bytes += namelen + 1;
		npath++;
	}

	/*
	 * For each path component, allocate a 2-element ctl_table array.
	 * The first array element will be filled with the sysctl entry
	 * for this, the second will be the sentinel (procname == 0).
	 *
	 * We allocate everything in one go so that we don't have to
	 * worry about freeing additional memory in unregister_sysctl_table.
	 */
	header = kzalloc(sizeof(struct ctl_table_header) + path_bytes +
			 (2 * npath * sizeof(struct ctl_table)), GFP_KERNEL);
	if (!header)
		return NULL;

	new = (struct ctl_table *) (header + 1);
	new_name = (char *)(new + (2 * npath));

	/* Now connect the dots */
	prevp = &header->ctl_table;
	for (name = path; name; name = nextname) {
		int namelen;
		nextname = strchr(name, '/');
		if (nextname) {
			namelen = nextname - name;
			nextname++;
		} else {
			namelen = strlen(name);
		}
		if (namelen == 0)
			continue;
		memcpy(new_name, name, namelen);
		new_name[namelen] = '\0';

		new->procname = new_name;
		new->mode     = 0555;

		*prevp = new;
		prevp = &new->child;

		new += 2;
		new_name += namelen + 1;
	}
	*prevp = table;

	init_header(header, root, NULL, table);
	if (sysctl_check_table(path, table))
		goto fail;

	spin_lock(&sysctl_lock);
	header->set = lookup_header_set(root, namespaces);
	header->attached_by = header->ctl_table;
	header->attached_to = &root_table[1];
	header->parent = &root_table_header;
	set = header->set;
	root = header->root;
	for (;;) {
		struct ctl_table_header *p;
		list_for_each_entry(p, &set->list, ctl_entry) {
			if (p->unregistering)
				continue;
			try_attach(p, header);
		}
		if (root == &sysctl_table_root)
			break;
		root = list_entry(root->root_list.prev,
				  struct ctl_table_root, root_list);
		set = lookup_header_set(root, namespaces);
	}
	if (sysctl_check_dups(namespaces, header, path, table))
		goto fail_locked;
	insert_header(header);
	spin_unlock(&sysctl_lock);

	return header;
fail_locked:
	spin_unlock(&sysctl_lock);
fail:
	kfree(header);
	dump_stack();
	return NULL;
}

static char *append_path(const char *path, char *pos, const char *name)
{
	int namelen;
	namelen = strlen(name);
	if (((pos - path) + namelen + 2) >= PATH_MAX)
		return NULL;
	memcpy(pos, name, namelen);
	pos[namelen] = '/';
	pos[namelen + 1] = '\0';
	pos += namelen + 1;
	return pos;
}

static int count_subheaders(struct ctl_table *table)
{
	int has_files = 0;
	int nr_subheaders = 0;
	struct ctl_table *entry;

	/* special case: no directory and empty directory */
	if (!table || !table->procname)
		return 1;

	for (entry = table; entry->procname; entry++) {
		if (entry->child)
			nr_subheaders += count_subheaders(entry->child);
		else
			has_files = 1;
	}
	return nr_subheaders + has_files;
}

static int register_leaf_sysctl_tables(const char *path, char *pos,
	struct ctl_table_header ***subheader,
	struct ctl_table_root *root, struct nsproxy *namespaces,
	struct ctl_table *table)
{
	struct ctl_table *ctl_table_arg = NULL;
	struct ctl_table *entry, *files;
	int nr_files = 0;
	int nr_dirs = 0;
	int err = -ENOMEM;

	for (entry = table; entry->procname; entry++) {
		if (entry->child)
			nr_dirs++;
		else
			nr_files++;
	}

	files = table;
	/* If there are mixed files and directories we need a new table */
	if (nr_dirs && nr_files) {
		struct ctl_table *new;
		files = kzalloc(sizeof(struct ctl_table) * (nr_files + 1),
				GFP_KERNEL);
		if (!files)
			goto out;

		ctl_table_arg = files;
		for (new = files, entry = table; entry->procname; entry++) {
			if (entry->child)
				continue;
			*new = *entry;
			new++;
		}
	}

	/* Register everything except a directory full of subdirectories */
	if (nr_files || !nr_dirs) {
		struct ctl_table_header *header;
		header = __register_sysctl_table(root, namespaces, path, files);
		if (!header) {
			kfree(ctl_table_arg);
			goto out;
		}

		/* Remember if we need to free the file table */
		header->ctl_table_arg = ctl_table_arg;
		**subheader = header;
		(*subheader)++;
	}

	/* Recurse into the subdirectories. */
	for (entry = table; entry->procname; entry++) {
		char *child_pos;

		if (!entry->child)
			continue;

		err = -ENAMETOOLONG;
		child_pos = append_path(path, pos, entry->procname);
		if (!child_pos)
			goto out;

		err = register_leaf_sysctl_tables(path, child_pos, subheader,
						  root, namespaces, entry->child);
		pos[0] = '\0';
		if (err)
			goto out;
	}
	err = 0;
out:
	/* On failure our caller will unregister all registered subheaders */
	return err;
}

/**
 * __register_sysctl_paths - register a sysctl table hierarchy
 * @root: List of sysctl headers to register on
 * @namespaces: Data to compute which lists of sysctl entries are visible
 * @path: The path to the directory the sysctl table is in.
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * See __register_sysctl_table for more details.
 */
struct ctl_table_header *__register_sysctl_paths(
	struct ctl_table_root *root,
	struct nsproxy *namespaces,
	const struct ctl_path *path, struct ctl_table *table)
{
	struct ctl_table *ctl_table_arg = table;
	int nr_subheaders = count_subheaders(table);
	struct ctl_table_header *header = NULL, **subheaders, **subheader;
	const struct ctl_path *component;
	char *new_path, *pos;

	pos = new_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!new_path)
		return NULL;

	pos[0] = '\0';
	for (component = path; component->procname; component++) {
		pos = append_path(new_path, pos, component->procname);
		if (!pos)
			goto out;
	}
	while (table->procname && table->child && !table[1].procname) {
		pos = append_path(new_path, pos, table->procname);
		if (!pos)
			goto out;
		table = table->child;
	}
	if (nr_subheaders == 1) {
		header = __register_sysctl_table(root, namespaces, new_path, table);
		if (header)
			header->ctl_table_arg = ctl_table_arg;
	} else {
		header = kzalloc(sizeof(*header) +
				 sizeof(*subheaders)*nr_subheaders, GFP_KERNEL);
		if (!header)
			goto out;

		subheaders = (struct ctl_table_header **) (header + 1);
		subheader = subheaders;
		header->ctl_table_arg = ctl_table_arg;

		if (register_leaf_sysctl_tables(new_path, pos, &subheader,
						root, namespaces, table))
			goto err_register_leaves;
	}

out:
	kfree(new_path);
	return header;

err_register_leaves:
	while (subheader > subheaders) {
		struct ctl_table_header *subh = *(--subheader);
		struct ctl_table *table = subh->ctl_table_arg;
		unregister_sysctl_table(subh);
		kfree(table);
	}
	kfree(header);
	header = NULL;
	goto out;
}

/**
 * register_sysctl_table_path - register a sysctl table hierarchy
 * @path: The path to the directory the sysctl table is in.
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * See __register_sysctl_paths for more details.
 */
struct ctl_table_header *register_sysctl_paths(const struct ctl_path *path,
						struct ctl_table *table)
{
	return __register_sysctl_paths(&sysctl_table_root, current->nsproxy,
					path, table);
}
EXPORT_SYMBOL(register_sysctl_paths);

/**
 * register_sysctl_table - register a sysctl table hierarchy
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * See register_sysctl_paths for more details.
 */
struct ctl_table_header *register_sysctl_table(struct ctl_table *table)
{
	static const struct ctl_path null_path[] = { {} };

	return register_sysctl_paths(null_path, table);
}
EXPORT_SYMBOL(register_sysctl_table);

static void drop_sysctl_table(struct ctl_table_header *header)
{
	if (--header->nreg)
		return;

	start_unregistering(header);
	if (!--header->parent->count) {
		WARN_ON(1);
		kfree_rcu(header->parent, rcu);
	}
	if (!--header->count)
		kfree_rcu(header, rcu);
}

/**
 * unregister_sysctl_table - unregister a sysctl table hierarchy
 * @header: the header returned from register_sysctl_table
 *
 * Unregisters the sysctl table and all children. proc entries may not
 * actually be removed until they are no longer used by anyone.
 */
void unregister_sysctl_table(struct ctl_table_header * header)
{
	int nr_subheaders;
	might_sleep();

	if (header == NULL)
		return;

	nr_subheaders = count_subheaders(header->ctl_table_arg);
	if (unlikely(nr_subheaders > 1)) {
		struct ctl_table_header **subheaders;
		int i;

		subheaders = (struct ctl_table_header **)(header + 1);
		for (i = nr_subheaders -1; i >= 0; i--) {
			struct ctl_table_header *subh = subheaders[i];
			struct ctl_table *table = subh->ctl_table_arg;
			unregister_sysctl_table(subh);
			kfree(table);
		}
		kfree(header);
		return;
	}

	spin_lock(&sysctl_lock);
	drop_sysctl_table(header);
	spin_unlock(&sysctl_lock);
}
EXPORT_SYMBOL(unregister_sysctl_table);

void setup_sysctl_set(struct ctl_table_set *p,
	struct ctl_table_root *root,
	int (*is_seen)(struct ctl_table_set *))
{
	INIT_LIST_HEAD(&p->list);
	p->root = root;
	p->is_seen = is_seen;
}

void retire_sysctl_set(struct ctl_table_set *set)
{
	WARN_ON(!list_empty(&set->list));
}

int __init proc_sys_init(void)
{
	struct proc_dir_entry *proc_sys_root;

	proc_sys_root = proc_mkdir("sys", NULL);
	proc_sys_root->proc_iops = &proc_sys_dir_operations;
	proc_sys_root->proc_fops = &proc_sys_dir_file_operations;
	proc_sys_root->nlink = 0;

	return sysctl_init();
}
