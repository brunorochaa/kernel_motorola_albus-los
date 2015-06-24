/*
 * Copyright (c) 2013-2015 TRUSTONIC LIMITED
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef NQ_H_
#define NQ_H_

/** \name NQ Size Defines
 * Minimum and maximum count of elements in the notification queue.
 */
#define MIN_NQ_ELEM 1	/** Minimum notification queue elements */
#define MAX_NQ_ELEM 64	/** Maximum notification queue elements */

/** \name NQ Length Defines
 * Minimum and maximum notification queue length.
 */
/** Minimum notification length (in bytes) */
#define MIN_NQ_LEN (MIN_NQ_ELEM * sizeof(struct notification))
/** Maximum notification length (in bytes) */
#define MAX_NQ_LEN (MAX_NQ_ELEM * sizeof(struct notification))

/** \name Session ID Defines
 * Standard Session IDs.
 */
/** MCP session ID, used to communicate with MobiCore (e.g. to start/stop TA) */
#define SID_MCP       0
/** Invalid session id, returned in case of error */
#define SID_INVALID   0xffffffff

/** Notification data structure */
struct notification {
	uint32_t	session_id;	/** Session ID */
	int32_t		payload;	/** Additional notification info */
};

/** Notification payload codes.
 * 0 indicated a plain simple notification,
 * a positive value is a termination reason from the task,
 * a negative value is a termination reason from MobiCore.
 * Possible negative values are given below.
 */
enum notification_payload {
	/** task terminated, but exit code is invalid */
	ERR_INVALID_EXIT_CODE = -1,
	/** task terminated due to session end, no exit code available */
	ERR_SESSION_CLOSE     = -2,
	/** task terminated due to invalid operation */
	ERR_INVALID_OPERATION = -3,
	/** session ID is unknown */
	ERR_INVALID_SID       = -4,
	/** session is not active */
	ERR_SID_NOT_ACTIVE    = -5,
	/** session was force-killed (due to an administrative command). */
	ERR_SESSION_KILLED    = -6,
};

/** Declaration of the notification queue header.
 * layout as specified in the data structure specification.
 */
struct notification_queue_header {
	uint32_t	write_cnt;	/** Write counter */
	uint32_t	read_cnt;	/** Read counter */
	uint32_t	queue_size;	/** Queue size */
};

/** Queue struct which defines a queue object.
 * The queue struct is accessed by the queue<operation> type of
 * function. elementCnt must be a power of two and the power needs
 * to be smaller than power of uint32_t (obviously 32).
 */
struct notification_queue {
	struct notification_queue_header hdr;		/** Queue header */
	struct notification notification[MIN_NQ_ELEM];	/** Elements */
};

#endif /** NQ_H_ */
