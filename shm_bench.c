/*
 * shm_bench.c - POSIX共有メモリ (shm) ベンチマーク
 *
 * xpmemとの性能比較のため、POSIX共有メモリを使った
 * プロセス間データコピーの速度を計測する。
 *
 * fork()で子プロセスを作り、親が書き込み → 子が読み取りの
 * パターンでベンチマークを行う。
 *
 * コンパイル:
 *   gcc -O2 -o shm_bench shm_bench.c -lrt -lpthread
 */

#include "common.h"
#include <sys/wait.h>

/* 共有メモリ上の制御構造体 */
typedef struct {
    volatile int phase;     /* 0: 待機, 1: データ準備完了, 2: コピー完了 */
    size_t data_size;
    int iteration;
    double copy_time;       /* 子プロセスが計測した時間 */
    size_t verify_err;      /* 検証結果 */
} shm_control_t;

#define CTRL_SHM_NAME "/xpmem_bench_ctrl"

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    size_t max_size = TEST_SIZES[NUM_TEST_SIZES - 1];
    char sizebuf[64];

    printf("=== POSIX共有メモリ (shm) ベンチマーク ===\n");
    printf("最大テストサイズ: %s\n\n", format_size(max_size, sizebuf, sizeof(sizebuf)));

    /* 制御用共有メモリ */
    shm_unlink(CTRL_SHM_NAME);
    int ctrl_fd = shm_open(CTRL_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (ctrl_fd < 0) { perror("shm_open ctrl"); return 1; }
    /*ftruncate(ctrl_fd, sizeof(shm_control_t));*/
    if (ftruncate(ctrl_fd, sizeof(shm_control_t)) == -1) {
        perror("ftruncate ctrl");
        exit(1);
    }

    shm_control_t *ctrl = mmap(NULL, sizeof(shm_control_t),
                                PROT_READ | PROT_WRITE, MAP_SHARED, ctrl_fd, 0);
    close(ctrl_fd);
    memset(ctrl, 0, sizeof(shm_control_t));

    /* データ用共有メモリ */
    shm_unlink(SHM_NAME);
    int data_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (data_fd < 0) { perror("shm_open data"); return 1; }
    /*ftruncate(data_fd, max_size);*/
    if (ftruncate(data_fd, max_size) == -1) {
        perror("ftruncate data");
        exit(1);
    }

    void *shm_ptr = mmap(NULL, max_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, data_fd, 0);
    close(data_fd);

    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* ページフォルト解消 */
    memset(shm_ptr, 0, max_size);

    pid_t pid = fork();

    if (pid == 0) {
        /* ===== 子プロセス (読み取り側) ===== */
        void *local_buf = alloc_aligned(max_size);
        if (!local_buf) { _exit(1); }
        memset(local_buf, 0, max_size);

        while (1) {
            /* データ準備完了を待つ */
            while (ctrl->phase != 1) {
                usleep(100);
            }

            size_t size = ctrl->data_size;
            if (size == 0) break; /* 終了シグナル */

            /* 共有メモリからローカルへコピー (計測) */
            memset(local_buf, 0, size);
            double t0 = get_time_sec();
            memcpy(local_buf, shm_ptr, size);
            double t1 = get_time_sec();

            ctrl->copy_time = t1 - t0;

            /* 検証 */
            ctrl->verify_err = verify_pattern(local_buf, size);

            /* コピー完了通知 */
            ctrl->phase = 2;
        }

        free(local_buf);
        _exit(0);

    } else {
        /* ===== 親プロセス (書き込み側) ===== */

        printf("--- POSIX shm memcpy ベンチマーク ---\n");
        printf("  (共有メモリ → 別プロセスのローカルバッファへ memcpy)\n\n");

        for (size_t si = 0; si < NUM_TEST_SIZES; si++) {
            size_t size = TEST_SIZES[si];
            if (size > max_size) break;

            double times[REPEAT_COUNT];

            for (int r = 0; r < REPEAT_COUNT; r++) {
                /* データを共有メモリに書き込む */
                fill_pattern(shm_ptr, size);

                ctrl->data_size = size;
                ctrl->iteration = r;
                ctrl->phase = 1; /* 子に通知 */

                /* 子プロセスのコピー完了を待つ */
                while (ctrl->phase != 2) {
                    usleep(100);
                }

                times[r] = ctrl->copy_time;
                print_result("SHM-cpy  ", size, times[r], r + 1);

                if (r == 0) {
                    if (ctrl->verify_err) {
                        fprintf(stderr, "  *** データ不整合! offset=%zu ***\n",
                                ctrl->verify_err - 1);
                    } else {
                        printf("  ✓ データ検証OK\n");
                    }
                }

                ctrl->phase = 0;
            }
            print_summary("SHM-cpy  ", size, times, REPEAT_COUNT);
            printf("\n");
        }

        /* 子プロセスに終了を通知 */
        ctrl->data_size = 0;
        ctrl->phase = 1;
        wait(NULL);

        printf("ベンチマーク完了\n");
    }

    /* クリーンアップ */
    munmap(shm_ptr, max_size);
    munmap(ctrl, sizeof(shm_control_t));
    shm_unlink(SHM_NAME);
    shm_unlink(CTRL_SHM_NAME);

    return 0;
}
