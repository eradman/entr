#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Include the actual production header
#include "entr.h"

START_TEST(test_watchfile_memory_safety)
{
    // Invariant: WatchFile structures must remain valid after memory operations
    // and never cause use-after-free when accessed through the files array
    const char *payloads[] = {
        "test_file.txt",           // Valid input
        "",                        // Empty string boundary
        "very_long_filename_"      // Long filename that may trigger reallocation
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        // Create test file
        FILE *fp = fopen(payloads[i], "w");
        if (fp) {
            fclose(fp);
        }

        // Initialize watch context
        WatchContext ctx;
        memset(&ctx, 0, sizeof(ctx));
        
        // Add the file to watch list - this exercises the malloc in files[n_files]
        int result = watch_file_add(&ctx, payloads[i]);
        
        // Property: After adding, the WatchFile pointer should be valid
        if (result == 0 && ctx.n_files > 0) {
            // Access the WatchFile structure - should not crash or cause use-after-free
            ck_assert_ptr_nonnull(ctx.files[ctx.n_files - 1]);
            ck_assert_int_ge(ctx.files[ctx.n_files - 1]->fd, -1);
        }
        
        // Clean up - this exercises the free() path
        watch_cleanup(&ctx);
        
        // Property: After cleanup, accessing files array should be safe
        // (either NULL or properly handled)
        ck_assert_int_eq(ctx.n_files, 0);
        
        // Remove test file
        unlink(payloads[i]);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_watchfile_memory_safety);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}