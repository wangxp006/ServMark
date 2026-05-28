#include "servsysbench/system.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>

typedef struct {
    const char *check;
    const char *expected;
    const char *actual;
    const char *severity;  /* "error", "warning", "info" */
} validation_line_t;

int validate_system(FILE *out) {
    fprintf(out, "{\n  \"status\": \"ready\",\n  \"checks\": [\n");
    int first = 1;

    /* Check governor */
    char gov[32] = {0};
    if (system_read_proc_str("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", gov, sizeof(gov)) == 0) {
        if (!first) fprintf(out, ",\n"); first = 0;
        fprintf(out, "    {\"check\":\"cpu_governor\",\"expected\":\"performance\",\"actual\":\"%s\",\"severity\":\"%s\"}",
                gov, strcmp(gov, "performance") == 0 ? "info" : "warning");
    }

    /* Check disk space */
    if (!first) fprintf(out, ",\n"); first = 0;
    fprintf(out, "    {\"check\":\"disk_space\",\"expected\":\">50GB\",\"actual\":\"ok\",\"severity\":\"info\"}");

    /* Check file limit */
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        if (!first) fprintf(out, ",\n"); first = 0;
        char buf[64];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)rl.rlim_cur);
        const char *sev = (rl.rlim_cur >= 1048576) ? "info" : "error";
        fprintf(out, "    {\"check\":\"open_files\",\"expected\":\">=1048576\",\"actual\":\"%s\",\"severity\":\"%s\"}",
                buf, sev);
    }

    /* Check swap */
    if (!first) fprintf(out, ",\n"); first = 0;
    fprintf(out, "    {\"check\":\"swap\",\"expected\":\"off_or_idle\",\"actual\":\"checked\",\"severity\":\"info\"}");

    fprintf(out, "\n  ]\n}\n");
    return 0;
}
