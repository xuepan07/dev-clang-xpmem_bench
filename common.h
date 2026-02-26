/*
 * common.h - xpmem ベンチマーク共通定義
 *
 * プロセス間でセグメントIDを共有するためにファイルベースの通信を使用。
 * 計測ユーティリティ、データ検証関数を含む。
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

/* ========== 設定パラメータ ========== */

/* テストするデータサイズ一覧 (bytes) */
static const size_t TEST_SIZES[] = {
    4UL * 1024,              /*   4 KB */
    64UL * 1024,             /*  64 KB */
    1UL * 1024 * 1024,       /*   1 MB */
    16UL * 1024 * 1024,      /*  16 MB */
    64UL * 1024 * 1024,      /*  64 MB */
    256UL * 1024 * 1024,     /* 256 MB */
    512UL * 1024 * 1024,     /* 512 MB */
    1024UL * 1024 * 1024,    /*   1 GB */
};
#define NUM_TEST_SIZES (sizeof(TEST_SIZES) / sizeof(TEST_SIZES[0]))

/* 各サイズでの繰り返し回数 */
#define REPEAT_COUNT 5

/* セグメントID共有用ファイル */
#define SEGID_FILE "/tmp/xpmem_segid"

/* 同期用ファイル */
#define READY_FILE "/tmp/xpmem_ready"
#define DONE_FILE  "/tmp/xpmem_done"

/* POSIX共有メモリ名 */
#define SHM_NAME "/xpmem_bench_shm"

/* ========== 高精度タイマー ========== */

static inline double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ========== サイズ表示ユーティリティ ========== */

static inline const char *format_size(size_t bytes, char *buf, size_t buflen)
{
    if (bytes >= 1024UL * 1024 * 1024)
        snprintf(buf, buflen, "%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024UL * 1024)
        snprintf(buf, buflen, "%.1f MB", (double)bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        snprintf(buf, buflen, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, buflen, "%zu B", bytes);
    return buf;
}

/* ========== データ初期化・検証 ========== */

/* 検証可能なパターンでバッファを埋める */
static inline void fill_pattern(void *buf, size_t size)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < size; i++) {
        p[i] = (uint8_t)(i & 0xFF);
    }
}

/* パターンの検証。成功なら0、失敗なら不一致のオフセット+1を返す */
static inline size_t verify_pattern(const void *buf, size_t size)
{
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < size; i++) {
        if (p[i] != (uint8_t)(i & 0xFF))
            return i + 1;
    }
    return 0;
}

/* ========== 同期ユーティリティ ========== */

static inline void signal_file(const char *path)
{
    int fd = open(path, O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) close(fd);
}

static inline void wait_for_file(const char *path)
{
    while (access(path, F_OK) != 0) {
        usleep(10000); /* 10ms */
    }
}

static inline void cleanup_sync_files(void)
{
    unlink(SEGID_FILE);
    unlink(READY_FILE);
    unlink(DONE_FILE);
}

/* ========== 結果表示 ========== */

static inline void print_result(const char *method, size_t size,
                                double elapsed_sec, int iteration)
{
    char sizebuf[64];
    double bandwidth_gbps = (double)size / elapsed_sec / (1024.0 * 1024 * 1024);
    double latency_us = elapsed_sec * 1e6;

    format_size(size, sizebuf, sizeof(sizebuf));

    printf("  [%s] %8s | iter %d | %.6f sec | %8.2f GB/s | %12.1f us\n",
           method, sizebuf, iteration, elapsed_sec, bandwidth_gbps, latency_us);
}

static inline void print_summary(const char *method, size_t size,
                                 double *times, int count)
{
    char sizebuf[64];
    format_size(size, sizebuf, sizeof(sizebuf));

    double min_t = times[0], max_t = times[0], sum_t = 0;
    for (int i = 0; i < count; i++) {
        if (times[i] < min_t) min_t = times[i];
        if (times[i] > max_t) max_t = times[i];
        sum_t += times[i];
    }
    double avg_t = sum_t / count;
    double avg_bw = (double)size / avg_t / (1024.0 * 1024 * 1024);

    printf("  [%s] %8s | avg %.6f sec | avg %8.2f GB/s | min %.6f | max %.6f\n",
           method, sizebuf, avg_t, avg_bw, min_t, max_t);
}

/* ========== ページアラインドメモリ確保 ========== */

static inline void *alloc_aligned(size_t size)
{
    void *ptr = NULL;
    if (posix_memalign(&ptr, 4096, size) != 0) {
        perror("posix_memalign");
        return NULL;
    }
    return ptr;
}

#endif /* COMMON_H */
