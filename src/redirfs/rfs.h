/*
 * RedirFS: Redirecting File System
 * Written by Frantisek Hrbata <frantisek.hrbata@redirfs.org>
 *
 * History:
 * 2017 - Slava Imameev made the following changes
 *        - modification for 4.x kernels
 *        - extended functionality
 *        - new operation IDs model
 *        - new hooks model with a shared hook vector
 *        - new objects model
 *
 * Copyright 2008 - 2010 Frantisek Hrbata
 * All rights reserved.
 *
 * This file is part of RedirFS.
 *
 * RedirFS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RedirFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RedirFS. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _RFS_H
#define _RFS_H

#include <linux/mount.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/quotaops.h>
#include <linux/slab.h>
#include "redirfs.h"
#include "rfs_object.h"
#include "rfs_dbg.h"

#ifndef f_dentry
    #define f_dentry    f_path.dentry
#endif

#ifndef f_vfsmnt
    #define f_vfsmnt    f_path.mnt
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0))
    #define f_inode     f_path.dentry->d_inode
#endif

/*
 * do not replace NULL operations to preserve file system driver semantics
 */
#define RFS_ADD_OP(ops_new, ops_old, op, f) \
    ((ops_old && ops_old->op && ops_new.op != f) ? (ops_new.op = f) : (void)0)
    
#define RFS_ADD_OP_MGT(ops_new, ops_old, op, f) \
    ((ops_new.op != f) ? (ops_new.op = f) : (void)0)

#define RFS_REM_OP(ops_new, ops_old, op) \
    (ops_new.op = (ops_old ? ops_old->op : NULL))

/*
 * if there is a filter registered for this operation then hook it
 */
#define RFS_SET_OP(arr, idc, ops_new, ops_old, op, f) \
    ((arr[RFS_IDC_TO_ITYPE(idc)][RFS_IDC_TO_OP_ID(idc)]) ? \
         RFS_ADD_OP(ops_new, ops_old, op, f) : \
         RFS_REM_OP(ops_new, ops_old, op) \
    )

/*---------------------------------------------------------------------------*/

#ifdef RFS_PER_OBJECT_OPS

    #define RFS_IS_FOP_SET(rf, idc) (true)

    #define RFS_SET_FOP(rf, idc, op, f) \
        (rf->rdentry->rinfo->rops ? \
            RFS_SET_OP(rf->rdentry->rinfo->rops->arr, idc, rf->op_new, \
                rf->op_old, op, f) : \
            RFS_REM_OP(rf->op_new, rf->op_old, op) \
        )

#else /* RFS_PER_OBJECT_OPS */

    /*
    * for a shared operations structure we can only add new operations
    * as the shared vector is a union of all vectors, instead of removal 
    * operation the per-file op_bitfield is used
    */

    #define RFS_FOP_BIT(idc) (RFS_IDC_TO_OP_ID(idc) - RFS_OP_f_start)

    #define RFS_IS_FOP_SET(rf, idc) (test_bit(RFS_FOP_BIT(idc), rf->f_op_bitfield))

    #define RFS_SET_FOP(rf, idc, op, f) \
        do { \
            int nr = RFS_FOP_BIT(idc); \
            if (rf->rdentry->rinfo->rops && \
                rf->rdentry->rinfo->rops->arr[RFS_IDC_TO_ITYPE(idc)][RFS_IDC_TO_OP_ID(idc)]) { \
                if (!test_bit(nr, rf->f_rhops->f_op_bitfield) && \
                    !test_and_set_bit(nr, rf->f_rhops->f_op_bitfield)) { \
                    RFS_ADD_OP((*rf->f_rhops->new.f_op), rf->f_rhops->old.f_op, op, f); \
                } \
                set_bit(nr, rf->f_op_bitfield); \
            } else if (test_bit(nr, rf->f_op_bitfield)) { \
                clear_bit(nr, rf->f_op_bitfield); \
            } \
        } while(0);

    #define RFS_SET_FOP_MGT(rf, idc, op, f) \
        do { \
            int nr = RFS_FOP_BIT(idc); \
            if (!test_bit(nr, rf->f_rhops->f_op_bitfield) && \
                !test_and_set_bit(nr, rf->f_rhops->f_op_bitfield)) { \
                RFS_ADD_OP_MGT((*rf->f_rhops->new.f_op), rf->f_rhops->old.f_op, op, f); \
            } \
            if (!test_bit(nr, rf->f_op_bitfield)) \
                set_bit(nr, rf->f_op_bitfield); \
        } while(0);

#endif /* !RFS_PEROBJECT_OPS */

/*---------------------------------------------------------------------------*/

#ifdef RFS_PER_OBJECT_OPS

    #define RFS_IS_DOP_SET(rd, idc) (true)

    #define RFS_SET_DOP(rd, idc, op, f) \
        (rd->rinfo->rops ? \
            RFS_SET_OP(rd->rinfo->rops->arr, idc, rd->op_new,\
                rd->op_old, op, f) : \
            RFS_REM_OP(rd->op_new, rd->op_old, op) \
        )

#else /* RFS_PER_OBJECT_OPS */

    /*
    * for a shared operations structure we can only add new operations
    * as the shared vector is a union of all vectors, instead of removal 
    * operation the per-file op_bitfield is used
    */

    #define RFS_DOP_BIT(idc) (RFS_IDC_TO_OP_ID(idc) - RFS_OP_d_start)

    #define RFS_IS_DOP_SET(rd, idc) (test_bit(RFS_DOP_BIT(idc), rd->d_op_bitfield))

    #define RFS_SET_DOP(rd, idc, op, f) \
        do { \
            int nr = RFS_DOP_BIT(idc); \
            if (rd->rinfo->rops && \
                rd->rinfo->rops->arr[RFS_IDC_TO_ITYPE(idc)][RFS_IDC_TO_OP_ID(idc)]) { \
                if (!test_bit(nr, rd->d_rhops->d_op_bitfield) && \
                    !test_and_set_bit(nr, rd->d_rhops->d_op_bitfield)) { \
                    RFS_ADD_OP((*rd->d_rhops->new.d_op), rd->d_rhops->old.d_op, op, f); \
                } \
                set_bit(nr, rd->d_op_bitfield); \
            } else if (test_bit(nr, rd->d_op_bitfield)) { \
                clear_bit(nr, rd->d_op_bitfield); \
            } \
        } while(0);

    #define RFS_SET_DOP_MGT(rd, idc, op, f) \
        do { \
            int nr = RFS_DOP_BIT(idc); \
            if (!test_bit(nr, rd->d_rhops->d_op_bitfield) && \
                !test_and_set_bit(nr, rd->d_rhops->d_op_bitfield)) { \
                RFS_ADD_OP_MGT((*rd->d_rhops->new.d_op), rd->d_rhops->old.d_op, op, f); \
            } \
            if (!test_bit(nr, rd->d_op_bitfield)) \
                set_bit(nr, rd->d_op_bitfield); \
        } while(0);

#endif /* !RFS_PEROBJECT_OPS */

/*---------------------------------------------------------------------------*/

#ifdef RFS_PER_OBJECT_OPS

    #define RFS_IS_IOP_SET(rf, idc) (true)

    #define RFS_SET_IOP_MGT(ri, idc, op, f) \
        (ri->rinfo->rops ? \
            RFS_ADD_OP(ri->op_new, ri->op_old, op, f) : \
            RFS_REM_OP(ri->op_new, ri->op_old, op) \
        )

    #define RFS_SET_IOP(ri, idc, op, f) \
        (ri->rinfo->rops ? \
            RFS_SET_OP(ri->rinfo->rops->arr, idc, ri->op_new, \
                ri->op_old, op, f) : \
            RFS_REM_OP(ri->op_new, ri->op_old, op) \
        )

#else /* RFS_PER_OBJECT_OPS */

    /*
    * for a shared operations structure we can only add new operations
    * as the shared vector is a union of all vectors, instead of removal 
    * operation the per-file op_bitfield is used
    */

    #define RFS_IOP_BIT(idc) (RFS_IDC_TO_OP_ID(idc) - RFS_OP_i_start)

    #define RFS_IS_IOP_SET(ri, idc) (test_bit(RFS_IOP_BIT(idc), ri->i_op_bitfield))

    #define RFS_SET_IOP(ri, idc, op, f) \
        do { \
            int nr = RFS_IOP_BIT(idc); \
            if (ri->rinfo->rops && \
                ri->rinfo->rops->arr[RFS_IDC_TO_ITYPE(idc)][RFS_IDC_TO_OP_ID(idc)]) { \
                if (!test_bit(nr, ri->i_rhops->i_op_bitfield) && \
                    !test_and_set_bit(nr, ri->i_rhops->i_op_bitfield)) { \
                    RFS_ADD_OP((*ri->i_rhops->new.i_op), ri->i_rhops->old.i_op, op, f); \
                } \
                set_bit(nr, ri->i_op_bitfield); \
            } else if (test_bit(nr, ri->i_op_bitfield)) { \
                clear_bit(nr, ri->i_op_bitfield); \
            } \
        } while(0);

    #define RFS_SET_IOP_MGT(ri, idc, op, f) \
        do { \
            int nr = RFS_IOP_BIT(idc); \
            if (!test_bit(nr, ri->i_rhops->i_op_bitfield) && \
                !test_and_set_bit(nr, ri->i_rhops->i_op_bitfield)) { \
                RFS_ADD_OP_MGT((*ri->i_rhops->new.i_op), ri->i_rhops->old.i_op, op, f); \
            } \
            if (!test_bit(nr, ri->i_op_bitfield)) \
                set_bit(nr, ri->i_op_bitfield); \
        } while(0);

#endif /* !RFS_PER_OBJECT_OPS */

/*---------------------------------------------------------------------------*/

#ifdef RFS_PER_OBJECT_OPS

    #define RFS_IS_AOP_SET(ri, idc) (true)

    #define RFS_SET_AOP(ri, idc, op, f) \
        (ri->rinfo->rops ? \
            RFS_SET_OP(ri->rinfo->rops->arr, idc, ri->a_op_new, \
                ri->a_op_old, op, f) : \
            RFS_REM_OP(ri->a_op_new, ri->a_op_old, op) \
        )
    
#else

    #define RFS_AOP_BIT(idc) (RFS_IDC_TO_OP_ID(idc) - RFS_OP_a_start)

#if 1
    #define RFS_IS_AOP_SET(ri, idc) (test_bit(RFS_AOP_BIT(idc), ri->a_op_bitfield))
#else
    #define RFS_IS_AOP_SET(ri, idc) (test_bit(RFS_IOP_BIT(idc), ri->a_op_bitfield))
#endif
    #define RFS_SET_AOP(ri, idc, op, f) \
        do { \
            int nr = RFS_AOP_BIT(idc); \
            if (ri->rinfo->rops && \
                ri->rinfo->rops->arr[RFS_IDC_TO_ITYPE(idc)][RFS_IDC_TO_OP_ID(idc)]) { \
                if (!test_bit(nr, ri->a_rhops->a_op_bitfield) && \
                    !test_and_set_bit(nr, ri->a_rhops->a_op_bitfield)) { \
                    RFS_ADD_OP((*ri->a_rhops->new.a_op), ri->a_rhops->old.a_op, op, f); \
                } \
                set_bit(nr, ri->a_op_bitfield); \
            } else if (test_bit(nr, ri->a_op_bitfield)) { \
                clear_bit(nr, ri->a_op_bitfield); \
            } \
        } while(0);

#endif

struct rfs_file;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))
    #define rfs_for_each_d_child(pos, head) list_for_each_entry(pos, head, d_child)
    #define rfs_d_child_entry(pos) list_entry(pos, struct dentry, d_child)
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0))
    #define rfs_for_each_d_child(pos, head) list_for_each_entry(pos, head, d_u.d_child)
    #define rfs_d_child_entry(pos) list_entry(pos, struct dentry, d_u.d_child)
#else
    #define rfs_for_each_d_child(pos, head) list_for_each_entry(pos, head, d_child)
    #define rfs_d_child_entry(pos) list_entry(pos, struct dentry, d_child)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))

    #define rfs_mutex_t semaphore
    #define RFS_DEFINE_MUTEX(mutex) DECLARE_MUTEX(mutex)
    #define rfs_mutex_init(mutex) init_MUTEX(mutex)
    #define rfs_mutex_lock(mutex) down(mutex)
    #define rfs_mutex_unlock(mutex) up(mutex)

    inline static void rfs_inode_mutex_lock(struct inode *inode)
    {
        down(&inode->i_sem);
    }
    inline static void rfs_inode_mutex_unlock(struct inode *inode)
    {
        up(&inode->i_sem);
    }

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0))

    #define rfs_mutex_t mutex
    #define RFS_DEFINE_MUTEX(mutex) DEFINE_MUTEX(mutex)
    #define rfs_mutex_init(mutex) mutex_init(mutex)
    #define rfs_mutex_lock(mutex) mutex_lock(mutex)
    #define rfs_mutex_unlock(mutex) mutex_unlock(mutex)

    inline static void rfs_inode_mutex_lock(struct inode *inode)
    {
        mutex_lock(&inode->i_mutex);
    }
    inline static void rfs_inode_mutex_unlock(struct inode *inode)
    {
        mutex_unlock(&inode->i_mutex);
    }

#else

    #define rfs_mutex_t rw_semaphore
    #define RFS_DEFINE_MUTEX(mutex) DECLARE_RWSEM(mutex)
    #define rfs_mutex_init(mutex) init_rwsem(mutex)
    #define rfs_mutex_lock(mutex) down_write(mutex)
    #define rfs_mutex_unlock(mutex) up_write(mutex)

    inline static void rfs_inode_mutex_lock(struct inode *inode)
    {
        down_write(&inode->i_rwsem);
    }
    inline static void rfs_inode_mutex_unlock(struct inode *inode)
    {
        up_write(&inode->i_rwsem);
    }
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))
    #define rfs_kmem_cache_t kmem_cache_t
#else
    #define rfs_kmem_cache_t struct kmem_cache
#endif

struct rfs_op_info {
    enum redirfs_rv (*pre_cb)(redirfs_context, struct redirfs_args *);
    enum redirfs_rv (*post_cb)(redirfs_context, struct redirfs_args *);
};

struct rfs_flt {
    struct list_head list;
    struct rfs_op_info cbs[RFS_INODE_MAX][RFS_OP_MAX];
    struct module *owner;
    struct kobject kobj;
    char *name;
    int priority;
    int paths_nr;
    spinlock_t lock;
    atomic_t active;
    atomic_t count;
    struct redirfs_filter_operations *ops;
};

void rfs_flt_put(struct rfs_flt *rflt);
struct rfs_flt *rfs_flt_get(struct rfs_flt *rflt);
void rfs_flt_release(struct kobject *kobj);

struct rfs_path {
    struct list_head list;
    struct list_head rfst_list;
    struct list_head rroot_list;
    struct rfs_root *rroot;
    struct rfs_chain *rinch;
    struct rfs_chain *rexch;
#ifdef RFS_PATH_WITH_MNT
    struct vfsmount *mnt;
#endif
    char*  pathname;
    struct dentry *dentry;
    atomic_t count;
    int id;
};

extern struct rfs_mutex_t rfs_path_mutex;

struct rfs_path *rfs_path_get(struct rfs_path *rpath);
void rfs_path_put(struct rfs_path *rpath);
struct rfs_path *rfs_path_find(struct vfsmount *mnt, struct dentry *dentry);
struct rfs_path *rfs_path_find_id(int id);
int rfs_path_get_info(struct rfs_flt *rflt, char *buf, int size);
int rfs_fsrename(struct inode *old_dir, struct dentry *old_dentry,
        struct inode *new_dir, struct dentry *new_dentry);

struct rfs_root {
    struct list_head list;
    struct list_head walk_list;
    struct list_head rpaths;
    struct list_head data;
    struct rfs_chain *rinch;
    struct rfs_chain *rexch;
    struct rfs_info *rinfo;
    struct dentry *dentry;
    int paths_nr;
    spinlock_t lock;
    atomic_t count;
};

extern struct list_head rfs_root_list;
extern struct list_head rfs_root_walk_list;

struct rfs_root *rfs_root_get(struct rfs_root *rroot);
void rfs_root_put(struct rfs_root *rroot);
void rfs_root_add_rpath(struct rfs_root *rroot, struct rfs_path *rpath);
void rfs_root_rem_rpath(struct rfs_root *rroot, struct rfs_path *rpath);
struct rfs_root *rfs_root_add(struct dentry *dentry);
int rfs_root_add_include(struct rfs_root *rroot, struct rfs_flt *rflt);
int rfs_root_add_exclude(struct rfs_root *rroot, struct rfs_flt *rflt);
int rfs_root_rem_include(struct rfs_root *rroot, struct rfs_flt *rflt);
int rfs_root_rem_exclude(struct rfs_root *rroot, struct rfs_flt *rflt);
int rfs_root_add_flt(struct rfs_root *rroot, void *data);
int rfs_root_rem_flt(struct rfs_root *rroot, void *data);
int rfs_root_walk(int (*cb)(struct rfs_root*, void *), void *data);
void rfs_root_add_walk(struct dentry *dentry);
void rfs_root_set_rinfo(struct rfs_root *rroot, struct rfs_info *rinfo);

struct rfs_ops {

    //
    // reference count
    //
    atomic_t count;

    int flags;

    //
    // arr[][] counts the number of registered filters for each operations,
    // see redirfs_op_id for the full list of operations indexed by this
    // array, this allows register up to 127 filters ( pre and post callbacks
    // are accounted separatelly ) for each inode type
    //
    unsigned char arr[RFS_INODE_MAX][RFS_OP_MAX];
};

struct rfs_ops *rfs_ops_alloc(void);
struct rfs_ops *rfs_ops_get(struct rfs_ops *rops);
void rfs_ops_put(struct rfs_ops *rops);

struct rfs_chain {
    struct rfs_flt **rflts;
    int rflts_nr;
    atomic_t count;
};

struct rfs_chain *rfs_chain_get(struct rfs_chain *rchain);
void rfs_chain_put(struct rfs_chain *rchain);
int rfs_chain_find(struct rfs_chain *rchain, struct rfs_flt *rflt);
struct rfs_chain *rfs_chain_add(struct rfs_chain *rchain, struct rfs_flt *rflt);
struct rfs_chain *rfs_chain_rem(struct rfs_chain *rchain, struct rfs_flt *rflt);
void rfs_chain_ops(struct rfs_chain *rchain, struct rfs_ops *ops);
int rfs_chain_cmp(struct rfs_chain *rch1, struct rfs_chain *rch2);
struct rfs_chain *rfs_chain_join(struct rfs_chain *rch1,
        struct rfs_chain *rch2);
struct rfs_chain *rfs_chain_diff(struct rfs_chain *rch1,
        struct rfs_chain *rch2);

struct rfs_info {
    struct rfs_chain *rchain;
    struct rfs_ops *rops;
    struct rfs_root *rroot;
    atomic_t count;
};

extern struct rfs_info *rfs_info_none;

struct rfs_info *rfs_info_alloc(struct rfs_root *rroot,
        struct rfs_chain *rchain);
struct rfs_info *rfs_info_get(struct rfs_info *rinfo);
void rfs_info_put(struct rfs_info *rinfo);
struct rfs_info *rfs_info_parent(struct dentry *dentry);
int rfs_info_add_include(struct rfs_root *rroot, struct rfs_flt *rflt);
int rfs_info_add_exclude(struct rfs_root *rroot, struct rfs_flt *rflt);
int rfs_info_rem_include(struct rfs_root *rroot, struct rfs_flt *rflt);
int rfs_info_rem_exclude(struct rfs_root *rroot, struct rfs_flt *rflt);
int rfs_info_add(struct dentry *dentry, struct rfs_info *rinfo,
        struct rfs_flt *rflt);
int rfs_info_rem(struct dentry *dentry, struct rfs_info *rinfo,
        struct rfs_flt *rflt);
int rfs_info_set(struct dentry *dentry, struct rfs_info *rinfo,
        struct rfs_flt *rflt);
int rfs_info_reset(struct dentry *dentry, struct rfs_info *rinfo);

struct rfs_dentry {
#ifdef RFS_DBG
    #define RFS_DENTRY_SIGNATURE  0xABCD0005
    uint32_t   signature;
#endif /* RFS_DBG */
    struct rfs_object robject;
    struct list_head rinode_list;
    struct list_head rfiles;
    struct list_head data;
    struct dentry *dentry;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
    const struct dentry_operations *op_old;
#else
    struct dentry_operations *op_old;
#endif
#ifdef RFS_PER_OBJECT_OPS
    struct dentry_operations op_new;
#else /* RFS_PER_OBJECT_OPS */
    struct rfs_hoperations* d_rhops;
    /* a mask of hooked operations for a file */
    unsigned long   d_op_bitfield[BIT_WORD(RFS_OP_d_end-RFS_OP_d_start) + 1];
#endif /* !RFS_PER_OBJECT_OPS */
    struct rfs_inode *rinode;
    struct rfs_info *rinfo;
    spinlock_t lock;
}; 

struct rfs_dentry* rfs_dentry_find(const struct dentry *dentry);

void rfs_d_iput(struct dentry *dentry, struct inode *inode);
struct rfs_dentry *rfs_dentry_get(struct rfs_dentry *rdentry);
void rfs_dentry_put(struct rfs_dentry *rdentry);
struct rfs_dentry *rfs_dentry_add(struct dentry *dentry,
        struct rfs_info *rinfo);
void rfs_dentry_del(struct rfs_dentry *rdentry);
int rfs_dentry_add_rinode(struct rfs_dentry *rdentry, struct rfs_info *rinfo);
void rfs_dentry_rem_rinode(struct rfs_dentry *rdentry);
struct rfs_info *rfs_dentry_get_rinfo(struct rfs_dentry *rdentry);
void rfs_dentry_set_rinfo(struct rfs_dentry *rdentry, struct rfs_info *rinfo);
void rfs_dentry_add_rfile(struct rfs_dentry *rdentry, struct rfs_file *rfile);
void rfs_dentry_rem_rfile(struct rfs_file *rfile);
void rfs_dentry_rem_rfiles(struct rfs_dentry *rdentry);
void rfs_dentry_set_ops(struct rfs_dentry *dentry);
int rfs_dentry_cache_create(void);
void rfs_dentry_cache_destory(void);
void rfs_dentry_rem_data(struct dentry *dentry, struct rfs_flt *rflt);
int rfs_dentry_move(struct dentry *dentry, struct rfs_flt *rflt,
        struct rfs_root *src, struct rfs_root *dst);

struct rfs_inode {
#ifdef RFS_DBG
    #define RFS_INODE_SIGNATURE  0xABCD0002
    uint32_t   signature;
#endif // RFS_DBG
    struct rfs_object robject;
    struct list_head rdentries; /* mutex */
    struct list_head data;
    struct inode *inode;
    struct file_operations f_op_new;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17))
    const struct inode_operations           *op_old;
    const struct file_operations            *f_op_old;
    const struct address_space_operations   *a_op_old;
#else
    struct inode_operations         *op_old;
    struct file_operations          *f_op_old;
    struct address_space_operations *a_op_old;
#endif
#ifdef RFS_PER_OBJECT_OPS
    struct inode_operations         op_new;
    struct address_space_operations a_op_new;
#else
    struct rfs_hoperations *i_rhops;
    struct rfs_hoperations *a_rhops;
    /* a mask of hooked operations for an inode */
    unsigned long   i_op_bitfield[BIT_WORD(RFS_OP_i_end-RFS_OP_i_start) + 1];
    unsigned long   a_op_bitfield[BIT_WORD(RFS_OP_a_end-RFS_OP_a_start) + 1];
#endif
    struct rfs_info *rinfo;
    struct rfs_mutex_t mutex;
    spinlock_t lock;
    atomic_t nlink;
    int rdentries_nr; /* mutex */
};

struct rfs_inode* rfs_inode_find(struct inode *inode);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0))
int rfs_rename(struct inode *old_dir, struct dentry *old_dentry,
        struct inode *new_dir, struct dentry *new_dentry);
#else
int rfs_rename(struct inode *old_dir, struct dentry *old_dentry,
        struct inode *new_dir, struct dentry *new_dentry,
        unsigned int flags);
#endif

struct rfs_inode *rfs_inode_get(struct rfs_inode *rinode);
void rfs_inode_put(struct rfs_inode *rinode);
struct rfs_inode *rfs_inode_add(struct inode *inode, struct rfs_info *rinfo);
void rfs_inode_del(struct rfs_inode *rinode);
void rfs_inode_add_rdentry(struct rfs_inode *rinode,
        struct rfs_dentry *rdentry);
void rfs_inode_rem_rdentry(struct rfs_inode *rinode,
        struct rfs_dentry *rdentry);
struct rfs_info *rfs_inode_get_rinfo(struct rfs_inode *rinode);
int rfs_inode_set_rinfo(struct rfs_inode *rinode);
void rfs_inode_set_ops(struct rfs_inode *rinode);
int rfs_inode_cache_create(void);
void rfs_inode_cache_destroy(void);

struct rfs_file {

#ifdef RFS_DBG
    #define RFS_FILE_SIGNATURE  0xABCD0001
    uint32_t  signature;
#endif

    struct rfs_object robject;
    struct list_head rdentry_list;
    struct list_head data;
    struct file *file;
    struct rfs_dentry *rdentry;
#ifndef RFS_PER_OBJECT_OPS 
    struct rfs_hoperations* f_rhops;
    /* a mask of hooked operations for a file */
    unsigned long   f_op_bitfield[BIT_WORD(RFS_OP_f_end-RFS_OP_f_start) + 1];
#endif /* ! RFS_PER_OBJECT_OPS */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17))
    const struct file_operations *op_old;
#else
    struct file_operations *op_old;
#endif

#ifdef RFS_PER_OBJECT_OPS 
    struct file_operations op_new;
#endif /* RFS_PER_OBJECT_OPS */
    spinlock_t lock;
};

struct rfs_file* rfs_file_find(struct file *file);
/*
 * NFS can open regular file without a file operation(fop) open and the fop's
 * are inherited from inode(i_fop). For that reason we hooks all fop of a inode
 * regular file. When any hook is evaluated at first time its handled as
 * open for filters and then as original operation.
 * Issue is replicable on old kernel 2.6.32.xyz
 */
struct rfs_file* rfs_file_find_with_open_flts(struct file *file);
     
extern struct file_operations rfs_file_ops;
extern struct file_operations rfs_reg_file_ops;

int rfs_open(struct inode *inode, struct file *file);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
struct dentry *rfs_lookup(struct inode *dir, struct dentry *dentry,
        struct nameidata *nd);
#endif
struct rfs_file *rfs_file_get(struct rfs_file *rfile);
void rfs_file_put(struct rfs_file *rfile);
void rfs_file_set_ops(struct rfs_file *rfile);
int rfs_file_cache_create(void);
void rfs_file_cache_destory(void);
struct rfs_file *rfs_file_add(struct file *file);

void rfs_add_dir_subs(
    struct rfs_file *rfile,
    struct dentry *last);

struct rfs_dcache_data {
    struct rfs_info *rinfo;
    struct rfs_flt *rflt;
    struct dentry *droot;
};

struct rfs_dcache_data *rfs_dcache_data_alloc(struct dentry *dentry,
        struct rfs_info *rinfo, struct rfs_flt *rflt);
void rfs_dcache_data_free(struct rfs_dcache_data *rdata);

struct rfs_dcache_entry {
    struct list_head list;
    struct dentry *dentry;
};

int rfs_dcache_walk(struct dentry *root, int (*cb)(struct dentry *, void *),
        void *data);
int rfs_dcache_add_dir(struct dentry *dentry, void *data);
int rfs_dcache_add(struct dentry *dentry, void *data);
int rfs_dcache_rem(struct dentry *dentry, void *data);
int rfs_dcache_set(struct dentry *dentry, void *data);
int rfs_dcache_reset(struct dentry *dentry, void *data);
int rfs_dcache_rdentry_add(struct dentry *dentry, struct rfs_info *rinfo);
int rfs_dcache_rinode_del(struct rfs_dentry *rdentry, struct inode *inode);

int rfs_dcache_get_subs(
    struct dentry *dir,
    struct list_head *sibs,
    struct dentry* last);

void rfs_dcache_entry_free_list(struct list_head *head);

struct dentry*
rfs_get_first_cached_dir_entry(
    struct dentry *dentry);

struct rfs_context {
    struct list_head data;
    int idx;
    int idx_start;
};

void rfs_context_init(struct rfs_context *rcont, int start);
void rfs_context_deinit(struct rfs_context *rcont);

int rfs_precall_flts(struct rfs_chain *rchain, struct rfs_context *rcont,
        struct redirfs_args *rargs);
void rfs_postcall_flts(struct rfs_chain *rchain, struct rfs_context *rcont,
        struct redirfs_args *rargs);

enum rfs_inode_type rfs_imode_to_type(umode_t i_mode, bool is_dentry);
enum redirfs_op_idc rfs_inode_to_idc(struct inode* inode, enum rfs_op_id id);

#define rfs_kobj_to_rflt(__kobj) container_of(__kobj, struct rfs_flt, kobj)
int rfs_flt_sysfs_init(struct rfs_flt *rflt);
void rfs_flt_sysfs_exit(struct rfs_flt *rflt);
void rfs_kobject_init(struct kobject *kobj);

int rfs_sysfs_create(void);

void rfs_data_remove(struct list_head *head);



#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,16))

    #define rfs_rename_lock(sb) down(&sb->s_vfs_rename_sem)
    #define rfs_rename_unlock(sb) up(&sb->s_vfs_rename_sem)

    #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14))
    typedef unsigned gfp_t;

    static inline void *kzalloc(size_t size, gfp_t flags)
    {
        void *p;
        
        p = kmalloc(size, flags);
        if (!p)
            return NULL;

        memset(p, 0, size);

        return p;
    }
    #endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14) */

    static inline void *kmem_cache_zalloc(kmem_cache_t *cache, gfp_t flags)
    {
        void *obj;

        obj = kmem_cache_alloc(cache, flags);
        if (!obj)
            return NULL;

        memset(obj, 0, kmem_cache_size(cache));

        return obj;
    }       

#else /* LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,16)) */

    #define rfs_rename_lock(sb) mutex_lock(&sb->s_vfs_rename_mutex)
    #define rfs_rename_unlock(sb) mutex_unlock(&sb->s_vfs_rename_mutex)

#endif /* ! LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,16)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))

    static inline rfs_kmem_cache_t *rfs_kmem_cache_create(const char *n, size_t s)
    {
        return kmem_cache_create(n, s, 0, SLAB_RECLAIM_ACCOUNT, NULL);
    }

#else

    static inline rfs_kmem_cache_t *rfs_kmem_cache_create(const char *n, size_t s)
    {
        return kmem_cache_create(n, s, 0, SLAB_RECLAIM_ACCOUNT, NULL, NULL);
    }

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))

    static inline void rfs_nameidata_put(struct nameidata *nd)
    {
        path_release(nd);
    }

    static inline struct dentry *rfs_nameidata_dentry(struct nameidata *nd)
    {
        return nd->dentry;
    }

    static inline struct vfsmount *rfs_nameidata_mnt(struct nameidata *nd)
    {
        return nd->mnt;
    }

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))


    static inline void rfs_nameidata_put(struct nameidata *nd)
    {
        path_put(&nd->path);
    }

    static inline struct dentry *rfs_nameidata_dentry(struct nameidata *nd)
    {
        return nd->path.dentry;
    }

    static inline struct vfsmount *rfs_nameidata_mnt(struct nameidata *nd)
    {
        return nd->path.mnt;
    }

#else

/*
* Nothing to declare for the latest kernels as struct path is used
*/

#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
    #define rfs_dq_transfer vfs_dq_transfer
#else
    #define rfs_dq_transfer DQUOT_TRANSFER
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))

    static inline int rfs_follow_up(struct vfsmount **mnt, struct dentry **dentry)
    {
        struct path path;
        int rv;

        path.mnt = *mnt;
        path.dentry = *dentry;

        rv = follow_up(&path);

        *mnt = path.mnt;
        *dentry = path.dentry;

        return rv;
    }

#else

    static inline int rfs_follow_up(struct vfsmount **mnt, struct dentry **dentry)
    {
        return follow_up(mnt, dentry);
    }

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
    static inline void rfs_dcache_lock(struct dentry *d)
    {
        spin_lock(&dcache_lock);
    }

    static inline void rfs_dcache_unlock(struct dentry *d)
    {
        spin_unlock(&dcache_lock);
    }

    static inline void rfs_dcache_lock_nested(struct dentry *d)
    {
    }

    static inline void rfs_dcache_unlock_nested(struct dentry *d)
    {
    }

    static inline struct dentry *rfs_dget_locked(struct dentry *d)
    {
        return dget_locked(d);
    }

#else

    static inline void rfs_dcache_lock(struct dentry *d)
    {
        spin_lock(&d->d_lock);
    }

    static inline void rfs_dcache_lock_nested(struct dentry *d)
    {
        spin_lock_nested(&d->d_lock, DENTRY_D_LOCK_NESTED);
	}

    static inline void rfs_dcache_unlock_nested(struct dentry *d)
    {
        spin_unlock(&d->d_lock);
    }

    static inline void rfs_dcache_unlock(struct dentry *d)
    {
        spin_unlock(&d->d_lock);
    }

    static inline struct dentry *rfs_dget_locked(struct dentry *d)
    {
        return dget_dlock(d);
    }

#endif


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))

    static inline int rfs_path_lookup(const char *name, struct nameidata *nd)
    {
        return path_lookup(name, LOOKUP_FOLLOW, nd);
    }

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))

    static inline int rfs_path_lookup(const char *name, struct nameidata *nd)
    {
        struct path path;
        int rv;

        rv = kern_path(name, LOOKUP_FOLLOW, &path);
        if (rv)
            return rv;

        nd->path = path;
        return 0;
    }

#else

    static inline int rfs_path_lookup(const char *name, struct path *path)
    {
        return kern_path(name, LOOKUP_FOLLOW, path);
    }

#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))

    static inline int rfs_inode_setattr(struct inode *inode, const struct iattr *attr)
    {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
        return inode_setattr(inode, (struct iattr *) attr);
#else
        return inode_setattr(inode, attr);
#endif
    }

#else

    static inline int rfs_inode_setattr(struct inode *inode, const struct iattr *attr)
    {
        setattr_copy(inode, attr);
        mark_inode_dirty(inode);
        return 0;
    }

#endif

#endif /* _RFS_H */

