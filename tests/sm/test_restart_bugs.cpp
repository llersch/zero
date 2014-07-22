#include "btree_test_env.h"
#include "gtest/gtest.h"
#include "sm_vas.h"
#include "btree.h"
#include "btcursor.h"
#include "bf.h"
#include "xct.h"

/* This class contains only test cases that are failing at the time.
 * The issues that cause the test cases to fail are tracked in the bug reporting system,
 * the associated issue ID is noted beside each test case. 
 * Since they would block the check-in process, all test cases are disabled. 
 */

btree_test_env *test_env;

lsn_t get_durable_lsn() {
    lsn_t ret;
    ss_m::get_durable_lsn(ret);
    return ret;
}
void output_durable_lsn(int W_IFDEBUG1(num)) {
    DBGOUT1( << num << ".durable LSN=" << get_durable_lsn());
}


/* Test case with an uncommitted transaction, no checkpoint
 * The crash shutdown scenario is currently failing because the current implementation for simulated crash shutdown
 * is unable to handle an in-flight transaction with multiple inserts.
 * A bug report concerning this issue has been submitted. (ZERO-182) 
 */
class restart_complic_inflight : public restart_test_base 
{
public:
    w_rc_t pre_shutdown(ss_m *ssm) {
        output_durable_lsn(1);
        W_DO(x_btree_create_index(ssm, &_volume, _stid, _root_pid));
        output_durable_lsn(2);
        W_DO(test_env->btree_insert_and_commit(_stid, "aa3", "data3"));
        W_DO(test_env->btree_insert_and_commit(_stid, "aa4", "data4"));
        W_DO(test_env->btree_insert_and_commit(_stid, "aa1", "data1"));

        // Start a transaction but no commit, normal shutdown
        W_DO(test_env->begin_xct());
        W_DO(test_env->btree_insert(_stid, "aa2", "data2"));
        W_DO(test_env->btree_insert(_stid, "aa5", "data5"));
        output_durable_lsn(3);
        return RCOK;
    }

    w_rc_t post_shutdown(ss_m *) {
        output_durable_lsn(4);
        x_btree_scan_result s;
        W_DO(test_env->btree_scan(_stid, s));
        EXPECT_EQ (3, s.rownum);
        EXPECT_EQ (std::string("aa1"), s.minkey);
        EXPECT_EQ (std::string("aa4"), s.maxkey);
        return RCOK;
    }
};

/* Passing */
TEST (RestartTestBugs, ComplicInflightN) {
    test_env->emtpy_logdata_dir();
    restart_complic_inflight context;
    EXPECT_EQ(test_env->runRestartTest(&context, false, 10), 0); // false = no simulated crash, normal shutdown
								 // 10 = recovery mode, m1 default serial mode
}
/**/

/* Disabled because it's failing
 * TEST (RestartTestBugs, ComplicInflightC) {
 *   test_env->empty_logdata_dir();
 *   restart_complic_inflight context;
 *   EXPECT_EQ(test_env->runRestartTest(&context, true, 10), 0);  // true = simulated crash
 *                                                                // 10 = recovery mode, m1 default serial mode
 * } 
 */


/* Test case with a committed insert, an aborted removal and an aborted update 
 * Currently, the crash shutdown scenario is failing because of a bug in the code, see issue ZERO-183 
 * There are two other test cases in test_restart - MultithrdInflightC and MultithrdAbortC - that fail for the same reason.
 * When the issue is resolved and this test case is transferred to test_restart, enable those as well.
 */
class restart_aborted_remove : public restart_test_base
{
public:
    w_rc_t pre_shutdown(ss_m *ssm) {
	output_durable_lsn(1);
	W_DO(x_btree_create_index(ssm, &_volume, _stid, _root_pid));
	output_durable_lsn(2);
	W_DO(test_env->btree_insert_and_commit(_stid, "aa0", "data0"));
	W_DO(test_env->begin_xct());
	W_DO(test_env->btree_remove(_stid, "aa0"));
	W_DO(test_env->abort_xct());
	output_durable_lsn(3);
	return RCOK;
    }

    w_rc_t post_shutdown(ss_m *) {
	output_durable_lsn(4);
	x_btree_scan_result s;
	W_DO(test_env->btree_scan(_stid, s));
	EXPECT_EQ(1, s.rownum);
	EXPECT_EQ(std::string("aa0"), s.maxkey);
	return RCOK;
    }
};

/* Passing */
TEST (RestartTestBugs, AbortedRemoveN) {
    test_env->empty_logdata_dir();
    restar_aborted_remove context;
    EXPECT_EQ(text_env->runRestartTest(&context, false, 10), 0);  // false = no simulated crash, normal shutdown
								  // 10 = recovery mode, m1 default serial mode
}
/**/

/* Disabled, because it's failing 
TEST (RestartTestBugs, AbortedRemoveFailingC) {
    test_env->empty_logdata_dir();
    restart_aborted_remove context;
    EXPECT_EQ(test_env->runRestartTest(&context, true, 10), 0);
    // true = simulated crash; 10 = recovery mode, m1 default serial mode
}
*/


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    test_env = new btree_test_env();
    ::testing::AddGlobalTestEnvironment(test_env);
    return RUN_ALL_TESTS();
}
