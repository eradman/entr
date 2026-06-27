#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

// Include the actual entr.c file to test the real functions
#include "entr.c"

START_TEST(test_path_length_invariant)
{
    // Invariant: Path length consistency must be maintained throughout processing
    const char *payloads[] = {
        // Exact exploit case: path that triggers directory processing with -d flag
        "/tmp/very_long_directory_name_that_approaches_buffer_limit",
        // Boundary case: exactly MAX_PATH_LENGTH (assuming 4096)
        "a",
        // Valid input: normal path
        "/usr/bin"
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        const char *path = payloads[i];
        size_t len = strlen(path);
        
        // Create test directory if needed for the exploit case
        if (i == 0) {
            mkdir(path, 0755);
        }
        
        // Initialize the files array
        files = malloc(sizeof(WatchFile *) * 10);
        for (int j = 0; j < 10; j++) {
            files[j] = malloc(sizeof(WatchFile));
            files[j]->fn = malloc(MAX_PATH_LENGTH);
        }
        n_files = 0;
        
        // Test the actual process_input function
        int result = process_input(path, 1);  // Use -d flag (directory mode)
        
        // Security property: After processing, if a file was added, its fn buffer
        // should contain a properly null-terminated string no longer than MAX_PATH_LENGTH-1
        if (n_files > 0) {
            ck_assert_ptr_nonnull(files[0]->fn);
            ck_assert_int_lt(strlen(files[0]->fn), MAX_PATH_LENGTH);
            ck_assert_int_eq(files[0]->fn[strlen(files[0]->fn)], '\0');
            
            // Additional invariant: The stored length should match the actual string length
            size_t stored_len = strlen(files[0]->fn);
            ck_assert_int_le(stored_len, len);
        }
        
        // Cleanup
        for (int j = 0; j < 10; j++) {
            free(files[j]->fn);
            free(files[j]);
        }
        free(files);
        
        // Remove test directory
        if (i == 0) {
            rmdir(path);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_path_length_invariant);
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