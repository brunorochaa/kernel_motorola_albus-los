/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _SDE_FENCE_H_
#define _SDE_FENCE_H_

#include <linux/kernel.h>
#include <linux/errno.h>

#ifdef CONFIG_SYNC
/**
 * sde_sync_get - Query sync fence object from a file handle
 *
 * On success, this function also increments the refcount of the sync fence
 *
 * @fd: Integer sync fence handle
 *
 * Return: Pointer to sync fence object, or NULL
 */
void *sde_sync_get(uint64_t fd);

/**
 * sde_sync_put - Releases a sync fence object acquired by @sde_sync_get
 *
 * This function decrements the sync fence's reference count; the object will
 * be released if the reference count goes to zero.
 *
 * @fence: Pointer to sync fence
 */
void sde_sync_put(void *fence);

/**
 * sde_sync_wait - Query sync fence object from a file handle
 *
 * @fence: Pointer to sync fence
 * @timeout_ms: Time to wait, in milliseconds. Waits forever if timeout_ms < 0
 *
 * Return: Zero on success, or -ETIME on timeout
 */
int sde_sync_wait(void *fence, long timeout_ms);
#else
static inline void *sde_sync_get(uint64_t fd)
{
	return NULL;
}

static inline void sde_sync_put(void *fence)
{
}

static inline int sde_sync_wait(void *fence, long timeout_ms)
{
	return 0;
}
#endif

#if defined(CONFIG_SYNC) && IS_ENABLED(CONFIG_SW_SYNC)
/**
 * sde_sync_timeline_create - Create timeline object
 *
 * @name: Name for timeline
 *
 * Return: Pointer to newly created timeline, or NULL on error
 */
void *sde_sync_timeline_create(const char *name);

/**
 * sde_sync_fence_create - Create fence object
 *
 * This function is NOT thread-safe.
 *
 * @timeline: Timeline to associate with fence
 * @name: Name for fence
 * @val: Timeline value at which to signal the fence, must be >= 0
 *
 * Return: File descriptor on success, or error code on error
 */
int sde_sync_fence_create(void *timeline, const char *name, int val);

/**
 * sde_sync_timeline_inc - Increment timeline object
 *
 * This function is NOT thread-safe.
 *
 * @timeline: Timeline to increment
 * @val: Amount by which to increase the timeline
 *
 * Return: File descriptor on success, or error code on error
 */
void sde_sync_timeline_inc(void *timeline, int val);
#else
static inline void *sde_sync_timeline_create(const char *name)
{
	return NULL;
}

static inline int sde_sync_fence_create(void *timeline,
		const char *name, int val)
{
	return -EINVAL;
}

static inline void sde_sync_timeline_inc(void *timeline, int val)
{
}
#endif /* defined(CONFIG_SYNC) && IS_ENABLED(CONFIG_SW_SYNC) */

#endif /* _SDE_FENCE_H_ */
