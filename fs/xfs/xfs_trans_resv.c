/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * Copyright (C) 2010 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log.h"
#include "xfs_trans_resv.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_extent_busy.h"
#include "xfs_bmap.h"
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_trans_space.h"
#include "xfs_trace.h"

/*
 * A buffer has a format structure overhead in the log in addition
 * to the data, so we need to take this into account when reserving
 * space in a transaction for a buffer.  Round the space required up
 * to a multiple of 128 bytes so that we don't change the historical
 * reservation that has been used for this overhead.
 */
STATIC uint
xfs_buf_log_overhead(void)
{
	return round_up(sizeof(struct xlog_op_header) +
			sizeof(struct xfs_buf_log_format), 128);
}

/*
 * Calculate out transaction log reservation per item in bytes.
 *
 * The nbufs argument is used to indicate the number of items that
 * will be changed in a transaction.  size is used to tell how many
 * bytes should be reserved per item.
 */
STATIC uint
xfs_calc_buf_res(
	uint		nbufs,
	uint		size)
{
	return nbufs * (size + xfs_buf_log_overhead());
}

/*
 * Various log reservation values.
 *
 * These are based on the size of the file system block because that is what
 * most transactions manipulate.  Each adds in an additional 128 bytes per
 * item logged to try to account for the overhead of the transaction mechanism.
 *
 * Note:  Most of the reservations underestimate the number of allocation
 * groups into which they could free extents in the xfs_bmap_finish() call.
 * This is because the number in the worst case is quite high and quite
 * unusual.  In order to fix this we need to change xfs_bmap_finish() to free
 * extents in only a single AG at a time.  This will require changes to the
 * EFI code as well, however, so that the EFI for the extents not freed is
 * logged again in each transaction.  See SGI PV #261917.
 *
 * Reservation functions here avoid a huge stack in xfs_trans_init due to
 * register overflow from temporaries in the calculations.
 */


/*
 * In a write transaction we can allocate a maximum of 2
 * extents.  This gives:
 *    the inode getting the new extents: inode size
 *    the inode's bmap btree: max depth * block size
 *    the agfs of the ags from which the extents are allocated: 2 * sector
 *    the superblock free block counter: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 * And the bmap_finish transaction can free bmap blocks in a join:
 *    the agfs of the ags containing the blocks: 2 * sector size
 *    the agfls of the ags containing the blocks: 2 * sector size
 *    the super block free block counter: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_write_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((xfs_calc_buf_res(1, mp->m_sb.sb_inodesize) +
		     xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK),
				      XFS_FSB_TO_B(mp, 1)) +
		     xfs_calc_buf_res(3, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 2),
				      XFS_FSB_TO_B(mp, 1))),
		    (xfs_calc_buf_res(5, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 2),
				      XFS_FSB_TO_B(mp, 1))));
}

/*
 * In truncating a file we free up to two extents at once.  We can modify:
 *    the inode being truncated: inode size
 *    the inode's bmap btree: (max depth + 1) * block size
 * And the bmap_finish transaction can free the blocks and bmap blocks:
 *    the agf for each of the ags: 4 * sector size
 *    the agfl for each of the ags: 4 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    worst case split in allocation btrees per extent assuming 4 extents:
 *		4 exts * 2 trees * (2 * max depth - 1) * block size
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
STATIC uint
xfs_calc_itruncate_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((xfs_calc_buf_res(1, mp->m_sb.sb_inodesize) +
		     xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + 1,
				      XFS_FSB_TO_B(mp, 1))),
		    (xfs_calc_buf_res(9, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 4),
				      XFS_FSB_TO_B(mp, 1)) +
		    xfs_calc_buf_res(5, 0) +
		    xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 1),
				     XFS_FSB_TO_B(mp, 1)) +
		    xfs_calc_buf_res(2 + XFS_IALLOC_BLOCKS(mp) +
				     mp->m_in_maxlevels, 0)));
}

/*
 * In renaming a files we can modify:
 *    the four inodes involved: 4 * inode size
 *    the two directory btrees: 2 * (max depth + v2) * dir block size
 *    the two directory bmap btrees: 2 * max depth * block size
 * And the bmap_finish transaction can free dir and bmap blocks (two sets
 *	of bmap blocks) giving:
 *    the agf for the ags in which the blocks live: 3 * sector size
 *    the agfl for the ags in which the blocks live: 3 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 3 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_rename_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((xfs_calc_buf_res(4, mp->m_sb.sb_inodesize) +
		     xfs_calc_buf_res(2 * XFS_DIROP_LOG_COUNT(mp),
				      XFS_FSB_TO_B(mp, 1))),
		    (xfs_calc_buf_res(7, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 3),
				      XFS_FSB_TO_B(mp, 1))));
}

/*
 * For creating a link to an inode:
 *    the parent directory inode: inode size
 *    the linked inode: inode size
 *    the directory btree could split: (max depth + v2) * dir block size
 *    the directory bmap btree could join or split: (max depth + v2) * blocksize
 * And the bmap_finish transaction can free some bmap blocks giving:
 *    the agf for the ag in which the blocks live: sector size
 *    the agfl for the ag in which the blocks live: sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_link_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((xfs_calc_buf_res(2, mp->m_sb.sb_inodesize) +
		     xfs_calc_buf_res(XFS_DIROP_LOG_COUNT(mp),
				      XFS_FSB_TO_B(mp, 1))),
		    (xfs_calc_buf_res(3, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 1),
				      XFS_FSB_TO_B(mp, 1))));
}

/*
 * For removing a directory entry we can modify:
 *    the parent directory inode: inode size
 *    the removed inode: inode size
 *    the directory btree could join: (max depth + v2) * dir block size
 *    the directory bmap btree could join or split: (max depth + v2) * blocksize
 * And the bmap_finish transaction can free the dir and bmap blocks giving:
 *    the agf for the ag in which the blocks live: 2 * sector size
 *    the agfl for the ag in which the blocks live: 2 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_remove_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((xfs_calc_buf_res(2, mp->m_sb.sb_inodesize) +
		     xfs_calc_buf_res(XFS_DIROP_LOG_COUNT(mp),
				      XFS_FSB_TO_B(mp, 1))),
		    (xfs_calc_buf_res(5, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 2),
				      XFS_FSB_TO_B(mp, 1))));
}

/*
 * For create, break it in to the two cases that the transaction
 * covers. We start with the modify case - allocation done by modification
 * of the state of existing inodes - and the allocation case.
 */

/*
 * For create we can modify:
 *    the parent directory inode: inode size
 *    the new inode: inode size
 *    the inode btree entry: block size
 *    the superblock for the nlink flag: sector size
 *    the directory btree: (max depth + v2) * dir block size
 *    the directory inode's bmap btree: (max depth + v2) * block size
 */
STATIC uint
xfs_calc_create_resv_modify(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(2, mp->m_sb.sb_inodesize) +
		xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
		(uint)XFS_FSB_TO_B(mp, 1) +
		xfs_calc_buf_res(XFS_DIROP_LOG_COUNT(mp), XFS_FSB_TO_B(mp, 1));
}

/*
 * For create we can allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the superblock for the nlink flag: sector size
 *    the inode blocks allocated: XFS_IALLOC_BLOCKS * blocksize
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
STATIC uint
xfs_calc_create_resv_alloc(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(2, mp->m_sb.sb_sectsize) +
		mp->m_sb.sb_sectsize +
		xfs_calc_buf_res(XFS_IALLOC_BLOCKS(mp), XFS_FSB_TO_B(mp, 1)) +
		xfs_calc_buf_res(mp->m_in_maxlevels, XFS_FSB_TO_B(mp, 1)) +
		xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

STATIC uint
__xfs_calc_create_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX(xfs_calc_create_resv_alloc(mp),
		    xfs_calc_create_resv_modify(mp));
}

/*
 * For icreate we can allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the superblock for the nlink flag: sector size
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
STATIC uint
xfs_calc_icreate_resv_alloc(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(2, mp->m_sb.sb_sectsize) +
		mp->m_sb.sb_sectsize +
		xfs_calc_buf_res(mp->m_in_maxlevels, XFS_FSB_TO_B(mp, 1)) +
		xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

STATIC uint
xfs_calc_icreate_reservation(xfs_mount_t *mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX(xfs_calc_icreate_resv_alloc(mp),
		    xfs_calc_create_resv_modify(mp));
}

STATIC uint
xfs_calc_create_reservation(
	struct xfs_mount	*mp)
{
	if (xfs_sb_version_hascrc(&mp->m_sb))
		return xfs_calc_icreate_reservation(mp);
	return __xfs_calc_create_reservation(mp);

}

/*
 * Making a new directory is the same as creating a new file.
 */
STATIC uint
xfs_calc_mkdir_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_create_reservation(mp);
}


/*
 * Making a new symplink is the same as creating a new file, but
 * with the added blocks for remote symlink data which can be up to 1kB in
 * length (MAXPATHLEN).
 */
STATIC uint
xfs_calc_symlink_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_create_reservation(mp) +
	       xfs_calc_buf_res(1, MAXPATHLEN);
}

/*
 * In freeing an inode we can modify:
 *    the inode being freed: inode size
 *    the super block free inode counter: sector size
 *    the agi hash list and counters: sector size
 *    the inode btree entry: block size
 *    the on disk inode before ours in the agi hash list: inode cluster size
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
STATIC uint
xfs_calc_ifree_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		xfs_calc_buf_res(1, mp->m_sb.sb_inodesize) +
		xfs_calc_buf_res(2, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(1, XFS_FSB_TO_B(mp, 1)) +
		MAX((__uint16_t)XFS_FSB_TO_B(mp, 1),
		    XFS_INODE_CLUSTER_SIZE(mp)) +
		xfs_calc_buf_res(1, 0) +
		xfs_calc_buf_res(2 + XFS_IALLOC_BLOCKS(mp) +
				 mp->m_in_maxlevels, 0) +
		xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * When only changing the inode we log the inode and possibly the superblock
 * We also add a bit of slop for the transaction stuff.
 */
STATIC uint
xfs_calc_ichange_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		mp->m_sb.sb_inodesize +
		mp->m_sb.sb_sectsize +
		512;

}

/*
 * Growing the data section of the filesystem.
 *	superblock
 *	agi and agf
 *	allocation btrees
 */
STATIC uint
xfs_calc_growdata_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(3, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * Growing the rt section of the filesystem.
 * In the first set of transactions (ALLOC) we allocate space to the
 * bitmap or summary files.
 *	superblock: sector size
 *	agf of the ag from which the extent is allocated: sector size
 *	bmap btree for bitmap/summary inode: max depth * blocksize
 *	bitmap/summary inode: inode size
 *	allocation btrees for 1 block alloc: 2 * (2 * maxdepth - 1) * blocksize
 */
STATIC uint
xfs_calc_growrtalloc_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(2, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK),
				 XFS_FSB_TO_B(mp, 1)) +
		xfs_calc_buf_res(1, mp->m_sb.sb_inodesize) +
		xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * Growing the rt section of the filesystem.
 * In the second set of transactions (ZERO) we zero the new metadata blocks.
 *	one bitmap/summary block: blocksize
 */
STATIC uint
xfs_calc_growrtzero_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_blocksize);
}

/*
 * Growing the rt section of the filesystem.
 * In the third set of transactions (FREE) we update metadata without
 * allocating any new blocks.
 *	superblock: sector size
 *	bitmap inode: inode size
 *	summary inode: inode size
 *	one bitmap block: blocksize
 *	summary blocks: new summary size
 */
STATIC uint
xfs_calc_growrtfree_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(2, mp->m_sb.sb_inodesize) +
		xfs_calc_buf_res(1, mp->m_sb.sb_blocksize) +
		xfs_calc_buf_res(1, mp->m_rsumsize);
}

/*
 * Logging the inode modification timestamp on a synchronous write.
 *	inode
 */
STATIC uint
xfs_calc_swrite_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_inodesize);
}

/*
 * Logging the inode mode bits when writing a setuid/setgid file
 *	inode
 */
STATIC uint
xfs_calc_writeid_reservation(xfs_mount_t *mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_inodesize);
}

/*
 * Converting the inode from non-attributed to attributed.
 *	the inode being converted: inode size
 *	agf block and superblock (for block allocation)
 *	the new block (directory sized)
 *	bmap blocks for the new directory block
 *	allocation btrees
 */
STATIC uint
xfs_calc_addafork_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		xfs_calc_buf_res(1, mp->m_sb.sb_inodesize) +
		xfs_calc_buf_res(2, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(1, mp->m_dirblksize) +
		xfs_calc_buf_res(XFS_DAENTER_BMAP1B(mp, XFS_DATA_FORK) + 1,
				 XFS_FSB_TO_B(mp, 1)) +
		xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * Removing the attribute fork of a file
 *    the inode being truncated: inode size
 *    the inode's bmap btree: max depth * block size
 * And the bmap_finish transaction can free the blocks and bmap blocks:
 *    the agf for each of the ags: 4 * sector size
 *    the agfl for each of the ags: 4 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    worst case split in allocation btrees per extent assuming 4 extents:
 *		4 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_attrinval_reservation(
	struct xfs_mount	*mp)
{
	return MAX((xfs_calc_buf_res(1, mp->m_sb.sb_inodesize) +
		    xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK),
				     XFS_FSB_TO_B(mp, 1))),
		   (xfs_calc_buf_res(9, mp->m_sb.sb_sectsize) +
		    xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 4),
				     XFS_FSB_TO_B(mp, 1))));
}

/*
 * Setting an attribute at mount time.
 *	the inode getting the attribute
 *	the superblock for allocations
 *	the agfs extents are allocated from
 *	the attribute btree * max depth
 *	the inode allocation btree
 * Since attribute transaction space is dependent on the size of the attribute,
 * the calculation is done partially at mount time and partially at runtime(see
 * below).
 */
STATIC uint
xfs_calc_attrsetm_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		xfs_calc_buf_res(1, mp->m_sb.sb_inodesize) +
		xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(XFS_DA_NODE_MAXDEPTH, XFS_FSB_TO_B(mp, 1));
}

/*
 * Setting an attribute at runtime, transaction space unit per block.
 * 	the superblock for allocations: sector size
 *	the inode bmap btree could join or split: max depth * block size
 * Since the runtime attribute transaction space is dependent on the total
 * blocks needed for the 1st bmap, here we calculate out the space unit for
 * one block so that the caller could figure out the total space according
 * to the attibute extent length in blocks by: ext * XFS_ATTRSETRT_LOG_RES(mp).
 */
STATIC uint
xfs_calc_attrsetrt_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * Removing an attribute.
 *    the inode: inode size
 *    the attribute btree could join: max depth * block size
 *    the inode bmap btree could join or split: max depth * block size
 * And the bmap_finish transaction can free the attr blocks freed giving:
 *    the agf for the ag in which the blocks live: 2 * sector size
 *    the agfl for the ag in which the blocks live: 2 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_attrrm_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((xfs_calc_buf_res(1, mp->m_sb.sb_inodesize) +
		     xfs_calc_buf_res(XFS_DA_NODE_MAXDEPTH,
				      XFS_FSB_TO_B(mp, 1)) +
		     (uint)XFS_FSB_TO_B(mp,
					XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)) +
		     xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK), 0)),
		    (xfs_calc_buf_res(5, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(XFS_ALLOCFREE_LOG_COUNT(mp, 2),
				      XFS_FSB_TO_B(mp, 1))));
}

/*
 * Clearing a bad agino number in an agi hash bucket.
 */
STATIC uint
xfs_calc_clear_agi_bucket_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize);
}

/*
 * Clearing the quotaflags in the superblock.
 *	the super block for changing quota flags: sector size
 */
STATIC uint
xfs_calc_qm_sbchange_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize);
}

/*
 * Adjusting quota limits.
 *    the xfs_disk_dquot_t: sizeof(struct xfs_disk_dquot)
 */
STATIC uint
xfs_calc_qm_setqlim_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, sizeof(struct xfs_disk_dquot));
}

/*
 * Allocating quota on disk if needed.
 *	the write transaction log space: XFS_WRITE_LOG_RES(mp)
 *	the unit of quota allocation: one system block size
 */
STATIC uint
xfs_calc_qm_dqalloc_reservation(
	struct xfs_mount	*mp)
{
	return XFS_WRITE_LOG_RES(mp) +
		xfs_calc_buf_res(1,
			XFS_FSB_TO_B(mp, XFS_DQUOT_CLUSTER_SIZE_FSB) - 1);
}

/*
 * Turning off quotas.
 *    the xfs_qoff_logitem_t: sizeof(struct xfs_qoff_logitem) * 2
 *    the superblock for the quota flags: sector size
 */
STATIC uint
xfs_calc_qm_quotaoff_reservation(
	struct xfs_mount	*mp)
{
	return sizeof(struct xfs_qoff_logitem) * 2 +
		xfs_calc_buf_res(1, mp->m_sb.sb_sectsize);
}

/*
 * End of turning off quotas.
 *    the xfs_qoff_logitem_t: sizeof(struct xfs_qoff_logitem) * 2
 */
STATIC uint
xfs_calc_qm_quotaoff_end_reservation(
	struct xfs_mount	*mp)
{
	return sizeof(struct xfs_qoff_logitem) * 2;
}

/*
 * Syncing the incore super block changes to disk.
 *     the super block to reflect the changes: sector size
 */
STATIC uint
xfs_calc_sb_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize);
}

void
xfs_trans_resv_calc(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	resp->tr_write = xfs_calc_write_reservation(mp);
	resp->tr_itruncate = xfs_calc_itruncate_reservation(mp);
	resp->tr_rename = xfs_calc_rename_reservation(mp);
	resp->tr_link = xfs_calc_link_reservation(mp);
	resp->tr_remove = xfs_calc_remove_reservation(mp);
	resp->tr_symlink = xfs_calc_symlink_reservation(mp);
	resp->tr_create = xfs_calc_create_reservation(mp);
	resp->tr_mkdir = xfs_calc_mkdir_reservation(mp);
	resp->tr_ifree = xfs_calc_ifree_reservation(mp);
	resp->tr_ichange = xfs_calc_ichange_reservation(mp);
	resp->tr_growdata = xfs_calc_growdata_reservation(mp);
	resp->tr_swrite = xfs_calc_swrite_reservation(mp);
	resp->tr_writeid = xfs_calc_writeid_reservation(mp);
	resp->tr_addafork = xfs_calc_addafork_reservation(mp);
	resp->tr_attrinval = xfs_calc_attrinval_reservation(mp);
	resp->tr_attrsetm = xfs_calc_attrsetm_reservation(mp);
	resp->tr_attrsetrt = xfs_calc_attrsetrt_reservation(mp);
	resp->tr_attrrm = xfs_calc_attrrm_reservation(mp);
	resp->tr_clearagi = xfs_calc_clear_agi_bucket_reservation(mp);
	resp->tr_growrtalloc = xfs_calc_growrtalloc_reservation(mp);
	resp->tr_growrtzero = xfs_calc_growrtzero_reservation(mp);
	resp->tr_growrtfree = xfs_calc_growrtfree_reservation(mp);
	resp->tr_qm_sbchange = xfs_calc_qm_sbchange_reservation(mp);
	resp->tr_qm_setqlim = xfs_calc_qm_setqlim_reservation(mp);
	resp->tr_qm_dqalloc = xfs_calc_qm_dqalloc_reservation(mp);
	resp->tr_qm_quotaoff = xfs_calc_qm_quotaoff_reservation(mp);
	resp->tr_qm_equotaoff = xfs_calc_qm_quotaoff_end_reservation(mp);
	resp->tr_sb = xfs_calc_sb_reservation(mp);
}
