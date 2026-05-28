#ifndef SSB_OUTPUT_H
#define SSB_OUTPUT_H
#include "harness.h"
int output_provenance(const run_result_t *result, const char *path);
int output_jsonl(const run_result_t *result, const char *path);
int output_terminal_summary(const run_result_t *result);
int output_html_report(const run_result_t *result, const char *path);
void output_generate_uuid(char buf[37]);
int output_compute_sha256(const char *data, char hex_out[65]);
#endif
