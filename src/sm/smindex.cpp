#include "w_defines.h"

#define SM_SOURCE
#define SMINDEX_C
#include "sm_int_4.h"
#include "sm_du_stats.h"
#include "sm.h"
#include "xct.h"
#include "btree.h"
#include "suppress_unused.h"

/*==============================================================*
 *  Physical ID version of all the index operations                *
 *==============================================================*/

rc_t ss_m::create_index(vid_t vid, stid_t &stid)
{
    W_DO(lm->intent_vol_lock(vid, IX)); // take IX on volume
    W_DO(io->create_store(vid, st_regular, stid) );
    W_DO(lm->intent_store_lock(stid, EX)); // take X on this new index

    lpid_t root;
    W_DO( bt->create(stid, root) );
    return RCOK;
}

/*********************************************************************
 *
 *  ss_m::create_index(vid, ntype, property, key_desc, stid)
 *  ss_m::create_index(vid, ntype, property, key_desc, cc, stid)
 *
 *********************************************************************/
rc_t
ss_m::create_index(
    vid_t                   vid, 
    ndx_t                   ntype, 
    store_property_t        property,
    const char*             key_desc,
    stid_t&                 stid
    )
{
    //TODO: SHORE-KITS-API: pass t_cc_kvl instead of t_cc_none
    assert(0);
    return 
    //create_index(vid, ntype, property, key_desc, t_cc_kvl, stid);
    create_index(vid, ntype, property, key_desc, t_cc_none, stid);
}

rc_t
ss_m::create_index(
    vid_t                 vid, 
    ndx_t                 ntype, 
    store_property_t      property,
    const char*           key_desc,
    concurrency_t         cc, 
    stid_t&               stid
    )
{
    SUPPRESS_UNUSED_5(vid, ntype, property, key_desc, cc);
    SUPPRESS_UNUSED(stid);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}


rc_t ss_m::destroy_index(const stid_t& stid)
{
    // take IX on volume, X on the index
    W_DO(lm->intent_vol_lock(stid.vol, IX));
    W_DO(lm->intent_store_lock(stid, EX));
    return RC(eNOTIMPLEMENTED);
    return RCOK;
}

rc_t ss_m::print_index(stid_t stid)
{
    lpid_t root_pid;
    W_DO(open_store_nolock (stid, root_pid)); // this method is for debugging
    bt->print(root_pid);
    return RCOK;
}

/*********************************************************************
 *  ss_m::create_mr_index(vid, ntype, property, key_desc, cc, stid)  *
 *********************************************************************/
rc_t ss_m::create_mr_index(vid_t                 vid, 
			   ndx_t                 ntype, 
			   store_property_t      property,
			   const char*           key_desc,
			   concurrency_t         cc, 
			   stid_t&               stid,
			   const bool            bIgnoreLatches
			   )
{
    SUPPRESS_UNUSED_5(vid, ntype, property, key_desc, cc);
    SUPPRESS_UNUSED_2(stid, bIgnoreLatches);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

/*****************************************************************************
 *  ss_m::create_mr_index(vid, ntype, property, key_desc, cc, stid, ranges)  *
 *****************************************************************************/
rc_t ss_m::create_mr_index(vid_t                 vid, 
			   ndx_t                 ntype, 
			   store_property_t      property,
			   const char*           key_desc,
			   concurrency_t         cc, 
			   stid_t&               stid,
			   key_ranges_map&        ranges,
			   const bool            bIgnoreLatches
			   )
{
    SUPPRESS_UNUSED_5(vid, ntype, property, key_desc, cc);
    SUPPRESS_UNUSED_3(stid, ranges, bIgnoreLatches);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

/*--------------------------------------------------------------*
 *  ss_m::destroy_mr_index()                                    *
 *--------------------------------------------------------------*/
rc_t ss_m::destroy_mr_index(const stid_t& iid)
{
    SUPPRESS_UNUSED(iid);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

/*--------------------------------------------------------------*
 *  ss_m::make_equal_partitions()                               *
 *--------------------------------------------------------------*/
rc_t ss_m::make_equal_partitions(stid_t stid, const vec_t& minKey,
				 const vec_t& maxKey, uint numParts)
{
    SUPPRESS_UNUSED_4(stid, minKey, maxKey, numParts);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}


rc_t ss_m::create_assoc(stid_t stid, const w_keystr_t& key, const vec_t& el)
{
    lpid_t root_pid;
    W_DO(open_store (stid, root_pid, true));
    W_DO( bt->insert(stid.vol.vol, stid.store, key, el) );
    return RCOK;
}

rc_t ss_m::create_assoc(stid_t stid, const vec_t& key, const vec_t& el)
{
    SUPPRESS_UNUSED_3(stid, key, el);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

rc_t ss_m::update_assoc(stid_t stid, const w_keystr_t& key, const vec_t& el)
{
    lpid_t root_pid;
    W_DO( open_store (stid, root_pid, true));
    W_DO( bt->update(stid.vol.vol, stid.store, key, el) );
    return RCOK;
}
rc_t ss_m::overwrite_assoc(stid_t stid, const w_keystr_t &key,
    const char *el, smsize_t offset, smsize_t elen)
{
    lpid_t root_pid;
    W_DO( open_store (stid, root_pid, true));
    W_DO( bt->overwrite(stid.vol.vol, stid.store, key, el, offset, elen) );
    return RCOK;
}

rc_t ss_m::destroy_assoc(stid_t stid, const w_keystr_t& key)
{
    lpid_t root_pid;
    W_DO(open_store (stid, root_pid, true));
    W_DO( bt->remove(stid.vol.vol, stid.store, key) );
    return RCOK;
}

rc_t ss_m::destroy_assoc(stid_t stid, const vec_t& key, const vec_t& el)
{
    SUPPRESS_UNUSED_3(stid, key, el);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

rc_t ss_m::find_assoc(stid_t stid, const w_keystr_t& key, 
                 void* el, smsize_t& elen, bool& found)
{
    lpid_t root_pid;
    bool for_update = g_xct_does_ex_lock_for_select();
    W_DO(open_store (stid, root_pid, for_update));
    W_DO( bt->lookup(stid.vol.vol, stid.store, key, el, elen, found) );    
    return RCOK;
}

rc_t ss_m::find_assoc(stid_t stid, const vec_t& key, 
                 void* el, smsize_t& elen, bool& found)
{
    SUPPRESS_UNUSED_5(stid, key, el, elen, found);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

rc_t ss_m::verify_index(stid_t stid, int hash_bits, bool &consistent)
{
    lpid_t root_pid;
    W_DO( open_store (stid, root_pid));
    W_DO( bt->verify_tree(stid.vol.vol, stid.store,  hash_bits, consistent) );
    return RCOK;
}

rc_t ss_m::defrag_index_page(btree_p &page)
{
    W_DO( bt->defrag_page(page));
    return RCOK;
}

rc_t ss_m::open_store (const stid_t &stid, lpid_t &root_pid, bool for_update)
{
    // take intent lock
    if (g_xct_does_need_lock()) {
        W_DO(lm->intent_vol_store_lock(stid, for_update ? IX : IS));
    }
    return open_store_nolock (stid, root_pid);
}
rc_t ss_m::open_store_nolock (const stid_t &stid, lpid_t &root_pid)
{
    root_pid = lpid_t (stid, io->get_root(stid));
    if (root_pid.page == 0) {
        return RC(eBADSTID);
    }
    return RCOK;
}

/*--------------------------------------------------------------*
 *  ss_m::_get_store_info()                                        *
 *--------------------------------------------------------------*/
rc_t
ss_m::_get_store_info(
    const stid_t&         stid, 
    sm_store_info_t&        info
)
{
    lpid_t root_pid;
    W_DO(open_store_nolock(stid, root_pid));
    info.store = stid.store;
    info.root   = root_pid.page;
    return RCOK;
}


/*--------------------------------------------------------------*
 *  ss_m::create_mr_assoc()                                     *
 *--------------------------------------------------------------*/
rc_t ss_m::create_mr_assoc(stid_t stid, const vec_t& key, el_filler& ef, 
			   const bool bIgnoreLocks, // = false
			   const bool bIgnoreLatches, // = false
			   RELOCATE_RECORD_CALLBACK_FUNC relocate_callback,  // = NULL 
			   const lpid_t& root) // lpid_t::null
{
    SUPPRESS_UNUSED_5(stid, key, ef, bIgnoreLocks, bIgnoreLatches);
    SUPPRESS_UNUSED_2(relocate_callback, root);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

/*--------------------------------------------------------------*
 *  ss_m::destroy_mr_assoc()                                    *
 *--------------------------------------------------------------*/
rc_t ss_m::destroy_mr_assoc(stid_t stid, const vec_t& key, const vec_t& el,
			    const bool bIgnoreLocks, const bool bIgnoreLatches,
			    const lpid_t& root)
{
    SUPPRESS_UNUSED_5(stid, key, el, bIgnoreLocks, bIgnoreLatches);
    SUPPRESS_UNUSED(root);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

/*--------------------------------------------------------------*
 *  ss_m::get_range_map()                                       *
 *--------------------------------------------------------------*/
rc_t ss_m::get_range_map(stid_t stid, key_ranges_map*& rangemap)
{
    SUPPRESS_UNUSED_2(stid, rangemap);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

/*--------------------------------------------------------------*
 *  ss_m::find_mr_assoc()                                       *
 *--------------------------------------------------------------*/
rc_t ss_m::find_mr_assoc(stid_t stid, const vec_t& key, 
			 void* el, smsize_t& elen, bool& found,
			 const bool bIgnoreLocks, const bool bIgnoreLatches,
			 const lpid_t& root)
{
    SUPPRESS_UNUSED_5(stid, key, el, elen, found);
    SUPPRESS_UNUSED_3(bIgnoreLocks, bIgnoreLatches, root);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}

/*--------------------------------------------------------------*
 *  ss_m::update_mr_assoc()                                     *
 *--------------------------------------------------------------*/
rc_t ss_m::update_mr_assoc(stid_t stid, const vec_t& key, 
			   const vec_t& old_el, const vec_t& new_el, bool& found,
			   const bool bIgnoreLocks, const bool bIgnoreLatches,
			   const lpid_t& root)
{
    SUPPRESS_UNUSED_5(stid, key, old_el, new_el, found);
    SUPPRESS_UNUSED_3(bIgnoreLocks, bIgnoreLatches, root);
    //TODO: SHORE-KITS-API
    assert(0);
    SUPPRESS_NON_RETURN(rc_t);
}
