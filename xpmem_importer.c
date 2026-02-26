/*
 * xpmem_importer.c - xpmem メモリインポータ (クライアント側) + ベンチマーク
 *
 * このプロセスは:
 * 1. エクスポータからセグメントIDを取得する
 * 2. xpmem_get() + xpmem_attach() でメモリをマッピングする
 * 3. 各サイズで memcpy による転送速度を計測する
 * 4. データの整合性を検証する
 * 5. xpmem直接アクセス (ゼロコピー) の速度も計測する
 *
 * 使い方:
 *   ./xpmem_importer
 *
 * コンパイル:
 *   gcc -O2 -o xpmem_importer xpmem_importer.c -lxpmem -lrt
 */

#include <xpmem.h>
#include "common.h"

/*
 * xpmem経由のmemcpyベンチマーク
 * attached_ptr: xpmem_attach()で得たリモートメモリへのポインタ
 * local_buf:    ローカルバッファ (コピー先)
 */
static void bench_xpmem_memcpy(void *attached_ptr, void *local_buf,
                                size_t max_size)
{
    printf("\n--- xpmem memcpy ベンチマーク ---\n");
    printf("  (リモートプロセスのメモリ → ローカルバッファへ memcpy)\n\n");

    for (size_t si = 0; si < NUM_TEST_SIZES; si++) {
        size_t size = TEST_SIZES[si];
        if (size > max_size) break;

        double times[REPEAT_COUNT];

        for (int r = 0; r < REPEAT_COUNT; r++) {
            /* ローカルバッファをクリア (キャッシュ効果を排除) */
            memset(local_buf, 0, size);

            /* 計測開始 */
            double t0 = get_time_sec();

            /* xpmemマッピングされたメモリからローカルへコピー */
            memcpy(local_buf, attached_ptr, size);

            /* 計測終了 */
            double t1 = get_time_sec();
            times[r] = t1 - t0;

            print_result("xpmem-cpy", size, times[r], r + 1);

            /* データ検証 (最初の1回だけ) */
            if (r == 0) {
                size_t err = verify_pattern(local_buf, size);
                if (err) {
                    fprintf(stderr, "  *** データ不整合! offset=%zu ***\n", err - 1);
                } else {
                    printf("  ✓ データ検証OK\n");
                }
            }
        }
        print_summary("xpmem-cpy", size, times, REPEAT_COUNT);
        printf("\n");
    }
}

/*
 * xpmem直接読み取り (ゼロコピー) ベンチマーク
 * マッピングされたメモリを直接走査して合計を計算 (実際のアクセスを強制)
 */
static void bench_xpmem_direct(void *attached_ptr, size_t max_size)
{
    printf("\n--- xpmem 直接アクセス (ゼロコピー) ベンチマーク ---\n");
    printf("  (リモートメモリを直接 load して走査)\n\n");

    for (size_t si = 0; si < NUM_TEST_SIZES; si++) {
        size_t size = TEST_SIZES[si];
        if (size > max_size) break;

        double times[REPEAT_COUNT];

        for (int r = 0; r < REPEAT_COUNT; r++) {
            volatile uint64_t checksum = 0;
            const uint64_t *p = (const uint64_t *)attached_ptr;
            size_t count = size / sizeof(uint64_t);

            double t0 = get_time_sec();

            /* 直接読み取り (ページフォルトでオンデマンドマッピング) */
            for (size_t i = 0; i < count; i++) {
                checksum += p[i];
            }

            double t1 = get_time_sec();
            times[r] = t1 - t0;

            print_result("xpmem-dir", size, times[r], r + 1);

            /* volatile変数を使い最適化抑制 */
            (void)checksum;
        }
        print_summary("xpmem-dir", size, times, REPEAT_COUNT);
        printf("\n");
    }
}

/*
 * ローカル memcpy ベンチマーク (基準値)
 */
static void bench_local_memcpy(size_t max_size)
{
    printf("\n--- ローカル memcpy ベンチマーク (基準値) ---\n");
    printf("  (同一プロセス内の memcpy)\n\n");

    void *src = alloc_aligned(max_size);
    void *dst = alloc_aligned(max_size);
    if (!src || !dst) {
        fprintf(stderr, "ローカルバッファ確保失敗\n");
        free(src); free(dst);
        return;
    }

    /* ページフォルト解消 */
    memset(src, 0xAA, max_size);
    memset(dst, 0, max_size);

    for (size_t si = 0; si < NUM_TEST_SIZES; si++) {
        size_t size = TEST_SIZES[si];
        if (size > max_size) break;

        double times[REPEAT_COUNT];

        for (int r = 0; r < REPEAT_COUNT; r++) {
            memset(dst, 0, size);

            double t0 = get_time_sec();
            memcpy(dst, src, size);
            double t1 = get_time_sec();

            times[r] = t1 - t0;
            print_result("LOCAL-cpy", size, times[r], r + 1);
        }
        print_summary("LOCAL-cpy", size, times, REPEAT_COUNT);
        printf("\n");
    }

    free(src);
    free(dst);
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("=== xpmem Importer / ベンチマーク ===\n");
    printf("PID: %d\n\n", getpid());

    /* エクスポータの準備完了を待つ */
    printf("エクスポータの準備完了待ち...\n");
    wait_for_file(READY_FILE);

    /* セグメントID読み込み */
    FILE *fp = fopen(SEGID_FILE, "r");
    if (!fp) {
        perror("セグメントIDファイル読み込み失敗");
        return 1;
    }

    long long segid_ll;
    size_t max_size;
    int exporter_pid;
    if (fscanf(fp, "%lld\n%zu\n%d", &segid_ll, &max_size, &exporter_pid) != 3) {
        fprintf(stderr, "セグメントIDファイルのフォーマットが不正\n");
        fclose(fp);
        return 1;
    }
    fclose(fp);

    xpmem_segid_t segid = (xpmem_segid_t)segid_ll;
    char sizebuf[64];
    printf("エクスポータPID: %d\n", exporter_pid);
    printf("セグメントID: %lld\n", segid_ll);
    printf("最大サイズ: %s\n", format_size(max_size, sizebuf, sizeof(sizebuf)));

    /* xpmem アクセス許可の取得 */
    printf("xpmem_get()...\n");
    /*xpmem_apid_t apid = xpmem_get(segid, XPMEM_RDONLY, XPMEM_PERMIT_MODE,*/
    xpmem_apid_t apid = xpmem_get(segid, XPMEM_RDWR, XPMEM_PERMIT_MODE,
                                    (void *)0666);
                                    /*(void *)0444);*/
    if (apid == -1) {
        perror("xpmem_get 失敗");
        return 1;
    }
    printf("APID: %lld\n", (long long)apid);

    /* xpmem メモリのアタッチ */
    printf("xpmem_attach()...\n");
    struct xpmem_addr addr;
    addr.apid = apid;
    addr.offset = 0;

    void *attached_ptr = xpmem_attach(addr, max_size, NULL);
    if (attached_ptr == (void *)-1) {
        perror("xpmem_attach 失敗");
        xpmem_release(apid);
        return 1;
    }
    printf("アタッチアドレス: %p\n", attached_ptr);

    /* エクスポータから共有メモリ経由で受け取ったデータを */
	/* ローカルプロセス内でコピーする先のバッファの確保 */
    void *local_buf = alloc_aligned(max_size);
    if (!local_buf) {
        fprintf(stderr, "ローカルバッファ確保失敗\n");
        xpmem_detach(attached_ptr);
        xpmem_release(apid);
        return 1;
    }

    printf("\n========================================\n");
    printf("  ベンチマーク開始\n");
    printf("  繰り返し回数: %d\n", REPEAT_COUNT);
    printf("========================================\n");

    /* ベンチマーク実行 */

    /* 1. xpmem memcpy (リモート→ローカル) */
    bench_xpmem_memcpy(attached_ptr, local_buf, max_size);

    /* 2. xpmem 直接アクセス (ゼロコピー) */
    bench_xpmem_direct(attached_ptr, max_size);

    /* 3. プロセス内のローカル memcpy (基準値) */
    bench_local_memcpy(max_size);

    /* 結果サマリ */
    printf("\n========================================\n");
    printf("  ベンチマーク完了\n");
    printf("========================================\n");

    /* クリーンアップ */
    free(local_buf);
    xpmem_detach(attached_ptr);
    xpmem_release(apid);

    /* エクスポータに完了通知 */
    signal_file(DONE_FILE);

    printf("インポータ終了\n");
    return 0;
}
