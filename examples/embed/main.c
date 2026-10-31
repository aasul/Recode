#include <recode.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t increment[] = {0x48,0x89,0xf8,0x48,0x83,0xc0,1,0xc3};
    struct rc_runtime_config config = rc_runtime_default_config();
    config.policy = RC_POLICY_FIXED;
    config.hot_threshold = 2;
    struct rc_runtime *runtime = NULL;
    struct rc_error error = {0};
    rc_handle handle;
    int failed = 1;
    if (strcmp(RECODE_VERSION, rc_version_string()) != 0)
        return 1;
    if (rc_runtime_create(&config, &runtime, &error) != RC_OK)
        goto done;
    if (rc_runtime_register(runtime, increment, sizeof(increment), &handle, &error) != RC_OK)
        goto done;
    for (uint64_t i = 0; i < 100; ++i) {
        uint64_t result;
        if (rc_runtime_call(runtime, handle, i, &result, &error) != RC_OK)
            goto done;
        if (result != i + 1) {
            fprintf(stderr, "unexpected result\n");
            goto done;
        }
    }
    struct rc_runtime_stats stats;
    if (rc_runtime_get_stats(runtime, &stats, &error) != RC_OK)
        goto done;
    printf("Recode %s: %" PRIu64 " calls, %zu executable bytes\n",
           rc_version_string(), stats.invocations, stats.executable_bytes);
    if (rc_runtime_remove(runtime, handle, &error) != RC_OK)
        goto done;
    failed = 0;
done:
    if (error.status != RC_OK)
        fprintf(stderr, "%s at %zu (errno %d)\n", rc_status_string(error.status),
                error.offset, error.system_errno);
    if (runtime != NULL && rc_runtime_destroy(&runtime, &error) != RC_OK) {
        fprintf(stderr, "cleanup: %s (errno %d); runtime retained\n",
                rc_status_string(error.status), error.system_errno);
        failed = 1;
    }
    return failed;
}
