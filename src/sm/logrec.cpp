/*
 * (c) Copyright 2011-2014, Hewlett-Packard Development Company, LP
 */

#include "w_defines.h"

/*  -- do not edit anything above this line --   </std-header>*/

#define SM_SOURCE
#define LOGREC_C

#include "sm_base.h"
#include "logdef_gen.cpp"
#include "vec_t.h"
#include "alloc_cache.h"
#include "allocator.h"
#include "vol.h"
#include "restore.h"
#include <sstream>


#include <iomanip>
typedef        ios::fmtflags        ios_fmtflags;

#include <new>

DEFINE_SM_ALLOC(logrec_t);

/*********************************************************************
 *
 *  logrec_t::cat_str()
 *
 *  Return a string describing the category of the log record.
 *
 *********************************************************************/
const char*
logrec_t::cat_str() const
{
    switch (cat())  {
    case t_logical:
        return "l---";

    case t_logical | t_cpsn:
        return "l--c";

    case t_status:
        return "s---";

    case t_undo:
        return "--u-";

    case t_redo:
        return "-r--";

    case t_redo | t_cpsn:
        return "-r-c";

    case t_undo | t_redo:
        return "-ru-";

    case t_undo | t_redo | t_logical:
        return "lru-";

    case t_redo | t_logical | t_cpsn:
        return "lr_c";

    case t_redo | t_logical : // used in I/O layer
        return "lr__";

    case t_undo | t_logical | t_cpsn:
        return "l_uc";

    case t_undo | t_logical :
        return "l-u-";

    case t_redo | t_single_sys_xct:
        return "ssx-";
    case t_multi | t_redo | t_single_sys_xct:
        return "ssxm";

#if W_DEBUG_LEVEL > 0
    case t_bad_cat:
        // for debugging only
        return "BAD-";
#endif
    default:
      return 0;
    }
}

/*********************************************************************
 *
 *  logrec_t::type_str()
 *
 *  Return a string describing the type of the log record.
 *
 *********************************************************************/
inline const char*
logrec_t::type_str() const
{
    return get_type_str(type());
}

const char*
logrec_t::get_type_str(kind_t type)
{
    switch (type)  {
#        include "logstr_gen.cpp"
    default:
      return 0;
    }

    /*
     *  Not reached.
     */
    W_FATAL(eINTERNAL);
    return 0;
}




/*********************************************************************
 *
 *  logrec_t::fill(pid, len)
 *
 *  Fill the "pid" and "length" field of the log record.
 *
 *********************************************************************/
void
logrec_t::fill(const lpid_t* p, snum_t store, uint16_t tag, smsize_t l)
{
    w_assert9(w_base_t::is_aligned(_data));

    /* adjust _cat */
    xct_t *x = xct();
    if(smlevel_0::in_recovery_undo() ||
        (x && ( x->rolling_back() ||
			   x->state() == smlevel_0::xct_aborting))
	)
    {
        header._cat |= t_rollback;
    }
    set_pid(lpid_t::null);
    if (!is_single_sys_xct()) { // prv does not exist in single-log system transaction
        set_xid_prev(lsn_t::null);
    }
    header._page_tag = tag;
    if (p) set_pid(*p);
    header._snum = store;
    char *dat = is_single_sys_xct() ? data_ssx() : data();
    if (l != align(l)) {
        // zero out extra space to keep purify happy
        memset(dat+l, 0, align(l)-l);
    }
    unsigned int tmp = align(l) + (is_single_sys_xct() ? hdr_single_sys_xct_sz : hdr_non_ssx_sz) + sizeof(lsn_t);
    tmp = (tmp + 7) & unsigned(-8); // force 8-byte alignment
    w_assert1(tmp <= sizeof(*this));
    header._len = tmp;
    if(type() != t_skip) {
        DBG( << "Creat log rec: " << *this
                << " size: " << header._len << " xid_prevlsn: " << (is_single_sys_xct() ? lsn_t::null : xid_prev()) );
    }
}



/*********************************************************************
 *
 *  logrec_t::fill_xct_attr(tid, xid_prev_lsn)
 *
 *  Fill the transaction related fields of the log record.
 *
 *********************************************************************/
void
logrec_t::fill_xct_attr(const tid_t& tid, const lsn_t& last)
{
    w_assert0(!is_single_sys_xct()); // prv/xid doesn't exist in single-log system transaction!
    xidInfo._xid = tid;
    if(xid_prev().valid()) {
        w_assert2(is_cpsn());
    } else {
        set_xid_prev (last);
    }
}

/*
 * Determine whether the log record header looks valid
 */
bool
logrec_t::valid_header(const lsn_t & lsn) const
{
    if (header._len < (is_single_sys_xct() ? hdr_single_sys_xct_sz : hdr_non_ssx_sz)
        || header._type > 100 || cat() == t_bad_cat ||
        (lsn != lsn_t::null && lsn != *_lsn_ck())) {
        return false;
    }
    return true;
}


/*********************************************************************
 *
 *  logrec_t::redo(page)
 *
 *  Invoke the redo method of the log record.
 *
 *********************************************************************/
void logrec_t::redo(fixable_page_h* page)
{
    FUNC(logrec_t::redo);
    DBG( << "Redo  log rec: " << *this
        << " size: " << header._len << " xid_prevlsn: " << (is_single_sys_xct() ? lsn_t::null : xid_prev()) );

    // Could be either user transaction or compensation operatio,
    // not system transaction because currently all system transactions
    // are single log

    // This is used by both Single-Page-Recovery and serial recovery REDO phase

    // Not all REDO operations have associated page
    // If there is a page, mark the page for recovery access
    // this is for page access validation purpose to allow recovery
    // operation to by-pass the page concurrent access check

    if(page)
        page->set_recovery_access();

    switch (header._type)  {
#include "redo_gen.cpp"
    }

    // If we have a page, clear the recovery flag on the page after
    // we are done with undo operation
    if(page)
        page->clear_recovery_access();

    /*
     *  Page is dirty after redo.
     *  (not all redone log records have a page)
     *  NB: the page lsn in set by the caller (in restart.cpp)
     *  This is ok in recovery because in this phase, there
     *  is not a bf_cleaner thread running. (that thread asserts
     *  that if the page is dirty, its lsn is non-null, and we
     *  have a short-lived violation of that right here).
     */
    if(page) page->set_dirty();
}

static __thread logrec_t::kind_t undoing_context = logrec_t::t_max_logrec; // for accounting TODO REMOVE


/*********************************************************************
 *
 *  logrec_t::undo(page)
 *
 *  Invoke the undo method of the log record. Automatically tag
 *  a compensation lsn to the last log record generated for the
 *  undo operation.
 *
 *********************************************************************/
void
logrec_t::undo(fixable_page_h* page)
{
    w_assert0(!is_single_sys_xct()); // UNDO shouldn't be called for single-log sys xct
    undoing_context = logrec_t::kind_t(header._type);
    FUNC(logrec_t::undo);
    DBG( << "Undo  log rec: " << *this
        << " size: " << header._len  << " xid_prevlsn: " << xid_prev());

    // Only system transactions involve multiple pages, while there
    // is no UNDO for system transactions, so we only need to mark
    // recovery flag for the current UNDO page

    // If there is a page, mark the page for recovery, this is for page access
    // validation purpose to allow recovery operation to by-pass the
    // page concurrent access check
    // In most cases we do not have a page from caller, therefore
    // we need to go to individual undo function to mark the recovery flag.
    // All the page related operations are in Btree_logrec.cpp, including
    // operations for system and user transactions, note that operations
    // for system transaction have REDO but no UNDO
    // The actual UNDO implementation in Btree_impl.cpp
    if(page)
        page->set_recovery_access();

    switch (header._type) {
#include "undo_gen.cpp"
    }

    xct()->compensate_undo(xid_prev());

    // If we have a page, clear the recovery flag on the page after
    // we are done with undo operation
    if(page)
        page->clear_recovery_access();

    undoing_context = logrec_t::t_max_logrec;
}

/*********************************************************************
 *
 *  logrec_t::corrupt()
 *
 *  Zero out most of log record to make it look corrupt.
 *  This is for recovery testing.
 *
 *********************************************************************/
void
logrec_t::corrupt()
{
    char* end_of_corruption = ((char*)this)+length();
    char* start_of_corruption = (char*)&header._type;
    size_t bytes_to_corrupt = end_of_corruption - start_of_corruption;
    memset(start_of_corruption, 0, bytes_to_corrupt);
}

/*********************************************************************
 *
 *  xct_freeing_space
 *
 *  Status Log to mark the end of transaction and the beginning
 *  of space recovery.
 *  Synchronous for commit. Async for abort.
 *
 *********************************************************************/
xct_freeing_space_log::xct_freeing_space_log()
{
    fill(0, 0);
}


/*********************************************************************
 *
 *  xct_end_group_log
 *
 *  Status Log to mark the end of transaction and space recovery
 *  for a group of transactions.
 *
 *********************************************************************/
xct_list_t::xct_list_t(
    const xct_t*                        xct[],
    int                                 cnt)
    : count(cnt)
{
    w_assert1(count <= max);
    for (uint i = 0; i < count; i++)  {
        xrec[i].tid = xct[i]->tid();
    }
}

xct_end_group_log::xct_end_group_log(const xct_t *list[], int listlen)
{
    fill(0, (new (_data) xct_list_t(list, listlen))->size());
}
/*********************************************************************
 *
 *  xct_end_log
 *  xct_abort_log
 *
 *  Status Log to mark the end of transaction and space recovery.
 *
 *********************************************************************/
xct_end_log::xct_end_log()
{
    fill(0, 0);
}

// We use a different log record type here only for debugging purposes
xct_abort_log::xct_abort_log()
{
    fill(0, 0);
}


/*********************************************************************
 *
 *  comment_log
 *
 *  For debugging
 *
 *********************************************************************/
comment_log::comment_log(const char *msg)
{
    w_assert1(strlen(msg) < sizeof(_data));
    memcpy(_data, msg, strlen(msg)+1);
    DBG(<<"comment_log: L: " << (const char *)_data);
    fill(0, strlen(msg)+1);
}

void
comment_log::redo(fixable_page_h *page)
{
    w_assert9(page == 0);
    DBG(<<"comment_log: R: " << (const char *)_data);
    ; // just for the purpose of setting breakpoints
}

void
comment_log::undo(fixable_page_h *page)
{
    w_assert9(page == 0);
    DBG(<<"comment_log: U: " << (const char *)_data);
    ; // just for the purpose of setting breakpoints
}

/*********************************************************************
 *
 *  compensate_log
 *
 *  Needed when compensation rec is written rather than piggybacked
 *  on another record
 *
 *********************************************************************/
compensate_log::compensate_log(const lsn_t& rec_lsn)
{
    fill(0, 0);
    set_clr(rec_lsn);
}


/*********************************************************************
 *
 *  skip_log partition
 *
 *  Filler log record -- for skipping to end of log partition
 *
 *********************************************************************/
skip_log::skip_log()
{
    fill(0, 0);
}

/*********************************************************************
 *
 *  chkpt_begin_log
 *
 *  Status Log to mark start of fussy checkpoint.
 *
 *********************************************************************/
chkpt_begin_log::chkpt_begin_log(const lsn_t &lastMountLSN)
{
    new (_data) lsn_t(lastMountLSN);
    fill(0, sizeof(lsn_t));
}



/*********************************************************************
 *
 *  chkpt_end_log(const lsn_t &master, const lsn_t& min_rec_lsn, const lsn_t& min_txn_lsn)
 *
 *  Status Log to mark completion of fussy checkpoint.
 *  Master is the lsn of the record that began this chkpt.
 *  min_rec_lsn is the earliest lsn for all dirty pages in this chkpt.
 *  min_txn_lsn is the earliest lsn for all txn in this chkpt.
 *
 *********************************************************************/
chkpt_end_log::chkpt_end_log(const lsn_t& lsn, const lsn_t& min_rec_lsn,
                                const lsn_t& min_txn_lsn)
{
    // initialize _data
    lsn_t *l = new (_data) lsn_t(lsn);
    l++; //grot
    *l = min_rec_lsn;
    l++; //grot
    *l = min_txn_lsn;

    fill(0, (3 * sizeof(lsn_t)) + (3 * sizeof(int)));
}



/*********************************************************************
 *
 *  chkpt_bf_tab_log
 *
 *  Data Log to save dirty page table at checkpoint.
 *  Contains, for each dirty page, its pid, minimum recovery lsn and page (latest) lsn.
 *
 *********************************************************************/

chkpt_bf_tab_t::chkpt_bf_tab_t(
    int                 cnt,        // I-  # elements in pids[] and rlsns[]
    const lpid_t*         pids,        // I-  id of of dirty pages
    const snum_t*        stores,       // I-  store number of dirty pages
    const lsn_t*         rlsns,        // I-  rlsns[i] is recovery lsn of pids[i], the oldest
    const lsn_t*         plsns)        // I-  plsns[i] is page lsn lsn of pids[i], the latest
    : count(cnt)
{
    w_assert1( sizeof(*this) <= logrec_t::max_data_sz );
    w_assert1(count <= max);
    for (uint i = 0; i < count; i++) {
        brec[i].pid = pids[i];
        brec[i].store = stores[i];
        brec[i].rec_lsn = rlsns[i];
        brec[i].page_lsn = plsns[i];
    }
}


chkpt_bf_tab_log::chkpt_bf_tab_log(
    int                 cnt,        // I-  # elements in pids[] and rlsns[]
    const lpid_t*         pid,        // I-  id of of dirty pages
    const snum_t*        store,   // I- store number of dirty pages
    const lsn_t*         rec_lsn,// I-  rec_lsn[i] is recovery lsn (oldest) of pids[i]
    const lsn_t*         page_lsn)// I-  page_lsn[i] is page lsn (latest) of pids[i]
{
    fill(0, (new (_data) chkpt_bf_tab_t(cnt, pid, store, rec_lsn, page_lsn))->size());
}




/*********************************************************************
 *
 *  chkpt_xct_tab_log
 *
 *  Data log to save transaction table at checkpoint.
 *  Contains, for each active xct, its id, state, last_lsn
 *  and undo_nxt lsn.
 *
 *********************************************************************/
chkpt_xct_tab_t::chkpt_xct_tab_t(
    const tid_t&                         _youngest,
    int                                 cnt,
    const tid_t*                         tid,
    const smlevel_0::xct_state_t*         state,
    const lsn_t*                         last_lsn,
    const lsn_t*                         undo_nxt,
    const lsn_t*                         first_lsn)
    : youngest(_youngest), count(cnt)
{
    w_assert1(count <= max);
    for (uint i = 0; i < count; i++)  {
        xrec[i].tid = tid[i];
        xrec[i].state = state[i];
        xrec[i].last_lsn = last_lsn[i];
        xrec[i].undo_nxt = undo_nxt[i];
        xrec[i].first_lsn = first_lsn[i];
    }
}

chkpt_xct_tab_log::chkpt_xct_tab_log(
    const tid_t&                         youngest,
    int                                 cnt,
    const tid_t*                         tid,
    const smlevel_0::xct_state_t*         state,
    const lsn_t*                         last_lsn,
    const lsn_t*                         undo_nxt,
    const lsn_t*                         first_lsn)
{
    fill(0, (new (_data) chkpt_xct_tab_t(youngest, cnt, tid, state,
                                         last_lsn, undo_nxt, first_lsn))->size());
}


/*********************************************************************
 *
 *  chkpt_xct_lock_log
 *
 *  Data log to save acquired transaction locks for an active transaction at checkpoint.
 *  Contains, each active lock, its hash and lock mode
 *
 *********************************************************************/
chkpt_xct_lock_t::chkpt_xct_lock_t(
    const tid_t&                        _tid,
    int                                 cnt,
    const okvl_mode*                    lock_mode,
    const uint32_t*                     lock_hash)
    : tid(_tid), count(cnt)
{
    w_assert1(count <= max);
    for (uint i = 0; i < count; i++)  {
        xrec[i].lock_mode = lock_mode[i];
        xrec[i].lock_hash = lock_hash[i];
    }
}

chkpt_xct_lock_log::chkpt_xct_lock_log(
    const tid_t&                        tid,
    int                                 cnt,
    const okvl_mode*                    lock_mode,
    const uint32_t*                     lock_hash)
{
    fill(0, (new (_data) chkpt_xct_lock_t(tid, cnt, lock_mode,
                                         lock_hash))->size());
}



/*********************************************************************
 *
 *  chkpt_dev_tab_log
 *
 *  Data log to save devices mounted at checkpoint.
 *  Contains, for each device mounted, its devname and vid.
 *
 *********************************************************************/
chkpt_dev_tab_t::chkpt_dev_tab_t(vid_t next_vid,
        const std::vector<string>& devnames)
    : count(devnames.size()), next_vid(next_vid)
{
    std::stringstream ss;
    for (uint i = 0; i < count; i++) {
        ss << devnames[i];
    }
    data_size = ss.tellp();
    w_assert0(data_size <= logrec_t::max_data_sz);
    ss.read(data, data_size);
}

void chkpt_dev_tab_t::read_devnames(std::vector<string>& devnames)
{
    std::string s;
    std::stringstream ss;
    ss.write(data, data_size);

    for (uint i = 0; i < count; i++) {
        ss >> s;
        devnames.push_back(s);
    }
}

chkpt_dev_tab_log::chkpt_dev_tab_log(vid_t next_vid,
        const std::vector<string>& devnames)
{
    fill(0, (new (_data) chkpt_dev_tab_t(next_vid, devnames))->size());
}

/**
 * Instead of processing chkpt_dev_tab by parsing all devices and mounting
 * them individually, simply make it a redoable log record and perform all
 * mounts in the redo method.
 */
void chkpt_dev_tab_log::redo(fixable_page_h*)
{
    chkpt_dev_tab_t* tab = (chkpt_dev_tab_t*) _data;
    std::vector<string> dnames;
    tab->read_devnames(dnames);
    w_assert0(tab->count == dnames.size());

    for (int i = 0; i < tab->count; i++) {
        smlevel_0::vol->sx_mount(dnames[i].c_str(), false /* log */);
    }
    smlevel_0::vol->set_next_vid(tab->next_vid);
}

chkpt_backup_tab_t::chkpt_backup_tab_t(
        const std::vector<vid_t>& vids,
        const std::vector<string>& paths)
    : count(vids.size())
{
    w_assert0(vids.size() == paths.size());
    std::stringstream ss;
    for (uint i = 0; i < count; i++) {
        ss << vids[i];
        ss << paths[i];
    }
    data_size = ss.tellp();
    w_assert0(data_size <= logrec_t::max_data_sz);
    ss.read(data, data_size);
}

void chkpt_backup_tab_t::read(
        std::vector<vid_t>& vids,
        std::vector<string>& paths)
{
    vid_t vid;
    std::string s;
    std::stringstream ss;
    ss.write(data, data_size);

    for (uint i = 0; i < count; i++) {
        ss >> vid;
        vids.push_back(vid);
        ss >> s;
        paths.push_back(s);
    }
}

chkpt_backup_tab_log::chkpt_backup_tab_log(
        const std::vector<vid_t>& vids,
        const std::vector<string>& paths)
{
    fill(0, (new (_data) chkpt_backup_tab_t(vids, paths))->size());
}

void chkpt_backup_tab_log::redo(fixable_page_h*)
{
    chkpt_backup_tab_t* tab = (chkpt_backup_tab_t*) _data;
    std::vector<vid_t> vids;
    std::vector<string> paths;
    tab->read(vids, paths);
    w_assert0(tab->count == vids.size());
    w_assert0(tab->count == paths.size());

    for (int i = 0; i < tab->count; i++) {
        smlevel_0::vol->sx_add_backup(vids[i], paths[i], false /* log */);
    }
}

chkpt_restore_tab_log::chkpt_restore_tab_log(vid_t vid)
{
    vol_t* vol = smlevel_0::vol->get(vid);
    chkpt_restore_tab_t* tab =
        new (_data) chkpt_restore_tab_t(vid);

    vol->chkpt_restore_progress(tab);
    fill(0, tab->length());
}

void chkpt_restore_tab_log::redo(fixable_page_h*)
{
    chkpt_restore_tab_t* tab = (chkpt_restore_tab_t*) _data;

    vol_t* vol = smlevel_0::vol->get(tab->vid);

    w_assert0(vol);
    if (!vol->is_failed()) {
        // Marking the device failed will kick-off the restore thread, initialize
        // its state, and restore the metadata (even if already done - idempotence)
        W_COERCE(vol->mark_failed(false /* evict */, true /* redo */));
    }

    for (size_t i = 0; i < tab->firstNotRestored; i++) {
        vol->redo_segment_restore(i);
    }

    RestoreBitmap bitmap(vol->num_pages());
    bitmap.deserialize(tab->bitmap, tab->firstNotRestored,
            tab->firstNotRestored + tab->bitmapSize);
    // Bitmap of RestoreMgr might have been initialized already
    for (size_t i = tab->firstNotRestored; i < bitmap.getSize(); i++) {
        if (bitmap.get(i)) {
            vol->redo_segment_restore(i);
        }
    }
}

format_vol_log::format_vol_log(const char* path, shpid_t num_pages, vid_t vid)
{
    memcpy(data_ssx(), &vid, sizeof(vid_t));
    memcpy(data_ssx() + sizeof(vid_t), &num_pages, sizeof(shpid_t));

    size_t length = strlen(path);
    w_assert0(length < smlevel_0::max_devname);
    memcpy(data_ssx() + sizeof(vid_t) + sizeof(shpid_t), path, length);
    fill(0, length + sizeof(vid_t) + sizeof(shpid_t));
}

void format_vol_log::redo(fixable_page_h*)
{
    vid_t expected_vid = *((vid_t*) data_ssx());
    shpid_t num_pages = *((shpid_t*) data_ssx() + sizeof(vid_t));
    const char* dev_name = (const char*) data_ssx() + sizeof(vid_t)
        + sizeof(shpid_t);

    vid_t created_vid;
    W_COERCE(smlevel_0::vol->sx_format(dev_name, num_pages, created_vid,
                false));
    w_assert0(expected_vid == created_vid);
}

mount_vol_log::mount_vol_log(const char* dev_name)
{
    size_t length = strlen(dev_name);
    w_assert0(length < smlevel_0::max_devname);
    memcpy(data_ssx(), dev_name, length);
    fill(0, length);
}

void mount_vol_log::redo(fixable_page_h*)
{
    const char* dev_name = (const char*) data_ssx();
    W_COERCE(smlevel_0::vol->sx_mount(dev_name, false));
}

dismount_vol_log::dismount_vol_log(const char* dev_name)
{
    size_t length = strlen(dev_name);
    w_assert0(length < smlevel_0::max_devname);
    memcpy(data_ssx(), dev_name, length);
    fill(0, length);
}

void dismount_vol_log::redo(fixable_page_h*)
{
    const char* dev_name = (const char*) data_ssx();
    W_COERCE(smlevel_0::vol->sx_dismount(dev_name, false));
}

add_backup_log::add_backup_log(vid_t vid, const char* dev_name)
{
    memcpy(data_ssx(), &vid, sizeof(vid_t));
    size_t length = strlen(dev_name);
    w_assert0(length < smlevel_0::max_devname);
    memcpy(data_ssx() + sizeof(vid_t), dev_name, length);
    fill(0, sizeof(vid_t) + length);
}

void add_backup_log::redo(fixable_page_h*)
{
    vid_t vid = *((vid_t*) data_ssx());
    const char* dev_name = (const char*) data_ssx() + sizeof(vid_t);
    W_COERCE(smlevel_0::vol->sx_add_backup(vid, dev_name, false));
}

restore_begin_log::restore_begin_log(vid_t vid)
{
    memcpy(_data, &vid, sizeof(vid_t));
    fill(0, sizeof(vid_t));
}

void restore_begin_log::redo(fixable_page_h*)
{
    vid_t vid = *((vid_t*) _data);
    vol_t* volume = smlevel_0::vol->get(vid);
    // volume must be mounted
    w_assert0(volume);

    // Marking the device failed will kick-off the restore thread, initialize
    // its state, and restore the metadata (even if already done - idempotence)
    W_COERCE(volume->mark_failed(false /* evict */, true /* redo */));
}

restore_end_log::restore_end_log(vid_t vid)
{
    memcpy(_data, &vid, sizeof(vid_t));
    fill(0, sizeof(vid_t));
}

void restore_end_log::redo(fixable_page_h*)
{
    vid_t vid = *((vid_t*) _data);
    vol_t* volume = smlevel_0::vol->get(vid);
    // volume must be mounted and failed
    w_assert0(volume && volume->is_failed());

    bool finished = volume->check_restore_finished(true /* redo */);
    w_assert0(finished);
}

restore_segment_log::restore_segment_log(vid_t vid, uint32_t segment)
{
    memcpy(_data, &vid, sizeof(vid_t));
    memcpy(_data + sizeof(vid_t), &segment, sizeof(uint32_t));
    fill(0, sizeof(vid_t) + sizeof(uint32_t));
}

void restore_segment_log::redo(fixable_page_h*)
{
    vid_t vid = *((vid_t*) _data);
    vol_t* volume = smlevel_0::vol->get(vid);
    // volume must be mounted and failed
    w_assert0(volume && volume->is_failed());

    uint32_t segment = *((uint32_t*) _data + sizeof(vid_t));

    volume->redo_segment_restore(segment);
}

alloc_a_page_log::alloc_a_page_log(vid_t vid, shpid_t pid)
{
    memcpy(data_ssx(), &vid, sizeof(vid_t));
    memcpy(data_ssx() + sizeof(vid_t), &pid, sizeof(shpid_t));
    fill(0, sizeof(vid_t) + sizeof(shpid_t));
}

void alloc_a_page_log::redo(fixable_page_h*)
{
    vid_t vid = *((vid_t*) data_ssx());
    shpid_t shpid = *((shpid_t*) data_ssx() + sizeof(vid_t));

    vol_t* volume = smlevel_0::vol->get(vid);
    w_assert0(volume);
    volume->alloc_a_page(shpid, true);
}

dealloc_a_page_log::dealloc_a_page_log(vid_t vid, shpid_t pid)
{
    memcpy(data_ssx(), &vid, sizeof(vid_t));
    memcpy(data_ssx() + sizeof(vid_t), &pid, sizeof(shpid_t));
    fill(0, sizeof(vid_t) + sizeof(shpid_t));
}

void dealloc_a_page_log::redo(fixable_page_h*)
{
    vid_t vid = *((vid_t*) data_ssx());
    shpid_t shpid = *((shpid_t*) data_ssx() + sizeof(vid_t));

    vol_t* volume = smlevel_0::vol->get(vid);
    w_assert0(volume);
    volume->alloc_a_page(shpid, true);
}

page_img_format_t::page_img_format_t (const btree_page_h& page)
{
    size_t unused_length;
    char* unused = page.page()->unused_part(unused_length);

    const char *pp_bin = (const char *) page._pp;
    beginning_bytes = unused - pp_bin;
    ending_bytes    = sizeof(btree_page) - (beginning_bytes + unused_length);

    ::memcpy (data, pp_bin, beginning_bytes);
    ::memcpy (data + beginning_bytes, unused + unused_length, ending_bytes);
    w_assert1(beginning_bytes >= btree_page::hdr_sz);
    w_assert1(beginning_bytes + ending_bytes <= sizeof(btree_page));
}

void page_img_format_t::apply(fixable_page_h* page)
{
    w_assert1(beginning_bytes >= btree_page::hdr_sz);
    w_assert1(beginning_bytes + ending_bytes <= sizeof(btree_page));
    char *pp_bin = (char *) page->get_generic_page();
    ::memcpy (pp_bin, data, beginning_bytes); // <<<>>>
    ::memcpy (pp_bin + sizeof(btree_page) - ending_bytes,
            data + beginning_bytes, ending_bytes);
}

page_img_format_log::page_img_format_log(const btree_page_h &page) {
    fill(page,
         (new (_data) page_img_format_t(page))->size());
}

void page_img_format_log::undo(fixable_page_h*) {
    // we don't have to do anything for UNDO
    // because this is a page creation!
}
void page_img_format_log::redo(fixable_page_h* page) {
    // REDO is simply applying the image
    page_img_format_t* dp = (page_img_format_t*) _data;
    dp->apply(page);
    page->set_dirty();
}



/*********************************************************************
 *
 *  operator<<(ostream, logrec)
 *
 *  Pretty print a log record to ostream.
 *
 *********************************************************************/
#include "logtype_gen.h"
ostream&
operator<<(ostream& o, const logrec_t& l)
{
    ios_fmtflags        f = o.flags();
    o.setf(ios::left, ios::left);

    o << "LSN=" << l.lsn_ck() << " ";
    const char *rb = l.is_rollback()? "U" : "F"; // rollback/undo or forward

    if (!l.is_single_sys_xct()) {
        o << "TID=" << l.tid() << ' ';
    } else {
        o << "TID=SSX" << ' ';
    }
    W_FORM(o)("%20s%5s:%1s", l.type_str(), l.cat_str(), rb );
    o << "  " << l.construct_pid();
    if (l.is_multi_page()) {
        o << " src-" << l.construct_pid2();
    }

    switch(l.type()) {
        case t_comment :
                {
                    o << (const char *)l._data;
                }
                break;

        case t_store_operation:
                {
                    store_operation_param& param = *(store_operation_param*)l._data;
                    o << ' ' << param;
                }
                break;

        default: /* nothing */
                break;
    }

    if (!l.is_single_sys_xct()) {
        if (l.is_cpsn())  o << " (UNDO-NXT=" << l.undo_nxt() << ')';
        else  o << " [UNDO-PRV=" << l.xid_prev() << "]";
    }

    o << " [page-prv " << l.page_prev_lsn();
    if (l.is_multi_page()) {
        o << " page2-prv " << l.page2_prev_lsn();
    }
    o << "]";

    o.flags(f);
    return o;
}

// nothing needed so far..
class page_set_to_be_deleted_t {
public:
    page_set_to_be_deleted_t(){}
    int size()  { return 0;}
};

page_set_to_be_deleted_log::page_set_to_be_deleted_log(const fixable_page_h& p)
{
    fill(p, (new (_data) page_set_to_be_deleted_t()) ->size());
}


void page_set_to_be_deleted_log::redo(fixable_page_h* page)
{
    rc_t rc = page->set_to_be_deleted(false); // no log
    if (rc.is_error()) {
        W_FATAL(rc.err_num());
    }
}

void page_set_to_be_deleted_log::undo(fixable_page_h* page)
{
    page->unset_to_be_deleted();
}

store_operation_log::store_operation_log(vid_t vid,
        const store_operation_param& param)
{
    new (_data) store_operation_param(param);
    lpid_t dummy(vid, 0);
    fill(&dummy, param.snum(), 0, param.size());
}

void store_operation_log::redo(fixable_page_h* /*page*/)
{
    store_operation_param& param = *(store_operation_param*)_data;
    DBG( << "store_operation_log::redo(page=" << pid()
        << ", param=" << param << ")" );
    W_COERCE( smlevel_0::vol->get(vid())->store_operation(param, true) );
}

void store_operation_log::undo(fixable_page_h* /*page*/)
{
    store_operation_param& param = *(store_operation_param*)_data;
    DBG( << "store_operation_log::undo(page=" << shpid() << ", param=" << param << ")" );

    switch (param.op())  {
        case smlevel_0::t_delete_store:
            /* do nothing, not undoable */
            break;
        case smlevel_0::t_create_store:
            // TODO implement destroy_store
            /*
            {
                stid_t stid(vid(), param.snum());
                W_COERCE( smlevel_0::io->destroy_store(stid) );
            }
            */
            break;
        case smlevel_0::t_set_deleting:
            switch (param.new_deleting_value())  {
                case smlevel_0::t_not_deleting_store:
                case smlevel_0::t_deleting_store:
                    {
                        store_operation_param new_param(param.snum(),
                                smlevel_0::t_set_deleting,
                                param.old_deleting_value(),
                                param.new_deleting_value());
                        W_COERCE( smlevel_0::vol->get(vid())->store_operation(
                                new_param) );
                    }
                    break;
                case smlevel_0::t_unknown_deleting:
                    W_FATAL(eINTERNAL);
                    break;
            }
            break;
        case smlevel_0::t_set_store_flags:
            {
                store_operation_param new_param(param.snum(),
                        smlevel_0::t_set_store_flags,
                        param.old_store_flags(), param.new_store_flags());
                W_COERCE( smlevel_0::vol->get(vid())->store_operation(
                        new_param) );
            }
            break;
        case smlevel_0::t_set_root:
            /* do nothing, not undoable */
            break;
    }
}

#if LOGREC_ACCOUNTING

class logrec_accounting_impl_t {
private:
    static __thread uint64_t bytes_written_fwd [t_max_logrec];
    static __thread uint64_t bytes_written_bwd [t_max_logrec];
    static __thread uint64_t bytes_written_bwd_cxt [t_max_logrec];
    static __thread uint64_t insertions_fwd [t_max_logrec];
    static __thread uint64_t insertions_bwd [t_max_logrec];
    static __thread uint64_t insertions_bwd_cxt [t_max_logrec];
    static __thread double            ratio_bf       [t_max_logrec];
    static __thread double            ratio_bf_cxt   [t_max_logrec];

    static const char *type_str(int _type);
    static void reinit();
public:
    logrec_accounting_impl_t() {  reinit(); }
    ~logrec_accounting_impl_t() {}
    static void account(logrec_t &l, bool fwd);
    static void account_end(bool fwd);
    static void print_account_and_clear();
};
static logrec_accounting_impl_t dummy;
void logrec_accounting_impl_t::reinit()
{
    for(int i=0; i < t_max_logrec; i++) {
        bytes_written_fwd[i] =
        bytes_written_bwd[i] =
        bytes_written_bwd_cxt[i] =
        insertions_fwd[i] =
        insertions_bwd[i] =
        insertions_bwd_cxt[i] =  0;
        ratio_bf[i] = 0.0;
        ratio_bf_cxt[i] = 0.0;
    }
}
// this doesn't have to be thread-safe, as I'm using it only
// to figure out the ratios
void logrec_accounting_t::account(logrec_t &l, bool fwd)
{
    logrec_accounting_impl_t::account(l,fwd);
}
void logrec_accounting_t::account_end(bool fwd)
{
    logrec_accounting_impl_t::account_end(fwd);
}

void logrec_accounting_impl_t::account_end(bool fwd)
{
    // Set the context to end so we can account for all
    // overhead related to that.
    if(!fwd) {
        undoing_context = logrec_t::t_xct_end;
    }
}
void logrec_accounting_impl_t::account(logrec_t &l, bool fwd)
{
    unsigned b = l.length();
    int      t = l.type();
    int      tcxt = l.type();
    if(fwd) {
        w_assert0((undoing_context == logrec_t::t_max_logrec)
               || (undoing_context == logrec_t::t_xct_end));
    } else {
        if(undoing_context != logrec_t::t_max_logrec) {
            tcxt = undoing_context;
        } else {
            // else it's something like a compensate  or xct_end
            // and we'll chalk it up to t_xct_abort, which
            // is not undoable.
            tcxt = t_xct_abort;
        }
    }
    if(fwd) {
        bytes_written_fwd[t] += b;
        insertions_fwd[t] ++;
    }
    else {
        bytes_written_bwd[t] += b;
        bytes_written_bwd_cxt[tcxt] += b;
        insertions_bwd[t] ++;
        insertions_bwd_cxt[tcxt] ++;
    }
    if(bytes_written_fwd[t]) {
        ratio_bf[t] = double(bytes_written_bwd_cxt[t]) /
            double(bytes_written_fwd[t]);
    } else {
        ratio_bf[t] = 1;
    }
    if(bytes_written_fwd[tcxt]) {
        ratio_bf_cxt[tcxt] = double(bytes_written_bwd_cxt[tcxt]) /
            double(bytes_written_fwd[tcxt]);
    } else {
        ratio_bf_cxt[tcxt] = 1;
    }
}

const char *logrec_accounting_impl_t::type_str(int _type) {
    switch (_type)  {
#        include "logstr_gen.cpp"
    default:
      return 0;
    }
}

void logrec_accounting_t::print_account_and_clear()
{
    logrec_accounting_impl_t::print_account_and_clear();
}
void logrec_accounting_impl_t::print_account_and_clear()
{
    uint64_t anyb=0;
    for(int i=0; i < t_max_logrec; i++) {
        anyb += insertions_bwd[i];
    }
    if(!anyb) {
        reinit();
        return;
    }
    // don't bother unless there was an abort.
    // I mean something besides just compensation records
    // being chalked up to bytes backward or insertions backward.
    if( insertions_bwd[t_compensate] == anyb ) {
        reinit();
        return;
    }

    char out[200]; // 120 is adequate
    sprintf(out,
        "%s %20s  %8s %8s %8s %12s %12s %12s %10s %10s PAGESIZE %d\n",
        "LOGREC",
        "record",
        "ins fwd", "ins bwd", "rec undo",
        "bytes fwd", "bytes bwd",  "bytes undo",
        "B:F",
        "BUNDO:F",
        SM_PAGESIZE
        );
    fprintf(stdout, "%s", out);
    uint64_t btf=0, btb=0, btc=0;
    uint64_t itf=0, itb=0, itc=0;
    for(int i=0; i < t_max_logrec; i++) {
        btf += bytes_written_fwd[i];
        btb += bytes_written_bwd[i];
        btc += bytes_written_bwd_cxt[i];
        itf += insertions_fwd[i];
        itb += insertions_bwd[i];
        itc += insertions_bwd_cxt[i];

        if( insertions_fwd[i] + insertions_bwd[i] + insertions_bwd_cxt[i] > 0)
        {
            sprintf(out,
            "%s %20s  %8lu %8lu %8lu %12lu %12lu %12lu %10.7f %10.7f PAGESIZE %d \n",
            "LOGREC",
            type_str(i) ,
            insertions_fwd[i],
            insertions_bwd[i],
            insertions_bwd_cxt[i],
            bytes_written_fwd[i],
            bytes_written_bwd[i],
            bytes_written_bwd_cxt[i],
            ratio_bf[i],
            ratio_bf_cxt[i],
            SM_PAGESIZE
            );
            fprintf(stdout, "%s", out);
        }
    }
    sprintf(out,
    "%s %20s  %8lu %8lu %8lu %12lu %12lu %12lu %10.7f %10.7f PAGESIZE %d\n",
    "LOGREC",
    "TOTAL",
    itf, itb, itc,
    btf, btb, btc,
    double(btb)/double(btf),
    double(btc)/double(btf),
    SM_PAGESIZE
    );
    fprintf(stdout, "%s", out);
    reinit();
}

__thread uint64_t logrec_accounting_impl_t::bytes_written_fwd [t_max_logrec];
__thread uint64_t logrec_accounting_impl_t::bytes_written_bwd [t_max_logrec];
__thread uint64_t logrec_accounting_impl_t::bytes_written_bwd_cxt [t_max_logrec];
__thread uint64_t logrec_accounting_impl_t::insertions_fwd [t_max_logrec];
__thread uint64_t logrec_accounting_impl_t::insertions_bwd [t_max_logrec];
__thread uint64_t logrec_accounting_impl_t::insertions_bwd_cxt [t_max_logrec];
__thread double            logrec_accounting_impl_t::ratio_bf       [t_max_logrec];
__thread double            logrec_accounting_impl_t::ratio_bf_cxt   [t_max_logrec];

#endif
