#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/swap.h>
#include <linux/version.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include "extent_io.h"
#include "extent_map.h"

/* temporary define until extent_map moves out of btrfs */
struct kmem_cache *btrfs_cache_create(const char *name, size_t size,
				       unsigned long extra_flags,
				       void (*ctor)(void *, struct kmem_cache *,
						    unsigned long));

static struct kmem_cache *extent_state_cache;
static struct kmem_cache *extent_buffer_cache;

static LIST_HEAD(buffers);
static LIST_HEAD(states);
static spinlock_t leak_lock = SPIN_LOCK_UNLOCKED;

#define BUFFER_LRU_MAX 64

struct tree_entry {
	u64 start;
	u64 end;
	struct rb_node rb_node;
};

struct extent_page_data {
	struct bio *bio;
	struct extent_io_tree *tree;
	get_extent_t *get_extent;
};

int __init extent_io_init(void)
{
	extent_state_cache = btrfs_cache_create("extent_state",
					    sizeof(struct extent_state), 0,
					    NULL);
	if (!extent_state_cache)
		return -ENOMEM;

	extent_buffer_cache = btrfs_cache_create("extent_buffers",
					    sizeof(struct extent_buffer), 0,
					    NULL);
	if (!extent_buffer_cache)
		goto free_state_cache;
	return 0;

free_state_cache:
	kmem_cache_destroy(extent_state_cache);
	return -ENOMEM;
}

void extent_io_exit(void)
{
	struct extent_state *state;
	struct extent_buffer *eb;

	while (!list_empty(&states)) {
		state = list_entry(states.next, struct extent_state, leak_list);
		printk("state leak: start %Lu end %Lu state %lu in tree %p refs %d\n", state->start, state->end, state->state, state->tree, atomic_read(&state->refs));
		list_del(&state->leak_list);
		kmem_cache_free(extent_state_cache, state);

	}

	while (!list_empty(&buffers)) {
		eb = list_entry(buffers.next, struct extent_buffer, leak_list);
		printk("buffer leak start %Lu len %lu refs %d\n", eb->start, eb->len, atomic_read(&eb->refs));
		list_del(&eb->leak_list);
		kmem_cache_free(extent_buffer_cache, eb);
	}
	if (extent_state_cache)
		kmem_cache_destroy(extent_state_cache);
	if (extent_buffer_cache)
		kmem_cache_destroy(extent_buffer_cache);
}

void extent_io_tree_init(struct extent_io_tree *tree,
			  struct address_space *mapping, gfp_t mask)
{
	tree->state.rb_node = NULL;
	tree->ops = NULL;
	tree->dirty_bytes = 0;
	spin_lock_init(&tree->lock);
	spin_lock_init(&tree->lru_lock);
	tree->mapping = mapping;
	INIT_LIST_HEAD(&tree->buffer_lru);
	tree->lru_size = 0;
	tree->last = NULL;
}
EXPORT_SYMBOL(extent_io_tree_init);

void extent_io_tree_empty_lru(struct extent_io_tree *tree)
{
	struct extent_buffer *eb;
	while(!list_empty(&tree->buffer_lru)) {
		eb = list_entry(tree->buffer_lru.next, struct extent_buffer,
				lru);
		list_del_init(&eb->lru);
		free_extent_buffer(eb);
	}
}
EXPORT_SYMBOL(extent_io_tree_empty_lru);

struct extent_state *alloc_extent_state(gfp_t mask)
{
	struct extent_state *state;
	unsigned long flags;

	state = kmem_cache_alloc(extent_state_cache, mask);
	if (!state)
		return state;
	state->state = 0;
	state->private = 0;
	state->tree = NULL;
	spin_lock_irqsave(&leak_lock, flags);
	list_add(&state->leak_list, &states);
	spin_unlock_irqrestore(&leak_lock, flags);

	atomic_set(&state->refs, 1);
	init_waitqueue_head(&state->wq);
	return state;
}
EXPORT_SYMBOL(alloc_extent_state);

void free_extent_state(struct extent_state *state)
{
	if (!state)
		return;
	if (atomic_dec_and_test(&state->refs)) {
		unsigned long flags;
		WARN_ON(state->tree);
		spin_lock_irqsave(&leak_lock, flags);
		list_del(&state->leak_list);
		spin_unlock_irqrestore(&leak_lock, flags);
		kmem_cache_free(extent_state_cache, state);
	}
}
EXPORT_SYMBOL(free_extent_state);

static struct rb_node *tree_insert(struct rb_root *root, u64 offset,
				   struct rb_node *node)
{
	struct rb_node ** p = &root->rb_node;
	struct rb_node * parent = NULL;
	struct tree_entry *entry;

	while(*p) {
		parent = *p;
		entry = rb_entry(parent, struct tree_entry, rb_node);

		if (offset < entry->start)
			p = &(*p)->rb_left;
		else if (offset > entry->end)
			p = &(*p)->rb_right;
		else
			return parent;
	}

	entry = rb_entry(node, struct tree_entry, rb_node);
	rb_link_node(node, parent, p);
	rb_insert_color(node, root);
	return NULL;
}

static struct rb_node *__etree_search(struct extent_io_tree *tree, u64 offset,
				     struct rb_node **prev_ret,
				     struct rb_node **next_ret)
{
	struct rb_root *root = &tree->state;
	struct rb_node * n = root->rb_node;
	struct rb_node *prev = NULL;
	struct rb_node *orig_prev = NULL;
	struct tree_entry *entry;
	struct tree_entry *prev_entry = NULL;

	if (tree->last) {
		struct extent_state *state;
		state = tree->last;
		if (state->start <= offset && offset <= state->end)
			return &tree->last->rb_node;
	}
	while(n) {
		entry = rb_entry(n, struct tree_entry, rb_node);
		prev = n;
		prev_entry = entry;

		if (offset < entry->start)
			n = n->rb_left;
		else if (offset > entry->end)
			n = n->rb_right;
		else {
			tree->last = rb_entry(n, struct extent_state, rb_node);
			return n;
		}
	}

	if (prev_ret) {
		orig_prev = prev;
		while(prev && offset > prev_entry->end) {
			prev = rb_next(prev);
			prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		}
		*prev_ret = prev;
		prev = orig_prev;
	}

	if (next_ret) {
		prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		while(prev && offset < prev_entry->start) {
			prev = rb_prev(prev);
			prev_entry = rb_entry(prev, struct tree_entry, rb_node);
		}
		*next_ret = prev;
	}
	return NULL;
}

static inline struct rb_node *tree_search(struct extent_io_tree *tree,
					  u64 offset)
{
	struct rb_node *prev = NULL;
	struct rb_node *ret;

	ret = __etree_search(tree, offset, &prev, NULL);
	if (!ret) {
		if (prev) {
			tree->last = rb_entry(prev, struct extent_state,
					      rb_node);
		}
		return prev;
	}
	return ret;
}

/*
 * utility function to look for merge candidates inside a given range.
 * Any extents with matching state are merged together into a single
 * extent in the tree.  Extents with EXTENT_IO in their state field
 * are not merged because the end_io handlers need to be able to do
 * operations on them without sleeping (or doing allocations/splits).
 *
 * This should be called with the tree lock held.
 */
static int merge_state(struct extent_io_tree *tree,
		       struct extent_state *state)
{
	struct extent_state *other;
	struct rb_node *other_node;

	if (state->state & EXTENT_IOBITS)
		return 0;

	other_node = rb_prev(&state->rb_node);
	if (other_node) {
		other = rb_entry(other_node, struct extent_state, rb_node);
		if (other->end == state->start - 1 &&
		    other->state == state->state) {
			state->start = other->start;
			other->tree = NULL;
			if (tree->last == other)
				tree->last = state;
			rb_erase(&other->rb_node, &tree->state);
			free_extent_state(other);
		}
	}
	other_node = rb_next(&state->rb_node);
	if (other_node) {
		other = rb_entry(other_node, struct extent_state, rb_node);
		if (other->start == state->end + 1 &&
		    other->state == state->state) {
			other->start = state->start;
			state->tree = NULL;
			if (tree->last == state)
				tree->last = other;
			rb_erase(&state->rb_node, &tree->state);
			free_extent_state(state);
		}
	}
	return 0;
}

static void set_state_cb(struct extent_io_tree *tree,
			 struct extent_state *state,
			 unsigned long bits)
{
	if (tree->ops && tree->ops->set_bit_hook) {
		tree->ops->set_bit_hook(tree->mapping->host, state->start,
					state->end, state->state, bits);
	}
}

static void clear_state_cb(struct extent_io_tree *tree,
			   struct extent_state *state,
			   unsigned long bits)
{
	if (tree->ops && tree->ops->set_bit_hook) {
		tree->ops->clear_bit_hook(tree->mapping->host, state->start,
					  state->end, state->state, bits);
	}
}

/*
 * insert an extent_state struct into the tree.  'bits' are set on the
 * struct before it is inserted.
 *
 * This may return -EEXIST if the extent is already there, in which case the
 * state struct is freed.
 *
 * The tree lock is not taken internally.  This is a utility function and
 * probably isn't what you want to call (see set/clear_extent_bit).
 */
static int insert_state(struct extent_io_tree *tree,
			struct extent_state *state, u64 start, u64 end,
			int bits)
{
	struct rb_node *node;

	if (end < start) {
		printk("end < start %Lu %Lu\n", end, start);
		WARN_ON(1);
	}
	if (bits & EXTENT_DIRTY)
		tree->dirty_bytes += end - start + 1;
	set_state_cb(tree, state, bits);
	state->state |= bits;
	state->start = start;
	state->end = end;
	node = tree_insert(&tree->state, end, &state->rb_node);
	if (node) {
		struct extent_state *found;
		found = rb_entry(node, struct extent_state, rb_node);
		printk("found node %Lu %Lu on insert of %Lu %Lu\n", found->start, found->end, start, end);
		free_extent_state(state);
		return -EEXIST;
	}
	state->tree = tree;
	tree->last = state;
	merge_state(tree, state);
	return 0;
}

/*
 * split a given extent state struct in two, inserting the preallocated
 * struct 'prealloc' as the newly created second half.  'split' indicates an
 * offset inside 'orig' where it should be split.
 *
 * Before calling,
 * the tree has 'orig' at [orig->start, orig->end].  After calling, there
 * are two extent state structs in the tree:
 * prealloc: [orig->start, split - 1]
 * orig: [ split, orig->end ]
 *
 * The tree locks are not taken by this function. They need to be held
 * by the caller.
 */
static int split_state(struct extent_io_tree *tree, struct extent_state *orig,
		       struct extent_state *prealloc, u64 split)
{
	struct rb_node *node;
	prealloc->start = orig->start;
	prealloc->end = split - 1;
	prealloc->state = orig->state;
	orig->start = split;

	node = tree_insert(&tree->state, prealloc->end, &prealloc->rb_node);
	if (node) {
		struct extent_state *found;
		found = rb_entry(node, struct extent_state, rb_node);
		printk("found node %Lu %Lu on insert of %Lu %Lu\n", found->start, found->end, prealloc->start, prealloc->end);
		free_extent_state(prealloc);
		return -EEXIST;
	}
	prealloc->tree = tree;
	return 0;
}

/*
 * utility function to clear some bits in an extent state struct.
 * it will optionally wake up any one waiting on this state (wake == 1), or
 * forcibly remove the state from the tree (delete == 1).
 *
 * If no bits are set on the state struct after clearing things, the
 * struct is freed and removed from the tree
 */
static int clear_state_bit(struct extent_io_tree *tree,
			    struct extent_state *state, int bits, int wake,
			    int delete)
{
	int ret = state->state & bits;

	if ((bits & EXTENT_DIRTY) && (state->state & EXTENT_DIRTY)) {
		u64 range = state->end - state->start + 1;
		WARN_ON(range > tree->dirty_bytes);
		tree->dirty_bytes -= range;
	}
	clear_state_cb(tree, state, bits);
	state->state &= ~bits;
	if (wake)
		wake_up(&state->wq);
	if (delete || state->state == 0) {
		if (state->tree) {
			clear_state_cb(tree, state, state->state);
			if (tree->last == state) {
				tree->last = extent_state_next(state);
			}
			rb_erase(&state->rb_node, &tree->state);
			state->tree = NULL;
			free_extent_state(state);
		} else {
			WARN_ON(1);
		}
	} else {
		merge_state(tree, state);
	}
	return ret;
}

/*
 * clear some bits on a range in the tree.  This may require splitting
 * or inserting elements in the tree, so the gfp mask is used to
 * indicate which allocations or sleeping are allowed.
 *
 * pass 'wake' == 1 to kick any sleepers, and 'delete' == 1 to remove
 * the given range from the tree regardless of state (ie for truncate).
 *
 * the range [start, end] is inclusive.
 *
 * This takes the tree lock, and returns < 0 on error, > 0 if any of the
 * bits were already set, or zero if none of the bits were already set.
 */
int clear_extent_bit(struct extent_io_tree *tree, u64 start, u64 end,
		     int bits, int wake, int delete, gfp_t mask)
{
	struct extent_state *state;
	struct extent_state *prealloc = NULL;
	struct rb_node *node;
	unsigned long flags;
	int err;
	int set = 0;

again:
	if (!prealloc && (mask & __GFP_WAIT)) {
		prealloc = alloc_extent_state(mask);
		if (!prealloc)
			return -ENOMEM;
	}

	spin_lock_irqsave(&tree->lock, flags);
	/*
	 * this search will find the extents that end after
	 * our range starts
	 */
	node = tree_search(tree, start);
	if (!node)
		goto out;
	state = rb_entry(node, struct extent_state, rb_node);
	if (state->start > end)
		goto out;
	WARN_ON(state->end < start);

	/*
	 *     | ---- desired range ---- |
	 *  | state | or
	 *  | ------------- state -------------- |
	 *
	 * We need to split the extent we found, and may flip
	 * bits on second half.
	 *
	 * If the extent we found extends past our range, we
	 * just split and search again.  It'll get split again
	 * the next time though.
	 *
	 * If the extent we found is inside our range, we clear
	 * the desired bit on it.
	 */

	if (state->start < start) {
		if (!prealloc)
			prealloc = alloc_extent_state(GFP_ATOMIC);
		err = split_state(tree, state, prealloc, start);
		BUG_ON(err == -EEXIST);
		prealloc = NULL;
		if (err)
			goto out;
		if (state->end <= end) {
			start = state->end + 1;
			set |= clear_state_bit(tree, state, bits,
					wake, delete);
		} else {
			start = state->start;
		}
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *                        | state |
	 * We need to split the extent, and clear the bit
	 * on the first half
	 */
	if (state->start <= end && state->end > end) {
		if (!prealloc)
			prealloc = alloc_extent_state(GFP_ATOMIC);
		err = split_state(tree, state, prealloc, end + 1);
		BUG_ON(err == -EEXIST);

		if (wake)
			wake_up(&state->wq);
		set |= clear_state_bit(tree, prealloc, bits,
				       wake, delete);
		prealloc = NULL;
		goto out;
	}

	start = state->end + 1;
	set |= clear_state_bit(tree, state, bits, wake, delete);
	goto search_again;

out:
	spin_unlock_irqrestore(&tree->lock, flags);
	if (prealloc)
		free_extent_state(prealloc);

	return set;

search_again:
	if (start > end)
		goto out;
	spin_unlock_irqrestore(&tree->lock, flags);
	if (mask & __GFP_WAIT)
		cond_resched();
	goto again;
}
EXPORT_SYMBOL(clear_extent_bit);

static int wait_on_state(struct extent_io_tree *tree,
			 struct extent_state *state)
{
	DEFINE_WAIT(wait);
	prepare_to_wait(&state->wq, &wait, TASK_UNINTERRUPTIBLE);
	spin_unlock_irq(&tree->lock);
	schedule();
	spin_lock_irq(&tree->lock);
	finish_wait(&state->wq, &wait);
	return 0;
}

/*
 * waits for one or more bits to clear on a range in the state tree.
 * The range [start, end] is inclusive.
 * The tree lock is taken by this function
 */
int wait_extent_bit(struct extent_io_tree *tree, u64 start, u64 end, int bits)
{
	struct extent_state *state;
	struct rb_node *node;

	spin_lock_irq(&tree->lock);
again:
	while (1) {
		/*
		 * this search will find all the extents that end after
		 * our range starts
		 */
		node = tree_search(tree, start);
		if (!node)
			break;

		state = rb_entry(node, struct extent_state, rb_node);

		if (state->start > end)
			goto out;

		if (state->state & bits) {
			start = state->start;
			atomic_inc(&state->refs);
			wait_on_state(tree, state);
			free_extent_state(state);
			goto again;
		}
		start = state->end + 1;

		if (start > end)
			break;

		if (need_resched()) {
			spin_unlock_irq(&tree->lock);
			cond_resched();
			spin_lock_irq(&tree->lock);
		}
	}
out:
	spin_unlock_irq(&tree->lock);
	return 0;
}
EXPORT_SYMBOL(wait_extent_bit);

static void set_state_bits(struct extent_io_tree *tree,
			   struct extent_state *state,
			   int bits)
{
	if ((bits & EXTENT_DIRTY) && !(state->state & EXTENT_DIRTY)) {
		u64 range = state->end - state->start + 1;
		tree->dirty_bytes += range;
	}
	set_state_cb(tree, state, bits);
	state->state |= bits;
}

/*
 * set some bits on a range in the tree.  This may require allocations
 * or sleeping, so the gfp mask is used to indicate what is allowed.
 *
 * If 'exclusive' == 1, this will fail with -EEXIST if some part of the
 * range already has the desired bits set.  The start of the existing
 * range is returned in failed_start in this case.
 *
 * [start, end] is inclusive
 * This takes the tree lock.
 */
int set_extent_bit(struct extent_io_tree *tree, u64 start, u64 end, int bits,
		   int exclusive, u64 *failed_start, gfp_t mask)
{
	struct extent_state *state;
	struct extent_state *prealloc = NULL;
	struct rb_node *node;
	unsigned long flags;
	int err = 0;
	int set;
	u64 last_start;
	u64 last_end;
again:
	if (!prealloc && (mask & __GFP_WAIT)) {
		prealloc = alloc_extent_state(mask);
		if (!prealloc)
			return -ENOMEM;
	}

	spin_lock_irqsave(&tree->lock, flags);
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, start);
	if (!node) {
		err = insert_state(tree, prealloc, start, end, bits);
		prealloc = NULL;
		BUG_ON(err == -EEXIST);
		goto out;
	}

	state = rb_entry(node, struct extent_state, rb_node);
	last_start = state->start;
	last_end = state->end;

	/*
	 * | ---- desired range ---- |
	 * | state |
	 *
	 * Just lock what we found and keep going
	 */
	if (state->start == start && state->end <= end) {
		set = state->state & bits;
		if (set && exclusive) {
			*failed_start = state->start;
			err = -EEXIST;
			goto out;
		}
		set_state_bits(tree, state, bits);
		start = state->end + 1;
		merge_state(tree, state);
		goto search_again;
	}

	/*
	 *     | ---- desired range ---- |
	 * | state |
	 *   or
	 * | ------------- state -------------- |
	 *
	 * We need to split the extent we found, and may flip bits on
	 * second half.
	 *
	 * If the extent we found extends past our
	 * range, we just split and search again.  It'll get split
	 * again the next time though.
	 *
	 * If the extent we found is inside our range, we set the
	 * desired bit on it.
	 */
	if (state->start < start) {
		set = state->state & bits;
		if (exclusive && set) {
			*failed_start = start;
			err = -EEXIST;
			goto out;
		}
		err = split_state(tree, state, prealloc, start);
		BUG_ON(err == -EEXIST);
		prealloc = NULL;
		if (err)
			goto out;
		if (state->end <= end) {
			set_state_bits(tree, state, bits);
			start = state->end + 1;
			merge_state(tree, state);
		} else {
			start = state->start;
		}
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *     | state | or               | state |
	 *
	 * There's a hole, we need to insert something in it and
	 * ignore the extent we found.
	 */
	if (state->start > start) {
		u64 this_end;
		if (end < last_start)
			this_end = end;
		else
			this_end = last_start -1;
		err = insert_state(tree, prealloc, start, this_end,
				   bits);
		prealloc = NULL;
		BUG_ON(err == -EEXIST);
		if (err)
			goto out;
		start = this_end + 1;
		goto search_again;
	}
	/*
	 * | ---- desired range ---- |
	 *                        | state |
	 * We need to split the extent, and set the bit
	 * on the first half
	 */
	if (state->start <= end && state->end > end) {
		set = state->state & bits;
		if (exclusive && set) {
			*failed_start = start;
			err = -EEXIST;
			goto out;
		}
		err = split_state(tree, state, prealloc, end + 1);
		BUG_ON(err == -EEXIST);

		set_state_bits(tree, prealloc, bits);
		merge_state(tree, prealloc);
		prealloc = NULL;
		goto out;
	}

	goto search_again;

out:
	spin_unlock_irqrestore(&tree->lock, flags);
	if (prealloc)
		free_extent_state(prealloc);

	return err;

search_again:
	if (start > end)
		goto out;
	spin_unlock_irqrestore(&tree->lock, flags);
	if (mask & __GFP_WAIT)
		cond_resched();
	goto again;
}
EXPORT_SYMBOL(set_extent_bit);

/* wrappers around set/clear extent bit */
int set_extent_dirty(struct extent_io_tree *tree, u64 start, u64 end,
		     gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_DIRTY, 0, NULL,
			      mask);
}
EXPORT_SYMBOL(set_extent_dirty);

int set_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		    int bits, gfp_t mask)
{
	return set_extent_bit(tree, start, end, bits, 0, NULL,
			      mask);
}
EXPORT_SYMBOL(set_extent_bits);

int clear_extent_bits(struct extent_io_tree *tree, u64 start, u64 end,
		      int bits, gfp_t mask)
{
	return clear_extent_bit(tree, start, end, bits, 0, 0, mask);
}
EXPORT_SYMBOL(clear_extent_bits);

int set_extent_delalloc(struct extent_io_tree *tree, u64 start, u64 end,
		     gfp_t mask)
{
	return set_extent_bit(tree, start, end,
			      EXTENT_DELALLOC | EXTENT_DIRTY, 0, NULL,
			      mask);
}
EXPORT_SYMBOL(set_extent_delalloc);

int clear_extent_dirty(struct extent_io_tree *tree, u64 start, u64 end,
		       gfp_t mask)
{
	return clear_extent_bit(tree, start, end,
				EXTENT_DIRTY | EXTENT_DELALLOC, 0, 0, mask);
}
EXPORT_SYMBOL(clear_extent_dirty);

int set_extent_new(struct extent_io_tree *tree, u64 start, u64 end,
		     gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_NEW, 0, NULL,
			      mask);
}
EXPORT_SYMBOL(set_extent_new);

int clear_extent_new(struct extent_io_tree *tree, u64 start, u64 end,
		       gfp_t mask)
{
	return clear_extent_bit(tree, start, end, EXTENT_NEW, 0, 0, mask);
}
EXPORT_SYMBOL(clear_extent_new);

int set_extent_uptodate(struct extent_io_tree *tree, u64 start, u64 end,
			gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_UPTODATE, 0, NULL,
			      mask);
}
EXPORT_SYMBOL(set_extent_uptodate);

int clear_extent_uptodate(struct extent_io_tree *tree, u64 start, u64 end,
			  gfp_t mask)
{
	return clear_extent_bit(tree, start, end, EXTENT_UPTODATE, 0, 0, mask);
}
EXPORT_SYMBOL(clear_extent_uptodate);

int set_extent_writeback(struct extent_io_tree *tree, u64 start, u64 end,
			 gfp_t mask)
{
	return set_extent_bit(tree, start, end, EXTENT_WRITEBACK,
			      0, NULL, mask);
}
EXPORT_SYMBOL(set_extent_writeback);

int clear_extent_writeback(struct extent_io_tree *tree, u64 start, u64 end,
			   gfp_t mask)
{
	return clear_extent_bit(tree, start, end, EXTENT_WRITEBACK, 1, 0, mask);
}
EXPORT_SYMBOL(clear_extent_writeback);

int wait_on_extent_writeback(struct extent_io_tree *tree, u64 start, u64 end)
{
	return wait_extent_bit(tree, start, end, EXTENT_WRITEBACK);
}
EXPORT_SYMBOL(wait_on_extent_writeback);

int lock_extent(struct extent_io_tree *tree, u64 start, u64 end, gfp_t mask)
{
	int err;
	u64 failed_start;
	while (1) {
		err = set_extent_bit(tree, start, end, EXTENT_LOCKED, 1,
				     &failed_start, mask);
		if (err == -EEXIST && (mask & __GFP_WAIT)) {
			wait_extent_bit(tree, failed_start, end, EXTENT_LOCKED);
			start = failed_start;
		} else {
			break;
		}
		WARN_ON(start > end);
	}
	return err;
}
EXPORT_SYMBOL(lock_extent);

int unlock_extent(struct extent_io_tree *tree, u64 start, u64 end,
		  gfp_t mask)
{
	return clear_extent_bit(tree, start, end, EXTENT_LOCKED, 1, 0, mask);
}
EXPORT_SYMBOL(unlock_extent);

/*
 * helper function to set pages and extents in the tree dirty
 */
int set_range_dirty(struct extent_io_tree *tree, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(tree->mapping, index);
		BUG_ON(!page);
		__set_page_dirty_nobuffers(page);
		page_cache_release(page);
		index++;
	}
	set_extent_dirty(tree, start, end, GFP_NOFS);
	return 0;
}
EXPORT_SYMBOL(set_range_dirty);

/*
 * helper function to set both pages and extents in the tree writeback
 */
int set_range_writeback(struct extent_io_tree *tree, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(tree->mapping, index);
		BUG_ON(!page);
		set_page_writeback(page);
		page_cache_release(page);
		index++;
	}
	set_extent_writeback(tree, start, end, GFP_NOFS);
	return 0;
}
EXPORT_SYMBOL(set_range_writeback);

int find_first_extent_bit(struct extent_io_tree *tree, u64 start,
			  u64 *start_ret, u64 *end_ret, int bits)
{
	struct rb_node *node;
	struct extent_state *state;
	int ret = 1;

	spin_lock_irq(&tree->lock);
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, start);
	if (!node) {
		goto out;
	}

	while(1) {
		state = rb_entry(node, struct extent_state, rb_node);
		if (state->end >= start && (state->state & bits)) {
			*start_ret = state->start;
			*end_ret = state->end;
			ret = 0;
			break;
		}
		node = rb_next(node);
		if (!node)
			break;
	}
out:
	spin_unlock_irq(&tree->lock);
	return ret;
}
EXPORT_SYMBOL(find_first_extent_bit);

struct extent_state *find_first_extent_bit_state(struct extent_io_tree *tree,
						 u64 start, int bits)
{
	struct rb_node *node;
	struct extent_state *state;

	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, start);
	if (!node) {
		goto out;
	}

	while(1) {
		state = rb_entry(node, struct extent_state, rb_node);
		if (state->end >= start && (state->state & bits)) {
			return state;
		}
		node = rb_next(node);
		if (!node)
			break;
	}
out:
	return NULL;
}
EXPORT_SYMBOL(find_first_extent_bit_state);

u64 find_lock_delalloc_range(struct extent_io_tree *tree,
			     u64 *start, u64 *end, u64 max_bytes)
{
	struct rb_node *node;
	struct extent_state *state;
	u64 cur_start = *start;
	u64 found = 0;
	u64 total_bytes = 0;

	spin_lock_irq(&tree->lock);
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
search_again:
	node = tree_search(tree, cur_start);
	if (!node) {
		if (!found)
			*end = (u64)-1;
		goto out;
	}

	while(1) {
		state = rb_entry(node, struct extent_state, rb_node);
		if (found && state->start != cur_start) {
			goto out;
		}
		if (!(state->state & EXTENT_DELALLOC)) {
			if (!found)
				*end = state->end;
			goto out;
		}
		if (!found) {
			struct extent_state *prev_state;
			struct rb_node *prev_node = node;
			while(1) {
				prev_node = rb_prev(prev_node);
				if (!prev_node)
					break;
				prev_state = rb_entry(prev_node,
						      struct extent_state,
						      rb_node);
				if (!(prev_state->state & EXTENT_DELALLOC))
					break;
				state = prev_state;
				node = prev_node;
			}
		}
		if (state->state & EXTENT_LOCKED) {
			DEFINE_WAIT(wait);
			atomic_inc(&state->refs);
			prepare_to_wait(&state->wq, &wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&tree->lock);
			schedule();
			spin_lock_irq(&tree->lock);
			finish_wait(&state->wq, &wait);
			free_extent_state(state);
			goto search_again;
		}
		set_state_cb(tree, state, EXTENT_LOCKED);
		state->state |= EXTENT_LOCKED;
		if (!found)
			*start = state->start;
		found++;
		*end = state->end;
		cur_start = state->end + 1;
		node = rb_next(node);
		if (!node)
			break;
		total_bytes += state->end - state->start + 1;
		if (total_bytes >= max_bytes)
			break;
	}
out:
	spin_unlock_irq(&tree->lock);
	return found;
}

u64 count_range_bits(struct extent_io_tree *tree,
		     u64 *start, u64 search_end, u64 max_bytes,
		     unsigned long bits)
{
	struct rb_node *node;
	struct extent_state *state;
	u64 cur_start = *start;
	u64 total_bytes = 0;
	int found = 0;

	if (search_end <= cur_start) {
		printk("search_end %Lu start %Lu\n", search_end, cur_start);
		WARN_ON(1);
		return 0;
	}

	spin_lock_irq(&tree->lock);
	if (cur_start == 0 && bits == EXTENT_DIRTY) {
		total_bytes = tree->dirty_bytes;
		goto out;
	}
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, cur_start);
	if (!node) {
		goto out;
	}

	while(1) {
		state = rb_entry(node, struct extent_state, rb_node);
		if (state->start > search_end)
			break;
		if (state->end >= cur_start && (state->state & bits)) {
			total_bytes += min(search_end, state->end) + 1 -
				       max(cur_start, state->start);
			if (total_bytes >= max_bytes)
				break;
			if (!found) {
				*start = state->start;
				found = 1;
			}
		}
		node = rb_next(node);
		if (!node)
			break;
	}
out:
	spin_unlock_irq(&tree->lock);
	return total_bytes;
}
/*
 * helper function to lock both pages and extents in the tree.
 * pages must be locked first.
 */
int lock_range(struct extent_io_tree *tree, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	struct page *page;
	int err;

	while (index <= end_index) {
		page = grab_cache_page(tree->mapping, index);
		if (!page) {
			err = -ENOMEM;
			goto failed;
		}
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto failed;
		}
		index++;
	}
	lock_extent(tree, start, end, GFP_NOFS);
	return 0;

failed:
	/*
	 * we failed above in getting the page at 'index', so we undo here
	 * up to but not including the page at 'index'
	 */
	end_index = index;
	index = start >> PAGE_CACHE_SHIFT;
	while (index < end_index) {
		page = find_get_page(tree->mapping, index);
		unlock_page(page);
		page_cache_release(page);
		index++;
	}
	return err;
}
EXPORT_SYMBOL(lock_range);

/*
 * helper function to unlock both pages and extents in the tree.
 */
int unlock_range(struct extent_io_tree *tree, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = end >> PAGE_CACHE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(tree->mapping, index);
		unlock_page(page);
		page_cache_release(page);
		index++;
	}
	unlock_extent(tree, start, end, GFP_NOFS);
	return 0;
}
EXPORT_SYMBOL(unlock_range);

int set_state_private(struct extent_io_tree *tree, u64 start, u64 private)
{
	struct rb_node *node;
	struct extent_state *state;
	int ret = 0;

	spin_lock_irq(&tree->lock);
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, start);
	if (!node) {
		ret = -ENOENT;
		goto out;
	}
	state = rb_entry(node, struct extent_state, rb_node);
	if (state->start != start) {
		ret = -ENOENT;
		goto out;
	}
	state->private = private;
out:
	spin_unlock_irq(&tree->lock);
	return ret;
}

int get_state_private(struct extent_io_tree *tree, u64 start, u64 *private)
{
	struct rb_node *node;
	struct extent_state *state;
	int ret = 0;

	spin_lock_irq(&tree->lock);
	/*
	 * this search will find all the extents that end after
	 * our range starts.
	 */
	node = tree_search(tree, start);
	if (!node) {
		ret = -ENOENT;
		goto out;
	}
	state = rb_entry(node, struct extent_state, rb_node);
	if (state->start != start) {
		ret = -ENOENT;
		goto out;
	}
	*private = state->private;
out:
	spin_unlock_irq(&tree->lock);
	return ret;
}

/*
 * searches a range in the state tree for a given mask.
 * If 'filled' == 1, this returns 1 only if every extent in the tree
 * has the bits set.  Otherwise, 1 is returned if any bit in the
 * range is found set.
 */
int test_range_bit(struct extent_io_tree *tree, u64 start, u64 end,
		   int bits, int filled)
{
	struct extent_state *state = NULL;
	struct rb_node *node;
	int bitset = 0;
	unsigned long flags;

	spin_lock_irqsave(&tree->lock, flags);
	node = tree_search(tree, start);
	while (node && start <= end) {
		state = rb_entry(node, struct extent_state, rb_node);

		if (filled && state->start > start) {
			bitset = 0;
			break;
		}

		if (state->start > end)
			break;

		if (state->state & bits) {
			bitset = 1;
			if (!filled)
				break;
		} else if (filled) {
			bitset = 0;
			break;
		}
		start = state->end + 1;
		if (start > end)
			break;
		node = rb_next(node);
		if (!node) {
			if (filled)
				bitset = 0;
			break;
		}
	}
	spin_unlock_irqrestore(&tree->lock, flags);
	return bitset;
}
EXPORT_SYMBOL(test_range_bit);

/*
 * helper function to set a given page up to date if all the
 * extents in the tree for that page are up to date
 */
static int check_page_uptodate(struct extent_io_tree *tree,
			       struct page *page)
{
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 end = start + PAGE_CACHE_SIZE - 1;
	if (test_range_bit(tree, start, end, EXTENT_UPTODATE, 1))
		SetPageUptodate(page);
	return 0;
}

/*
 * helper function to unlock a page if all the extents in the tree
 * for that page are unlocked
 */
static int check_page_locked(struct extent_io_tree *tree,
			     struct page *page)
{
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 end = start + PAGE_CACHE_SIZE - 1;
	if (!test_range_bit(tree, start, end, EXTENT_LOCKED, 0))
		unlock_page(page);
	return 0;
}

/*
 * helper function to end page writeback if all the extents
 * in the tree for that page are done with writeback
 */
static int check_page_writeback(struct extent_io_tree *tree,
			     struct page *page)
{
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 end = start + PAGE_CACHE_SIZE - 1;
	if (!test_range_bit(tree, start, end, EXTENT_WRITEBACK, 0))
		end_page_writeback(page);
	return 0;
}

/* lots and lots of room for performance fixes in the end_bio funcs */

/*
 * after a writepage IO is done, we need to:
 * clear the uptodate bits on error
 * clear the writeback bits in the extent tree for this IO
 * end_page_writeback if the page has no more pending IO
 *
 * Scheduling is not allowed, so the extent state tree is expected
 * to have one and only one object corresponding to this IO.
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
static void end_bio_extent_writepage(struct bio *bio, int err)
#else
static int end_bio_extent_writepage(struct bio *bio,
				   unsigned int bytes_done, int err)
#endif
{
	int uptodate = err == 0;
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct extent_state *state = bio->bi_private;
	struct extent_io_tree *tree = state->tree;
	struct rb_node *node;
	u64 start;
	u64 end;
	u64 cur;
	int whole_page;
	int ret;
	unsigned long flags;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	if (bio->bi_size)
		return 1;
#endif
	do {
		struct page *page = bvec->bv_page;
		start = ((u64)page->index << PAGE_CACHE_SHIFT) +
			 bvec->bv_offset;
		end = start + bvec->bv_len - 1;

		if (bvec->bv_offset == 0 && bvec->bv_len == PAGE_CACHE_SIZE)
			whole_page = 1;
		else
			whole_page = 0;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (tree->ops && tree->ops->writepage_end_io_hook) {
			ret = tree->ops->writepage_end_io_hook(page, start,
						       end, state);
			if (ret)
				uptodate = 0;
		}

		if (!uptodate && tree->ops &&
		    tree->ops->writepage_io_failed_hook) {
			ret = tree->ops->writepage_io_failed_hook(bio, page,
							 start, end, state);
			if (ret == 0) {
				state = NULL;
				uptodate = (err == 0);
				continue;
			}
		}

		if (!uptodate) {
			clear_extent_uptodate(tree, start, end, GFP_ATOMIC);
			ClearPageUptodate(page);
			SetPageError(page);
		}

		/*
		 * bios can get merged in funny ways, and so we need to
		 * be careful with the state variable.  We know the
		 * state won't be merged with others because it has
		 * WRITEBACK set, but we can't be sure each biovec is
		 * sequential in the file.  So, if our cached state
		 * doesn't match the expected end, search the tree
		 * for the correct one.
		 */

		spin_lock_irqsave(&tree->lock, flags);
		if (!state || state->end != end) {
			state = NULL;
			node = __etree_search(tree, start, NULL, NULL);
			if (node) {
				state = rb_entry(node, struct extent_state,
						 rb_node);
				if (state->end != end ||
				    !(state->state & EXTENT_WRITEBACK))
					state = NULL;
			}
			if (!state) {
				spin_unlock_irqrestore(&tree->lock, flags);
				clear_extent_writeback(tree, start,
						       end, GFP_ATOMIC);
				goto next_io;
			}
		}
		cur = end;
		while(1) {
			struct extent_state *clear = state;
			cur = state->start;
			node = rb_prev(&state->rb_node);
			if (node) {
				state = rb_entry(node,
						 struct extent_state,
						 rb_node);
			} else {
				state = NULL;
			}

			clear_state_bit(tree, clear, EXTENT_WRITEBACK,
					1, 0);
			if (cur == start)
				break;
			if (cur < start) {
				WARN_ON(1);
				break;
			}
			if (!node)
				break;
		}
		/* before releasing the lock, make sure the next state
		 * variable has the expected bits set and corresponds
		 * to the correct offsets in the file
		 */
		if (state && (state->end + 1 != start ||
		    !(state->state & EXTENT_WRITEBACK))) {
			state = NULL;
		}
		spin_unlock_irqrestore(&tree->lock, flags);
next_io:

		if (whole_page)
			end_page_writeback(page);
		else
			check_page_writeback(tree, page);
	} while (bvec >= bio->bi_io_vec);
	bio_put(bio);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	return 0;
#endif
}

/*
 * after a readpage IO is done, we need to:
 * clear the uptodate bits on error
 * set the uptodate bits if things worked
 * set the page up to date if all extents in the tree are uptodate
 * clear the lock bit in the extent tree
 * unlock the page if there are no other extents locked for it
 *
 * Scheduling is not allowed, so the extent state tree is expected
 * to have one and only one object corresponding to this IO.
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
static void end_bio_extent_readpage(struct bio *bio, int err)
#else
static int end_bio_extent_readpage(struct bio *bio,
				   unsigned int bytes_done, int err)
#endif
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct extent_state *state = bio->bi_private;
	struct extent_io_tree *tree = state->tree;
	struct rb_node *node;
	u64 start;
	u64 end;
	u64 cur;
	unsigned long flags;
	int whole_page;
	int ret;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	if (bio->bi_size)
		return 1;
#endif

	do {
		struct page *page = bvec->bv_page;
		start = ((u64)page->index << PAGE_CACHE_SHIFT) +
			bvec->bv_offset;
		end = start + bvec->bv_len - 1;

		if (bvec->bv_offset == 0 && bvec->bv_len == PAGE_CACHE_SIZE)
			whole_page = 1;
		else
			whole_page = 0;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (uptodate && tree->ops && tree->ops->readpage_end_io_hook) {
			ret = tree->ops->readpage_end_io_hook(page, start, end,
							      state);
			if (ret)
				uptodate = 0;
		}
		if (!uptodate && tree->ops &&
		    tree->ops->readpage_io_failed_hook) {
			ret = tree->ops->readpage_io_failed_hook(bio, page,
							 start, end, state);
			if (ret == 0) {
				state = NULL;
				uptodate =
					test_bit(BIO_UPTODATE, &bio->bi_flags);
				continue;
			}
		}

		spin_lock_irqsave(&tree->lock, flags);
		if (!state || state->end != end) {
			state = NULL;
			node = __etree_search(tree, start, NULL, NULL);
			if (node) {
				state = rb_entry(node, struct extent_state,
						 rb_node);
				if (state->end != end ||
				    !(state->state & EXTENT_LOCKED))
					state = NULL;
			}
			if (!state) {
				spin_unlock_irqrestore(&tree->lock, flags);
				if (uptodate)
					set_extent_uptodate(tree, start, end,
							    GFP_ATOMIC);
				unlock_extent(tree, start, end, GFP_ATOMIC);
				goto next_io;
			}
		}

		cur = end;
		while(1) {
			struct extent_state *clear = state;
			cur = state->start;
			node = rb_prev(&state->rb_node);
			if (node) {
				state = rb_entry(node,
					 struct extent_state,
					 rb_node);
			} else {
				state = NULL;
			}
			if (uptodate) {
				set_state_cb(tree, clear, EXTENT_UPTODATE);
				clear->state |= EXTENT_UPTODATE;
			}
			clear_state_bit(tree, clear, EXTENT_LOCKED,
					1, 0);
			if (cur == start)
				break;
			if (cur < start) {
				WARN_ON(1);
				break;
			}
			if (!node)
				break;
		}
		/* before releasing the lock, make sure the next state
		 * variable has the expected bits set and corresponds
		 * to the correct offsets in the file
		 */
		if (state && (state->end + 1 != start ||
		    !(state->state & EXTENT_LOCKED))) {
			state = NULL;
		}
		spin_unlock_irqrestore(&tree->lock, flags);
next_io:
		if (whole_page) {
			if (uptodate) {
				SetPageUptodate(page);
			} else {
				ClearPageUptodate(page);
				SetPageError(page);
			}
			unlock_page(page);
		} else {
			if (uptodate) {
				check_page_uptodate(tree, page);
			} else {
				ClearPageUptodate(page);
				SetPageError(page);
			}
			check_page_locked(tree, page);
		}
	} while (bvec >= bio->bi_io_vec);

	bio_put(bio);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	return 0;
#endif
}

/*
 * IO done from prepare_write is pretty simple, we just unlock
 * the structs in the extent tree when done, and set the uptodate bits
 * as appropriate.
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
static void end_bio_extent_preparewrite(struct bio *bio, int err)
#else
static int end_bio_extent_preparewrite(struct bio *bio,
				       unsigned int bytes_done, int err)
#endif
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct extent_state *state = bio->bi_private;
	struct extent_io_tree *tree = state->tree;
	u64 start;
	u64 end;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	if (bio->bi_size)
		return 1;
#endif

	do {
		struct page *page = bvec->bv_page;
		start = ((u64)page->index << PAGE_CACHE_SHIFT) +
			bvec->bv_offset;
		end = start + bvec->bv_len - 1;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (uptodate) {
			set_extent_uptodate(tree, start, end, GFP_ATOMIC);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}

		unlock_extent(tree, start, end, GFP_ATOMIC);

	} while (bvec >= bio->bi_io_vec);

	bio_put(bio);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	return 0;
#endif
}

static struct bio *
extent_bio_alloc(struct block_device *bdev, u64 first_sector, int nr_vecs,
		 gfp_t gfp_flags)
{
	struct bio *bio;

	bio = bio_alloc(gfp_flags, nr_vecs);

	if (bio == NULL && (current->flags & PF_MEMALLOC)) {
		while (!bio && (nr_vecs /= 2))
			bio = bio_alloc(gfp_flags, nr_vecs);
	}

	if (bio) {
		bio->bi_size = 0;
		bio->bi_bdev = bdev;
		bio->bi_sector = first_sector;
	}
	return bio;
}

static int submit_one_bio(int rw, struct bio *bio, int mirror_num)
{
	int ret = 0;
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
	struct page *page = bvec->bv_page;
	struct extent_io_tree *tree = bio->bi_private;
	struct rb_node *node;
	struct extent_state *state;
	u64 start;
	u64 end;

	start = ((u64)page->index << PAGE_CACHE_SHIFT) + bvec->bv_offset;
	end = start + bvec->bv_len - 1;

	spin_lock_irq(&tree->lock);
	node = __etree_search(tree, start, NULL, NULL);
	BUG_ON(!node);
	state = rb_entry(node, struct extent_state, rb_node);
	while(state->end < end) {
		node = rb_next(node);
		state = rb_entry(node, struct extent_state, rb_node);
	}
	BUG_ON(state->end != end);
	spin_unlock_irq(&tree->lock);

	bio->bi_private = state;

	bio_get(bio);

	if (tree->ops && tree->ops->submit_bio_hook)
		tree->ops->submit_bio_hook(page->mapping->host, rw, bio,
					   mirror_num);
	else
		submit_bio(rw, bio);
	if (bio_flagged(bio, BIO_EOPNOTSUPP))
		ret = -EOPNOTSUPP;
	bio_put(bio);
	return ret;
}

static int submit_extent_page(int rw, struct extent_io_tree *tree,
			      struct page *page, sector_t sector,
			      size_t size, unsigned long offset,
			      struct block_device *bdev,
			      struct bio **bio_ret,
			      unsigned long max_pages,
			      bio_end_io_t end_io_func,
			      int mirror_num)
{
	int ret = 0;
	struct bio *bio;
	int nr;

	if (bio_ret && *bio_ret) {
		bio = *bio_ret;
		if (bio->bi_sector + (bio->bi_size >> 9) != sector ||
		    (tree->ops && tree->ops->merge_bio_hook &&
		     tree->ops->merge_bio_hook(page, offset, size, bio)) ||
		    bio_add_page(bio, page, size, offset) < size) {
			ret = submit_one_bio(rw, bio, mirror_num);
			bio = NULL;
		} else {
			return 0;
		}
	}
	nr = bio_get_nr_vecs(bdev);
	bio = extent_bio_alloc(bdev, sector, nr, GFP_NOFS | __GFP_HIGH);
	if (!bio) {
		printk("failed to allocate bio nr %d\n", nr);
	}


	bio_add_page(bio, page, size, offset);
	bio->bi_end_io = end_io_func;
	bio->bi_private = tree;

	if (bio_ret) {
		*bio_ret = bio;
	} else {
		ret = submit_one_bio(rw, bio, mirror_num);
	}

	return ret;
}

void set_page_extent_mapped(struct page *page)
{
	if (!PagePrivate(page)) {
		SetPagePrivate(page);
		WARN_ON(!page->mapping->a_ops->invalidatepage);
		set_page_private(page, EXTENT_PAGE_PRIVATE);
		page_cache_get(page);
	}
}

void set_page_extent_head(struct page *page, unsigned long len)
{
	set_page_private(page, EXTENT_PAGE_PRIVATE_FIRST_PAGE | len << 2);
}

/*
 * basic readpage implementation.  Locked extent state structs are inserted
 * into the tree that are removed when the IO is done (by the end_io
 * handlers)
 */
static int __extent_read_full_page(struct extent_io_tree *tree,
				   struct page *page,
				   get_extent_t *get_extent,
				   struct bio **bio, int mirror_num)
{
	struct inode *inode = page->mapping->host;
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 page_end = start + PAGE_CACHE_SIZE - 1;
	u64 end;
	u64 cur = start;
	u64 extent_offset;
	u64 last_byte = i_size_read(inode);
	u64 block_start;
	u64 cur_end;
	sector_t sector;
	struct extent_map *em;
	struct block_device *bdev;
	int ret;
	int nr = 0;
	size_t page_offset = 0;
	size_t iosize;
	size_t blocksize = inode->i_sb->s_blocksize;

	set_page_extent_mapped(page);

	end = page_end;
	lock_extent(tree, start, end, GFP_NOFS);

	while (cur <= end) {
		if (cur >= last_byte) {
			char *userpage;
			iosize = PAGE_CACHE_SIZE - page_offset;
			userpage = kmap_atomic(page, KM_USER0);
			memset(userpage + page_offset, 0, iosize);
			flush_dcache_page(page);
			kunmap_atomic(userpage, KM_USER0);
			set_extent_uptodate(tree, cur, cur + iosize - 1,
					    GFP_NOFS);
			unlock_extent(tree, cur, cur + iosize - 1, GFP_NOFS);
			break;
		}
		em = get_extent(inode, page, page_offset, cur,
				end - cur + 1, 0);
		if (IS_ERR(em) || !em) {
			SetPageError(page);
			unlock_extent(tree, cur, end, GFP_NOFS);
			break;
		}

		extent_offset = cur - em->start;
		BUG_ON(extent_map_end(em) <= cur);
		BUG_ON(end < cur);

		iosize = min(extent_map_end(em) - cur, end - cur + 1);
		cur_end = min(extent_map_end(em) - 1, end);
		iosize = (iosize + blocksize - 1) & ~((u64)blocksize - 1);
		sector = (em->block_start + extent_offset) >> 9;
		bdev = em->bdev;
		block_start = em->block_start;
		free_extent_map(em);
		em = NULL;

		/* we've found a hole, just zero and go on */
		if (block_start == EXTENT_MAP_HOLE) {
			char *userpage;
			userpage = kmap_atomic(page, KM_USER0);
			memset(userpage + page_offset, 0, iosize);
			flush_dcache_page(page);
			kunmap_atomic(userpage, KM_USER0);

			set_extent_uptodate(tree, cur, cur + iosize - 1,
					    GFP_NOFS);
			unlock_extent(tree, cur, cur + iosize - 1, GFP_NOFS);
			cur = cur + iosize;
			page_offset += iosize;
			continue;
		}
		/* the get_extent function already copied into the page */
		if (test_range_bit(tree, cur, cur_end, EXTENT_UPTODATE, 1)) {
			unlock_extent(tree, cur, cur + iosize - 1, GFP_NOFS);
			cur = cur + iosize;
			page_offset += iosize;
			continue;
		}
		/* we have an inline extent but it didn't get marked up
		 * to date.  Error out
		 */
		if (block_start == EXTENT_MAP_INLINE) {
			SetPageError(page);
			unlock_extent(tree, cur, cur + iosize - 1, GFP_NOFS);
			cur = cur + iosize;
			page_offset += iosize;
			continue;
		}

		ret = 0;
		if (tree->ops && tree->ops->readpage_io_hook) {
			ret = tree->ops->readpage_io_hook(page, cur,
							  cur + iosize - 1);
		}
		if (!ret) {
			unsigned long nr = (last_byte >> PAGE_CACHE_SHIFT) + 1;
			nr -= page->index;
			ret = submit_extent_page(READ, tree, page,
					 sector, iosize, page_offset,
					 bdev, bio, nr,
					 end_bio_extent_readpage, mirror_num);
		}
		if (ret)
			SetPageError(page);
		cur = cur + iosize;
		page_offset += iosize;
		nr++;
	}
	if (!nr) {
		if (!PageError(page))
			SetPageUptodate(page);
		unlock_page(page);
	}
	return 0;
}

int extent_read_full_page(struct extent_io_tree *tree, struct page *page,
			    get_extent_t *get_extent)
{
	struct bio *bio = NULL;
	int ret;

	ret = __extent_read_full_page(tree, page, get_extent, &bio, 0);
	if (bio)
		submit_one_bio(READ, bio, 0);
	return ret;
}
EXPORT_SYMBOL(extent_read_full_page);

/*
 * the writepage semantics are similar to regular writepage.  extent
 * records are inserted to lock ranges in the tree, and as dirty areas
 * are found, they are marked writeback.  Then the lock bits are removed
 * and the end_io handler clears the writeback ranges
 */
static int __extent_writepage(struct page *page, struct writeback_control *wbc,
			      void *data)
{
	struct inode *inode = page->mapping->host;
	struct extent_page_data *epd = data;
	struct extent_io_tree *tree = epd->tree;
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 delalloc_start;
	u64 page_end = start + PAGE_CACHE_SIZE - 1;
	u64 end;
	u64 cur = start;
	u64 extent_offset;
	u64 last_byte = i_size_read(inode);
	u64 block_start;
	u64 iosize;
	sector_t sector;
	struct extent_map *em;
	struct block_device *bdev;
	int ret;
	int nr = 0;
	size_t page_offset = 0;
	size_t blocksize;
	loff_t i_size = i_size_read(inode);
	unsigned long end_index = i_size >> PAGE_CACHE_SHIFT;
	u64 nr_delalloc;
	u64 delalloc_end;

	WARN_ON(!PageLocked(page));
	if (page->index > end_index) {
		clear_extent_dirty(tree, start, page_end, GFP_NOFS);
		unlock_page(page);
		return 0;
	}

	if (page->index == end_index) {
		char *userpage;

		size_t offset = i_size & (PAGE_CACHE_SIZE - 1);

		userpage = kmap_atomic(page, KM_USER0);
		memset(userpage + offset, 0, PAGE_CACHE_SIZE - offset);
		flush_dcache_page(page);
		kunmap_atomic(userpage, KM_USER0);
	}

	set_page_extent_mapped(page);

	delalloc_start = start;
	delalloc_end = 0;
	while(delalloc_end < page_end) {
		nr_delalloc = find_lock_delalloc_range(tree, &delalloc_start,
						       &delalloc_end,
						       128 * 1024 * 1024);
		if (nr_delalloc == 0) {
			delalloc_start = delalloc_end + 1;
			continue;
		}
		tree->ops->fill_delalloc(inode, delalloc_start,
					 delalloc_end);
		clear_extent_bit(tree, delalloc_start,
				 delalloc_end,
				 EXTENT_LOCKED | EXTENT_DELALLOC,
				 1, 0, GFP_NOFS);
		delalloc_start = delalloc_end + 1;
	}
	lock_extent(tree, start, page_end, GFP_NOFS);

	end = page_end;
	if (test_range_bit(tree, start, page_end, EXTENT_DELALLOC, 0)) {
		printk("found delalloc bits after lock_extent\n");
	}

	if (last_byte <= start) {
		clear_extent_dirty(tree, start, page_end, GFP_NOFS);
		goto done;
	}

	set_extent_uptodate(tree, start, page_end, GFP_NOFS);
	blocksize = inode->i_sb->s_blocksize;

	while (cur <= end) {
		if (cur >= last_byte) {
			clear_extent_dirty(tree, cur, page_end, GFP_NOFS);
			break;
		}
		em = epd->get_extent(inode, page, page_offset, cur,
				     end - cur + 1, 1);
		if (IS_ERR(em) || !em) {
			SetPageError(page);
			break;
		}

		extent_offset = cur - em->start;
		BUG_ON(extent_map_end(em) <= cur);
		BUG_ON(end < cur);
		iosize = min(extent_map_end(em) - cur, end - cur + 1);
		iosize = (iosize + blocksize - 1) & ~((u64)blocksize - 1);
		sector = (em->block_start + extent_offset) >> 9;
		bdev = em->bdev;
		block_start = em->block_start;
		free_extent_map(em);
		em = NULL;

		if (block_start == EXTENT_MAP_HOLE ||
		    block_start == EXTENT_MAP_INLINE) {
			clear_extent_dirty(tree, cur,
					   cur + iosize - 1, GFP_NOFS);
			cur = cur + iosize;
			page_offset += iosize;
			continue;
		}

		/* leave this out until we have a page_mkwrite call */
		if (0 && !test_range_bit(tree, cur, cur + iosize - 1,
				   EXTENT_DIRTY, 0)) {
			cur = cur + iosize;
			page_offset += iosize;
			continue;
		}
		clear_extent_dirty(tree, cur, cur + iosize - 1, GFP_NOFS);
		if (tree->ops && tree->ops->writepage_io_hook) {
			ret = tree->ops->writepage_io_hook(page, cur,
						cur + iosize - 1);
		} else {
			ret = 0;
		}
		if (ret) {
			SetPageError(page);
		} else {
			unsigned long max_nr = end_index + 1;
			set_range_writeback(tree, cur, cur + iosize - 1);
			if (!PageWriteback(page)) {
				printk("warning page %lu not writeback, "
				       "cur %llu end %llu\n", page->index,
				       (unsigned long long)cur,
				       (unsigned long long)end);
			}

			ret = submit_extent_page(WRITE, tree, page, sector,
						 iosize, page_offset, bdev,
						 &epd->bio, max_nr,
						 end_bio_extent_writepage, 0);
			if (ret)
				SetPageError(page);
		}
		cur = cur + iosize;
		page_offset += iosize;
		nr++;
	}
done:
	if (nr == 0) {
		/* make sure the mapping tag for page dirty gets cleared */
		set_page_writeback(page);
		end_page_writeback(page);
	}
	unlock_extent(tree, start, page_end, GFP_NOFS);
	unlock_page(page);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
/* Taken directly from 2.6.23 for 2.6.18 back port */
typedef int (*writepage_t)(struct page *page, struct writeback_control *wbc,
                                void *data);

/**
 * write_cache_pages - walk the list of dirty pages of the given address space
 * and write all of them.
 * @mapping: address space structure to write
 * @wbc: subtract the number of written pages from *@wbc->nr_to_write
 * @writepage: function called for each page
 * @data: data passed to writepage function
 *
 * If a page is already under I/O, write_cache_pages() skips it, even
 * if it's dirty.  This is desirable behaviour for memory-cleaning writeback,
 * but it is INCORRECT for data-integrity system calls such as fsync().  fsync()
 * and msync() need to guarantee that all the data which was dirty at the time
 * the call was made get new I/O started against them.  If wbc->sync_mode is
 * WB_SYNC_ALL then we were called for data integrity and we must wait for
 * existing IO to complete.
 */
static int write_cache_pages(struct address_space *mapping,
		      struct writeback_control *wbc, writepage_t writepage,
		      void *data)
{
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	int ret = 0;
	int done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	int scanned = 0;
	int range_whole = 0;

	if (wbc->nonblocking && bdi_write_congested(bdi)) {
		wbc->encountered_congestion = 1;
		return 0;
	}

	pagevec_init(&pvec, 0);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		scanned = 1;
	}
retry:
	while (!done && (index <= end) &&
	       (nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
					      PAGECACHE_TAG_DIRTY,
					      min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1))) {
		unsigned i;

		scanned = 1;
		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			/*
			 * At this point we hold neither mapping->tree_lock nor
			 * lock on the page itself: the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or even
			 * swizzled back from swapper_space to tmpfs file
			 * mapping
			 */
			lock_page(page);

			if (unlikely(page->mapping != mapping)) {
				unlock_page(page);
				continue;
			}

			if (!wbc->range_cyclic && page->index > end) {
				done = 1;
				unlock_page(page);
				continue;
			}

			if (wbc->sync_mode != WB_SYNC_NONE)
				wait_on_page_writeback(page);

			if (PageWriteback(page) ||
			    !clear_page_dirty_for_io(page)) {
				unlock_page(page);
				continue;
			}

			ret = (*writepage)(page, wbc, data);

			if (unlikely(ret == AOP_WRITEPAGE_ACTIVATE)) {
				unlock_page(page);
				ret = 0;
			}
			if (ret || (--(wbc->nr_to_write) <= 0))
				done = 1;
			if (wbc->nonblocking && bdi_write_congested(bdi)) {
				wbc->encountered_congestion = 1;
				done = 1;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}
	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;
		goto retry;
	}
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = index;
	return ret;
}
#endif

int extent_write_full_page(struct extent_io_tree *tree, struct page *page,
			  get_extent_t *get_extent,
			  struct writeback_control *wbc)
{
	int ret;
	struct address_space *mapping = page->mapping;
	struct extent_page_data epd = {
		.bio = NULL,
		.tree = tree,
		.get_extent = get_extent,
	};
	struct writeback_control wbc_writepages = {
		.bdi		= wbc->bdi,
		.sync_mode	= WB_SYNC_NONE,
		.older_than_this = NULL,
		.nr_to_write	= 64,
		.range_start	= page_offset(page) + PAGE_CACHE_SIZE,
		.range_end	= (loff_t)-1,
	};


	ret = __extent_writepage(page, wbc, &epd);

	write_cache_pages(mapping, &wbc_writepages, __extent_writepage, &epd);
	if (epd.bio) {
		submit_one_bio(WRITE, epd.bio, 0);
	}
	return ret;
}
EXPORT_SYMBOL(extent_write_full_page);


int extent_writepages(struct extent_io_tree *tree,
		      struct address_space *mapping,
		      get_extent_t *get_extent,
		      struct writeback_control *wbc)
{
	int ret = 0;
	struct extent_page_data epd = {
		.bio = NULL,
		.tree = tree,
		.get_extent = get_extent,
	};

	ret = write_cache_pages(mapping, wbc, __extent_writepage, &epd);
	if (epd.bio) {
		submit_one_bio(WRITE, epd.bio, 0);
	}
	return ret;
}
EXPORT_SYMBOL(extent_writepages);

int extent_readpages(struct extent_io_tree *tree,
		     struct address_space *mapping,
		     struct list_head *pages, unsigned nr_pages,
		     get_extent_t get_extent)
{
	struct bio *bio = NULL;
	unsigned page_idx;
	struct pagevec pvec;

	pagevec_init(&pvec, 0);
	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = list_entry(pages->prev, struct page, lru);

		prefetchw(&page->flags);
		list_del(&page->lru);
		/*
		 * what we want to do here is call add_to_page_cache_lru,
		 * but that isn't exported, so we reproduce it here
		 */
		if (!add_to_page_cache(page, mapping,
					page->index, GFP_KERNEL)) {

			/* open coding of lru_cache_add, also not exported */
			page_cache_get(page);
			if (!pagevec_add(&pvec, page))
				__pagevec_lru_add(&pvec);
			__extent_read_full_page(tree, page, get_extent,
						&bio, 0);
		}
		page_cache_release(page);
	}
	if (pagevec_count(&pvec))
		__pagevec_lru_add(&pvec);
	BUG_ON(!list_empty(pages));
	if (bio)
		submit_one_bio(READ, bio, 0);
	return 0;
}
EXPORT_SYMBOL(extent_readpages);

/*
 * basic invalidatepage code, this waits on any locked or writeback
 * ranges corresponding to the page, and then deletes any extent state
 * records from the tree
 */
int extent_invalidatepage(struct extent_io_tree *tree,
			  struct page *page, unsigned long offset)
{
	u64 start = ((u64)page->index << PAGE_CACHE_SHIFT);
	u64 end = start + PAGE_CACHE_SIZE - 1;
	size_t blocksize = page->mapping->host->i_sb->s_blocksize;

	start += (offset + blocksize -1) & ~(blocksize - 1);
	if (start > end)
		return 0;

	lock_extent(tree, start, end, GFP_NOFS);
	wait_on_extent_writeback(tree, start, end);
	clear_extent_bit(tree, start, end,
			 EXTENT_LOCKED | EXTENT_DIRTY | EXTENT_DELALLOC,
			 1, 1, GFP_NOFS);
	return 0;
}
EXPORT_SYMBOL(extent_invalidatepage);

/*
 * simple commit_write call, set_range_dirty is used to mark both
 * the pages and the extent records as dirty
 */
int extent_commit_write(struct extent_io_tree *tree,
			struct inode *inode, struct page *page,
			unsigned from, unsigned to)
{
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;

	set_page_extent_mapped(page);
	set_page_dirty(page);

	if (pos > inode->i_size) {
		i_size_write(inode, pos);
		mark_inode_dirty(inode);
	}
	return 0;
}
EXPORT_SYMBOL(extent_commit_write);

int extent_prepare_write(struct extent_io_tree *tree,
			 struct inode *inode, struct page *page,
			 unsigned from, unsigned to, get_extent_t *get_extent)
{
	u64 page_start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 page_end = page_start + PAGE_CACHE_SIZE - 1;
	u64 block_start;
	u64 orig_block_start;
	u64 block_end;
	u64 cur_end;
	struct extent_map *em;
	unsigned blocksize = 1 << inode->i_blkbits;
	size_t page_offset = 0;
	size_t block_off_start;
	size_t block_off_end;
	int err = 0;
	int iocount = 0;
	int ret = 0;
	int isnew;

	set_page_extent_mapped(page);

	block_start = (page_start + from) & ~((u64)blocksize - 1);
	block_end = (page_start + to - 1) | (blocksize - 1);
	orig_block_start = block_start;

	lock_extent(tree, page_start, page_end, GFP_NOFS);
	while(block_start <= block_end) {
		em = get_extent(inode, page, page_offset, block_start,
				block_end - block_start + 1, 1);
		if (IS_ERR(em) || !em) {
			goto err;
		}
		cur_end = min(block_end, extent_map_end(em) - 1);
		block_off_start = block_start & (PAGE_CACHE_SIZE - 1);
		block_off_end = block_off_start + blocksize;
		isnew = clear_extent_new(tree, block_start, cur_end, GFP_NOFS);

		if (!PageUptodate(page) && isnew &&
		    (block_off_end > to || block_off_start < from)) {
			void *kaddr;

			kaddr = kmap_atomic(page, KM_USER0);
			if (block_off_end > to)
				memset(kaddr + to, 0, block_off_end - to);
			if (block_off_start < from)
				memset(kaddr + block_off_start, 0,
				       from - block_off_start);
			flush_dcache_page(page);
			kunmap_atomic(kaddr, KM_USER0);
		}
		if ((em->block_start != EXTENT_MAP_HOLE &&
		     em->block_start != EXTENT_MAP_INLINE) &&
		    !isnew && !PageUptodate(page) &&
		    (block_off_end > to || block_off_start < from) &&
		    !test_range_bit(tree, block_start, cur_end,
				    EXTENT_UPTODATE, 1)) {
			u64 sector;
			u64 extent_offset = block_start - em->start;
			size_t iosize;
			sector = (em->block_start + extent_offset) >> 9;
			iosize = (cur_end - block_start + blocksize) &
				~((u64)blocksize - 1);
			/*
			 * we've already got the extent locked, but we
			 * need to split the state such that our end_bio
			 * handler can clear the lock.
			 */
			set_extent_bit(tree, block_start,
				       block_start + iosize - 1,
				       EXTENT_LOCKED, 0, NULL, GFP_NOFS);
			ret = submit_extent_page(READ, tree, page,
					 sector, iosize, page_offset, em->bdev,
					 NULL, 1,
					 end_bio_extent_preparewrite, 0);
			iocount++;
			block_start = block_start + iosize;
		} else {
			set_extent_uptodate(tree, block_start, cur_end,
					    GFP_NOFS);
			unlock_extent(tree, block_start, cur_end, GFP_NOFS);
			block_start = cur_end + 1;
		}
		page_offset = block_start & (PAGE_CACHE_SIZE - 1);
		free_extent_map(em);
	}
	if (iocount) {
		wait_extent_bit(tree, orig_block_start,
				block_end, EXTENT_LOCKED);
	}
	check_page_uptodate(tree, page);
err:
	/* FIXME, zero out newly allocated blocks on error */
	return err;
}
EXPORT_SYMBOL(extent_prepare_write);

/*
 * a helper for releasepage, this tests for areas of the page that
 * are locked or under IO and drops the related state bits if it is safe
 * to drop the page.
 */
int try_release_extent_state(struct extent_map_tree *map,
			     struct extent_io_tree *tree, struct page *page,
			     gfp_t mask)
{
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 end = start + PAGE_CACHE_SIZE - 1;
	int ret = 1;

	if (test_range_bit(tree, start, end, EXTENT_IOBITS, 0))
		ret = 0;
	else {
		if ((mask & GFP_NOFS) == GFP_NOFS)
			mask = GFP_NOFS;
		clear_extent_bit(tree, start, end, EXTENT_UPTODATE,
				 1, 1, mask);
	}
	return ret;
}
EXPORT_SYMBOL(try_release_extent_state);

/*
 * a helper for releasepage.  As long as there are no locked extents
 * in the range corresponding to the page, both state records and extent
 * map records are removed
 */
int try_release_extent_mapping(struct extent_map_tree *map,
			       struct extent_io_tree *tree, struct page *page,
			       gfp_t mask)
{
	struct extent_map *em;
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 end = start + PAGE_CACHE_SIZE - 1;

	if ((mask & __GFP_WAIT) &&
	    page->mapping->host->i_size > 16 * 1024 * 1024) {
		u64 len;
		while (start <= end) {
			len = end - start + 1;
			spin_lock(&map->lock);
			em = lookup_extent_mapping(map, start, len);
			if (!em || IS_ERR(em)) {
				spin_unlock(&map->lock);
				break;
			}
			if (em->start != start) {
				spin_unlock(&map->lock);
				free_extent_map(em);
				break;
			}
			if (!test_range_bit(tree, em->start,
					    extent_map_end(em) - 1,
					    EXTENT_LOCKED, 0)) {
				remove_extent_mapping(map, em);
				/* once for the rb tree */
				free_extent_map(em);
			}
			start = extent_map_end(em);
			spin_unlock(&map->lock);

			/* once for us */
			free_extent_map(em);
		}
	}
	return try_release_extent_state(map, tree, page, mask);
}
EXPORT_SYMBOL(try_release_extent_mapping);

sector_t extent_bmap(struct address_space *mapping, sector_t iblock,
		get_extent_t *get_extent)
{
	struct inode *inode = mapping->host;
	u64 start = iblock << inode->i_blkbits;
	sector_t sector = 0;
	struct extent_map *em;

	em = get_extent(inode, NULL, 0, start, (1 << inode->i_blkbits), 0);
	if (!em || IS_ERR(em))
		return 0;

	if (em->block_start == EXTENT_MAP_INLINE ||
	    em->block_start == EXTENT_MAP_HOLE)
		goto out;

	sector = (em->block_start + start - em->start) >> inode->i_blkbits;
out:
	free_extent_map(em);
	return sector;
}

static int add_lru(struct extent_io_tree *tree, struct extent_buffer *eb)
{
	if (list_empty(&eb->lru)) {
		extent_buffer_get(eb);
		list_add(&eb->lru, &tree->buffer_lru);
		tree->lru_size++;
		if (tree->lru_size >= BUFFER_LRU_MAX) {
			struct extent_buffer *rm;
			rm = list_entry(tree->buffer_lru.prev,
					struct extent_buffer, lru);
			tree->lru_size--;
			list_del_init(&rm->lru);
			free_extent_buffer(rm);
		}
	} else
		list_move(&eb->lru, &tree->buffer_lru);
	return 0;
}
static struct extent_buffer *find_lru(struct extent_io_tree *tree,
				      u64 start, unsigned long len)
{
	struct list_head *lru = &tree->buffer_lru;
	struct list_head *cur = lru->next;
	struct extent_buffer *eb;

	if (list_empty(lru))
		return NULL;

	do {
		eb = list_entry(cur, struct extent_buffer, lru);
		if (eb->start == start && eb->len == len) {
			extent_buffer_get(eb);
			return eb;
		}
		cur = cur->next;
	} while (cur != lru);
	return NULL;
}

static inline unsigned long num_extent_pages(u64 start, u64 len)
{
	return ((start + len + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT) -
		(start >> PAGE_CACHE_SHIFT);
}

static inline struct page *extent_buffer_page(struct extent_buffer *eb,
					      unsigned long i)
{
	struct page *p;
	struct address_space *mapping;

	if (i == 0)
		return eb->first_page;
	i += eb->start >> PAGE_CACHE_SHIFT;
	mapping = eb->first_page->mapping;
	read_lock_irq(&mapping->tree_lock);
	p = radix_tree_lookup(&mapping->page_tree, i);
	read_unlock_irq(&mapping->tree_lock);
	return p;
}

int release_extent_buffer_tail_pages(struct extent_buffer *eb)
{
	unsigned long num_pages = num_extent_pages(eb->start, eb->len);
	struct page *page;
	unsigned long i;

	if (num_pages == 1)
		return 0;
	for (i = 1; i < num_pages; i++) {
		page = extent_buffer_page(eb, i);
		page_cache_release(page);
	}
	return 0;
}


int invalidate_extent_lru(struct extent_io_tree *tree, u64 start,
			  unsigned long len)
{
	struct list_head *lru = &tree->buffer_lru;
	struct list_head *cur = lru->next;
	struct extent_buffer *eb;
	int found = 0;

	spin_lock(&tree->lru_lock);
	if (list_empty(lru))
		goto out;

	do {
		eb = list_entry(cur, struct extent_buffer, lru);
		if (eb->start <= start && eb->start + eb->len > start) {
			eb->flags &= ~EXTENT_UPTODATE;
		}
		cur = cur->next;
	} while (cur != lru);
out:
	spin_unlock(&tree->lru_lock);
	return found;
}

static struct extent_buffer *__alloc_extent_buffer(struct extent_io_tree *tree,
						   u64 start,
						   unsigned long len,
						   gfp_t mask)
{
	struct extent_buffer *eb = NULL;
	unsigned long flags;

	spin_lock(&tree->lru_lock);
	eb = find_lru(tree, start, len);
	spin_unlock(&tree->lru_lock);
	if (eb) {
		return eb;
	}

	eb = kmem_cache_zalloc(extent_buffer_cache, mask);
	INIT_LIST_HEAD(&eb->lru);
	eb->start = start;
	eb->len = len;
	spin_lock_irqsave(&leak_lock, flags);
	list_add(&eb->leak_list, &buffers);
	spin_unlock_irqrestore(&leak_lock, flags);
	atomic_set(&eb->refs, 1);

	return eb;
}

static void __free_extent_buffer(struct extent_buffer *eb)
{
	unsigned long flags;
	spin_lock_irqsave(&leak_lock, flags);
	list_del(&eb->leak_list);
	spin_unlock_irqrestore(&leak_lock, flags);
	kmem_cache_free(extent_buffer_cache, eb);
}

struct extent_buffer *alloc_extent_buffer(struct extent_io_tree *tree,
					  u64 start, unsigned long len,
					  struct page *page0,
					  gfp_t mask)
{
	unsigned long num_pages = num_extent_pages(start, len);
	unsigned long i;
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	struct extent_buffer *eb;
	struct page *p;
	struct address_space *mapping = tree->mapping;
	int uptodate = 1;

	eb = __alloc_extent_buffer(tree, start, len, mask);
	if (!eb)
		return NULL;

	if (eb->flags & EXTENT_BUFFER_FILLED)
		goto lru_add;

	if (page0) {
		eb->first_page = page0;
		i = 1;
		index++;
		page_cache_get(page0);
		mark_page_accessed(page0);
		set_page_extent_mapped(page0);
		set_page_extent_head(page0, len);
		uptodate = PageUptodate(page0);
	} else {
		i = 0;
	}
	for (; i < num_pages; i++, index++) {
		p = find_or_create_page(mapping, index, mask | __GFP_HIGHMEM);
		if (!p) {
			WARN_ON(1);
			goto fail;
		}
		set_page_extent_mapped(p);
		mark_page_accessed(p);
		if (i == 0) {
			eb->first_page = p;
			set_page_extent_head(p, len);
		} else {
			set_page_private(p, EXTENT_PAGE_PRIVATE);
		}
		if (!PageUptodate(p))
			uptodate = 0;
		unlock_page(p);
	}
	if (uptodate)
		eb->flags |= EXTENT_UPTODATE;
	eb->flags |= EXTENT_BUFFER_FILLED;

lru_add:
	spin_lock(&tree->lru_lock);
	add_lru(tree, eb);
	spin_unlock(&tree->lru_lock);
	return eb;

fail:
	spin_lock(&tree->lru_lock);
	list_del_init(&eb->lru);
	spin_unlock(&tree->lru_lock);
	if (!atomic_dec_and_test(&eb->refs))
		return NULL;
	for (index = 1; index < i; index++) {
		page_cache_release(extent_buffer_page(eb, index));
	}
	if (i > 0)
		page_cache_release(extent_buffer_page(eb, 0));
	__free_extent_buffer(eb);
	return NULL;
}
EXPORT_SYMBOL(alloc_extent_buffer);

struct extent_buffer *find_extent_buffer(struct extent_io_tree *tree,
					 u64 start, unsigned long len,
					  gfp_t mask)
{
	unsigned long num_pages = num_extent_pages(start, len);
	unsigned long i;
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	struct extent_buffer *eb;
	struct page *p;
	struct address_space *mapping = tree->mapping;
	int uptodate = 1;

	eb = __alloc_extent_buffer(tree, start, len, mask);
	if (!eb)
		return NULL;

	if (eb->flags & EXTENT_BUFFER_FILLED)
		goto lru_add;

	for (i = 0; i < num_pages; i++, index++) {
		p = find_lock_page(mapping, index);
		if (!p) {
			goto fail;
		}
		set_page_extent_mapped(p);
		mark_page_accessed(p);

		if (i == 0) {
			eb->first_page = p;
			set_page_extent_head(p, len);
		} else {
			set_page_private(p, EXTENT_PAGE_PRIVATE);
		}

		if (!PageUptodate(p))
			uptodate = 0;
		unlock_page(p);
	}
	if (uptodate)
		eb->flags |= EXTENT_UPTODATE;
	eb->flags |= EXTENT_BUFFER_FILLED;

lru_add:
	spin_lock(&tree->lru_lock);
	add_lru(tree, eb);
	spin_unlock(&tree->lru_lock);
	return eb;
fail:
	spin_lock(&tree->lru_lock);
	list_del_init(&eb->lru);
	spin_unlock(&tree->lru_lock);
	if (!atomic_dec_and_test(&eb->refs))
		return NULL;
	for (index = 1; index < i; index++) {
		page_cache_release(extent_buffer_page(eb, index));
	}
	if (i > 0)
		page_cache_release(extent_buffer_page(eb, 0));
	__free_extent_buffer(eb);
	return NULL;
}
EXPORT_SYMBOL(find_extent_buffer);

void free_extent_buffer(struct extent_buffer *eb)
{
	unsigned long i;
	unsigned long num_pages;

	if (!eb)
		return;

	if (!atomic_dec_and_test(&eb->refs))
		return;

	WARN_ON(!list_empty(&eb->lru));
	num_pages = num_extent_pages(eb->start, eb->len);

	for (i = 1; i < num_pages; i++) {
		page_cache_release(extent_buffer_page(eb, i));
	}
	page_cache_release(extent_buffer_page(eb, 0));
	__free_extent_buffer(eb);
}
EXPORT_SYMBOL(free_extent_buffer);

int clear_extent_buffer_dirty(struct extent_io_tree *tree,
			      struct extent_buffer *eb)
{
	int set;
	unsigned long i;
	unsigned long num_pages;
	struct page *page;

	u64 start = eb->start;
	u64 end = start + eb->len - 1;

	set = clear_extent_dirty(tree, start, end, GFP_NOFS);
	num_pages = num_extent_pages(eb->start, eb->len);

	for (i = 0; i < num_pages; i++) {
		page = extent_buffer_page(eb, i);
		lock_page(page);
		if (i == 0)
			set_page_extent_head(page, eb->len);
		else
			set_page_private(page, EXTENT_PAGE_PRIVATE);

		/*
		 * if we're on the last page or the first page and the
		 * block isn't aligned on a page boundary, do extra checks
		 * to make sure we don't clean page that is partially dirty
		 */
		if ((i == 0 && (eb->start & (PAGE_CACHE_SIZE - 1))) ||
		    ((i == num_pages - 1) &&
		     ((eb->start + eb->len) & (PAGE_CACHE_SIZE - 1)))) {
			start = (u64)page->index << PAGE_CACHE_SHIFT;
			end  = start + PAGE_CACHE_SIZE - 1;
			if (test_range_bit(tree, start, end,
					   EXTENT_DIRTY, 0)) {
				unlock_page(page);
				continue;
			}
		}
		clear_page_dirty_for_io(page);
		read_lock_irq(&page->mapping->tree_lock);
		if (!PageDirty(page)) {
			radix_tree_tag_clear(&page->mapping->page_tree,
						page_index(page),
						PAGECACHE_TAG_DIRTY);
		}
		read_unlock_irq(&page->mapping->tree_lock);
		unlock_page(page);
	}
	return 0;
}
EXPORT_SYMBOL(clear_extent_buffer_dirty);

int wait_on_extent_buffer_writeback(struct extent_io_tree *tree,
				    struct extent_buffer *eb)
{
	return wait_on_extent_writeback(tree, eb->start,
					eb->start + eb->len - 1);
}
EXPORT_SYMBOL(wait_on_extent_buffer_writeback);

int set_extent_buffer_dirty(struct extent_io_tree *tree,
			     struct extent_buffer *eb)
{
	unsigned long i;
	unsigned long num_pages;

	num_pages = num_extent_pages(eb->start, eb->len);
	for (i = 0; i < num_pages; i++) {
		struct page *page = extent_buffer_page(eb, i);
		/* writepage may need to do something special for the
		 * first page, we have to make sure page->private is
		 * properly set.  releasepage may drop page->private
		 * on us if the page isn't already dirty.
		 */
		if (i == 0) {
			lock_page(page);
			set_page_extent_head(page, eb->len);
		} else if (PagePrivate(page) &&
			   page->private != EXTENT_PAGE_PRIVATE) {
			lock_page(page);
			set_page_extent_mapped(page);
			unlock_page(page);
		}
		__set_page_dirty_nobuffers(extent_buffer_page(eb, i));
		if (i == 0)
			unlock_page(page);
	}
	return set_extent_dirty(tree, eb->start,
				eb->start + eb->len - 1, GFP_NOFS);
}
EXPORT_SYMBOL(set_extent_buffer_dirty);

int clear_extent_buffer_uptodate(struct extent_io_tree *tree,
				struct extent_buffer *eb)
{
	unsigned long i;
	struct page *page;
	unsigned long num_pages;

	num_pages = num_extent_pages(eb->start, eb->len);
	eb->flags &= ~EXTENT_UPTODATE;

	clear_extent_uptodate(tree, eb->start, eb->start + eb->len - 1,
			      GFP_NOFS);
	for (i = 0; i < num_pages; i++) {
		page = extent_buffer_page(eb, i);
		ClearPageUptodate(page);
	}
	return 0;
}

int set_extent_buffer_uptodate(struct extent_io_tree *tree,
				struct extent_buffer *eb)
{
	unsigned long i;
	struct page *page;
	unsigned long num_pages;

	num_pages = num_extent_pages(eb->start, eb->len);

	set_extent_uptodate(tree, eb->start, eb->start + eb->len - 1,
			    GFP_NOFS);
	for (i = 0; i < num_pages; i++) {
		page = extent_buffer_page(eb, i);
		if ((i == 0 && (eb->start & (PAGE_CACHE_SIZE - 1))) ||
		    ((i == num_pages - 1) &&
		     ((eb->start + eb->len) & (PAGE_CACHE_SIZE - 1)))) {
			check_page_uptodate(tree, page);
			continue;
		}
		SetPageUptodate(page);
	}
	return 0;
}
EXPORT_SYMBOL(set_extent_buffer_uptodate);

int extent_range_uptodate(struct extent_io_tree *tree,
			  u64 start, u64 end)
{
	struct page *page;
	int ret;
	int pg_uptodate = 1;
	int uptodate;
	unsigned long index;

	ret = test_range_bit(tree, start, end, EXTENT_UPTODATE, 1);
	if (ret)
		return 1;
	while(start <= end) {
		index = start >> PAGE_CACHE_SHIFT;
		page = find_get_page(tree->mapping, index);
		uptodate = PageUptodate(page);
		page_cache_release(page);
		if (!uptodate) {
			pg_uptodate = 0;
			break;
		}
		start += PAGE_CACHE_SIZE;
	}
	return pg_uptodate;
}

int extent_buffer_uptodate(struct extent_io_tree *tree,
			   struct extent_buffer *eb)
{
	int ret = 0;
	unsigned long num_pages;
	unsigned long i;
	struct page *page;
	int pg_uptodate = 1;

	if (eb->flags & EXTENT_UPTODATE)
		return 1;

	ret = test_range_bit(tree, eb->start, eb->start + eb->len - 1,
			   EXTENT_UPTODATE, 1);
	if (ret)
		return ret;

	num_pages = num_extent_pages(eb->start, eb->len);
	for (i = 0; i < num_pages; i++) {
		page = extent_buffer_page(eb, i);
		if (!PageUptodate(page)) {
			pg_uptodate = 0;
			break;
		}
	}
	return pg_uptodate;
}
EXPORT_SYMBOL(extent_buffer_uptodate);

int read_extent_buffer_pages(struct extent_io_tree *tree,
			     struct extent_buffer *eb,
			     u64 start, int wait,
			     get_extent_t *get_extent, int mirror_num)
{
	unsigned long i;
	unsigned long start_i;
	struct page *page;
	int err;
	int ret = 0;
	int locked_pages = 0;
	int all_uptodate = 1;
	int inc_all_pages = 0;
	unsigned long num_pages;
	struct bio *bio = NULL;

	if (eb->flags & EXTENT_UPTODATE)
		return 0;

	if (test_range_bit(tree, eb->start, eb->start + eb->len - 1,
			   EXTENT_UPTODATE, 1)) {
		return 0;
	}

	if (start) {
		WARN_ON(start < eb->start);
		start_i = (start >> PAGE_CACHE_SHIFT) -
			(eb->start >> PAGE_CACHE_SHIFT);
	} else {
		start_i = 0;
	}

	num_pages = num_extent_pages(eb->start, eb->len);
	for (i = start_i; i < num_pages; i++) {
		page = extent_buffer_page(eb, i);
		if (!wait) {
			if (TestSetPageLocked(page))
				goto unlock_exit;
		} else {
			lock_page(page);
		}
		locked_pages++;
		if (!PageUptodate(page)) {
			all_uptodate = 0;
		}
	}
	if (all_uptodate) {
		if (start_i == 0)
			eb->flags |= EXTENT_UPTODATE;
		goto unlock_exit;
	}

	for (i = start_i; i < num_pages; i++) {
		page = extent_buffer_page(eb, i);
		if (inc_all_pages)
			page_cache_get(page);
		if (!PageUptodate(page)) {
			if (start_i == 0)
				inc_all_pages = 1;
			ClearPageError(page);
			err = __extent_read_full_page(tree, page,
						      get_extent, &bio,
						      mirror_num);
			if (err) {
				ret = err;
			}
		} else {
			unlock_page(page);
		}
	}

	if (bio)
		submit_one_bio(READ, bio, mirror_num);

	if (ret || !wait) {
		return ret;
	}
	for (i = start_i; i < num_pages; i++) {
		page = extent_buffer_page(eb, i);
		wait_on_page_locked(page);
		if (!PageUptodate(page)) {
			ret = -EIO;
		}
	}
	if (!ret)
		eb->flags |= EXTENT_UPTODATE;
	return ret;

unlock_exit:
	i = start_i;
	while(locked_pages > 0) {
		page = extent_buffer_page(eb, i);
		i++;
		unlock_page(page);
		locked_pages--;
	}
	return ret;
}
EXPORT_SYMBOL(read_extent_buffer_pages);

void read_extent_buffer(struct extent_buffer *eb, void *dstv,
			unsigned long start,
			unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *dst = (char *)dstv;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & ((unsigned long)PAGE_CACHE_SIZE - 1);

	while(len > 0) {
		page = extent_buffer_page(eb, i);

		cur = min(len, (PAGE_CACHE_SIZE - offset));
		kaddr = kmap_atomic(page, KM_USER1);
		memcpy(dst, kaddr + offset, cur);
		kunmap_atomic(kaddr, KM_USER1);

		dst += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}
EXPORT_SYMBOL(read_extent_buffer);

int map_private_extent_buffer(struct extent_buffer *eb, unsigned long start,
			       unsigned long min_len, char **token, char **map,
			       unsigned long *map_start,
			       unsigned long *map_len, int km)
{
	size_t offset = start & (PAGE_CACHE_SIZE - 1);
	char *kaddr;
	struct page *p;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;
	unsigned long end_i = (start_offset + start + min_len - 1) >>
		PAGE_CACHE_SHIFT;

	if (i != end_i)
		return -EINVAL;

	if (i == 0) {
		offset = start_offset;
		*map_start = 0;
	} else {
		offset = 0;
		*map_start = ((u64)i << PAGE_CACHE_SHIFT) - start_offset;
	}
	if (start + min_len > eb->len) {
printk("bad mapping eb start %Lu len %lu, wanted %lu %lu\n", eb->start, eb->len, start, min_len);
		WARN_ON(1);
	}

	p = extent_buffer_page(eb, i);
	kaddr = kmap_atomic(p, km);
	*token = kaddr;
	*map = kaddr + offset;
	*map_len = PAGE_CACHE_SIZE - offset;
	return 0;
}
EXPORT_SYMBOL(map_private_extent_buffer);

int map_extent_buffer(struct extent_buffer *eb, unsigned long start,
		      unsigned long min_len,
		      char **token, char **map,
		      unsigned long *map_start,
		      unsigned long *map_len, int km)
{
	int err;
	int save = 0;
	if (eb->map_token) {
		unmap_extent_buffer(eb, eb->map_token, km);
		eb->map_token = NULL;
		save = 1;
	}
	err = map_private_extent_buffer(eb, start, min_len, token, map,
				       map_start, map_len, km);
	if (!err && save) {
		eb->map_token = *token;
		eb->kaddr = *map;
		eb->map_start = *map_start;
		eb->map_len = *map_len;
	}
	return err;
}
EXPORT_SYMBOL(map_extent_buffer);

void unmap_extent_buffer(struct extent_buffer *eb, char *token, int km)
{
	kunmap_atomic(token, km);
}
EXPORT_SYMBOL(unmap_extent_buffer);

int memcmp_extent_buffer(struct extent_buffer *eb, const void *ptrv,
			  unsigned long start,
			  unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *ptr = (char *)ptrv;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;
	int ret = 0;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & ((unsigned long)PAGE_CACHE_SIZE - 1);

	while(len > 0) {
		page = extent_buffer_page(eb, i);

		cur = min(len, (PAGE_CACHE_SIZE - offset));

		kaddr = kmap_atomic(page, KM_USER0);
		ret = memcmp(ptr, kaddr + offset, cur);
		kunmap_atomic(kaddr, KM_USER0);
		if (ret)
			break;

		ptr += cur;
		len -= cur;
		offset = 0;
		i++;
	}
	return ret;
}
EXPORT_SYMBOL(memcmp_extent_buffer);

void write_extent_buffer(struct extent_buffer *eb, const void *srcv,
			 unsigned long start, unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	char *src = (char *)srcv;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & ((unsigned long)PAGE_CACHE_SIZE - 1);

	while(len > 0) {
		page = extent_buffer_page(eb, i);
		WARN_ON(!PageUptodate(page));

		cur = min(len, PAGE_CACHE_SIZE - offset);
		kaddr = kmap_atomic(page, KM_USER1);
		memcpy(kaddr + offset, src, cur);
		kunmap_atomic(kaddr, KM_USER1);

		src += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}
EXPORT_SYMBOL(write_extent_buffer);

void memset_extent_buffer(struct extent_buffer *eb, char c,
			  unsigned long start, unsigned long len)
{
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	size_t start_offset = eb->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + start) >> PAGE_CACHE_SHIFT;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	offset = (start_offset + start) & ((unsigned long)PAGE_CACHE_SIZE - 1);

	while(len > 0) {
		page = extent_buffer_page(eb, i);
		WARN_ON(!PageUptodate(page));

		cur = min(len, PAGE_CACHE_SIZE - offset);
		kaddr = kmap_atomic(page, KM_USER0);
		memset(kaddr + offset, c, cur);
		kunmap_atomic(kaddr, KM_USER0);

		len -= cur;
		offset = 0;
		i++;
	}
}
EXPORT_SYMBOL(memset_extent_buffer);

void copy_extent_buffer(struct extent_buffer *dst, struct extent_buffer *src,
			unsigned long dst_offset, unsigned long src_offset,
			unsigned long len)
{
	u64 dst_len = dst->len;
	size_t cur;
	size_t offset;
	struct page *page;
	char *kaddr;
	size_t start_offset = dst->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long i = (start_offset + dst_offset) >> PAGE_CACHE_SHIFT;

	WARN_ON(src->len != dst_len);

	offset = (start_offset + dst_offset) &
		((unsigned long)PAGE_CACHE_SIZE - 1);

	while(len > 0) {
		page = extent_buffer_page(dst, i);
		WARN_ON(!PageUptodate(page));

		cur = min(len, (unsigned long)(PAGE_CACHE_SIZE - offset));

		kaddr = kmap_atomic(page, KM_USER0);
		read_extent_buffer(src, kaddr + offset, src_offset, cur);
		kunmap_atomic(kaddr, KM_USER0);

		src_offset += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}
EXPORT_SYMBOL(copy_extent_buffer);

static void move_pages(struct page *dst_page, struct page *src_page,
		       unsigned long dst_off, unsigned long src_off,
		       unsigned long len)
{
	char *dst_kaddr = kmap_atomic(dst_page, KM_USER0);
	if (dst_page == src_page) {
		memmove(dst_kaddr + dst_off, dst_kaddr + src_off, len);
	} else {
		char *src_kaddr = kmap_atomic(src_page, KM_USER1);
		char *p = dst_kaddr + dst_off + len;
		char *s = src_kaddr + src_off + len;

		while (len--)
			*--p = *--s;

		kunmap_atomic(src_kaddr, KM_USER1);
	}
	kunmap_atomic(dst_kaddr, KM_USER0);
}

static void copy_pages(struct page *dst_page, struct page *src_page,
		       unsigned long dst_off, unsigned long src_off,
		       unsigned long len)
{
	char *dst_kaddr = kmap_atomic(dst_page, KM_USER0);
	char *src_kaddr;

	if (dst_page != src_page)
		src_kaddr = kmap_atomic(src_page, KM_USER1);
	else
		src_kaddr = dst_kaddr;

	memcpy(dst_kaddr + dst_off, src_kaddr + src_off, len);
	kunmap_atomic(dst_kaddr, KM_USER0);
	if (dst_page != src_page)
		kunmap_atomic(src_kaddr, KM_USER1);
}

void memcpy_extent_buffer(struct extent_buffer *dst, unsigned long dst_offset,
			   unsigned long src_offset, unsigned long len)
{
	size_t cur;
	size_t dst_off_in_page;
	size_t src_off_in_page;
	size_t start_offset = dst->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long dst_i;
	unsigned long src_i;

	if (src_offset + len > dst->len) {
		printk("memmove bogus src_offset %lu move len %lu len %lu\n",
		       src_offset, len, dst->len);
		BUG_ON(1);
	}
	if (dst_offset + len > dst->len) {
		printk("memmove bogus dst_offset %lu move len %lu len %lu\n",
		       dst_offset, len, dst->len);
		BUG_ON(1);
	}

	while(len > 0) {
		dst_off_in_page = (start_offset + dst_offset) &
			((unsigned long)PAGE_CACHE_SIZE - 1);
		src_off_in_page = (start_offset + src_offset) &
			((unsigned long)PAGE_CACHE_SIZE - 1);

		dst_i = (start_offset + dst_offset) >> PAGE_CACHE_SHIFT;
		src_i = (start_offset + src_offset) >> PAGE_CACHE_SHIFT;

		cur = min(len, (unsigned long)(PAGE_CACHE_SIZE -
					       src_off_in_page));
		cur = min_t(unsigned long, cur,
			(unsigned long)(PAGE_CACHE_SIZE - dst_off_in_page));

		copy_pages(extent_buffer_page(dst, dst_i),
			   extent_buffer_page(dst, src_i),
			   dst_off_in_page, src_off_in_page, cur);

		src_offset += cur;
		dst_offset += cur;
		len -= cur;
	}
}
EXPORT_SYMBOL(memcpy_extent_buffer);

void memmove_extent_buffer(struct extent_buffer *dst, unsigned long dst_offset,
			   unsigned long src_offset, unsigned long len)
{
	size_t cur;
	size_t dst_off_in_page;
	size_t src_off_in_page;
	unsigned long dst_end = dst_offset + len - 1;
	unsigned long src_end = src_offset + len - 1;
	size_t start_offset = dst->start & ((u64)PAGE_CACHE_SIZE - 1);
	unsigned long dst_i;
	unsigned long src_i;

	if (src_offset + len > dst->len) {
		printk("memmove bogus src_offset %lu move len %lu len %lu\n",
		       src_offset, len, dst->len);
		BUG_ON(1);
	}
	if (dst_offset + len > dst->len) {
		printk("memmove bogus dst_offset %lu move len %lu len %lu\n",
		       dst_offset, len, dst->len);
		BUG_ON(1);
	}
	if (dst_offset < src_offset) {
		memcpy_extent_buffer(dst, dst_offset, src_offset, len);
		return;
	}
	while(len > 0) {
		dst_i = (start_offset + dst_end) >> PAGE_CACHE_SHIFT;
		src_i = (start_offset + src_end) >> PAGE_CACHE_SHIFT;

		dst_off_in_page = (start_offset + dst_end) &
			((unsigned long)PAGE_CACHE_SIZE - 1);
		src_off_in_page = (start_offset + src_end) &
			((unsigned long)PAGE_CACHE_SIZE - 1);

		cur = min_t(unsigned long, len, src_off_in_page + 1);
		cur = min(cur, dst_off_in_page + 1);
		move_pages(extent_buffer_page(dst, dst_i),
			   extent_buffer_page(dst, src_i),
			   dst_off_in_page - cur + 1,
			   src_off_in_page - cur + 1, cur);

		dst_end -= cur;
		src_end -= cur;
		len -= cur;
	}
}
EXPORT_SYMBOL(memmove_extent_buffer);
