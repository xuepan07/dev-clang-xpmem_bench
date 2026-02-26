/*
 * xpmem_exporter.c - XPMEM メモリエクスポータ (サーバ側)
 *
 * このプロセスは:
 * 1. 大容量メモリ領域を確保し、検証パターンで埋める
 * 2. xpmem_make() でメモリ領域を公開する
 * 3. セグメントIDをファイル経由でインポータに通知する
 * 4. インポータがコピーを完了するまで待機する
 *
 * 使い方:
 *   ./xpmem_exporter [最大テストサイズ(MB)]
 *
 * コンパイル:
 *   gcc -O2 -o xpmem_exporter xpmem_exporter.c -lxpmem -lrt
 */

#include <xpmem.h>
#include "common.h"

static volatile int g_running = 1;

static void sigint_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(int argc, char *argv[])
{
    /* 最大テストサイズの決定 */
    size_t max_size = TEST_SIZES[NUM_TEST_SIZES - 1];
    if (argc > 1) {
        max_size = (size_t)atol(argv[1]) * 1024UL * 1024;
    }

    char sizebuf[64];
    printf("=== xpmem Exporter ===\n");
    printf("PID: %d\n", getpid());
    printf("最大テストサイズ: %s\n", format_size(max_size, sizebuf, sizeof(sizebuf)));

    /* シグナルハンドラ設定 */
    signal(SIGINT, sigint_handler);  /* Ctrl + C */

    /* 同期ファイルのクリーンアップ */
    cleanup_sync_files();

    /* ページアラインドメモリの確保 */
    printf("メモリ確保中...\n");
    void *shared_buf = alloc_aligned(max_size);
    if (!shared_buf) {
        fprintf(stderr, "メモリ確保失敗: %s\n", format_size(max_size, sizebuf, sizeof(sizebuf)));
        return 1;
    }

    /* ページフォルトを事前に発生させる (メモリを実際に確保) */
    printf("メモリ初期化中 (ページフォルト解消)...\n");
    memset(shared_buf, 0, max_size);

    /* インポータにコピーする検証パターンの書き込み */
    printf("テストパターン書き込み中...\n");
    fill_pattern(shared_buf, max_size);

    /* xpmem セグメントの作成 (エクスポート) */
    printf("xpmem セグメント作成中...\n");
    xpmem_segid_t segid = xpmem_make(shared_buf, max_size,
                                      XPMEM_PERMIT_MODE, (void *)0666);
    if (segid == -1) {
        perror("xpmem_make 失敗");
        fprintf(stderr, "\n/dev/xpmem が存在するか確認してください:\n");
        fprintf(stderr, "  ls -la /dev/xpmem\n");
        fprintf(stderr, "  sudo insmod /usr/local/lib/modules/$(uname -r)/xpmem.ko\n");
        free(shared_buf);
        return 1;
    }

    printf("セグメントID: %lld\n", (long long)segid);

    /* セグメントIDをファイルに書き出し */
    FILE *fp = fopen(SEGID_FILE, "w");
    if (!fp) {
        perror("セグメントIDファイル書き込み失敗");
        xpmem_remove(segid);
        free(shared_buf);
        return 1;
    }
    fprintf(fp, "%lld\n%zu\n%d\n", (long long)segid, max_size, getpid());
    fclose(fp);

    /* インポータに準備完了を通知 */
    signal_file(READY_FILE);
    printf("インポータ待機中... (Ctrl+C で終了)\n\n");

    /* インポータの完了待ち */
    while (g_running) {
        if (access(DONE_FILE, F_OK) == 0) {
            printf("\nインポータからの完了通知を受信\n");
            break;
        }
        usleep(100000); /* 100ms */
    }

    /* クリーンアップ */
    printf("クリーンアップ中...\n");
    xpmem_remove(segid);
    free(shared_buf);
    cleanup_sync_files();

    printf("エクスポータ終了\n");
    return 0;
}
