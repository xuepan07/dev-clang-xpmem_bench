#!/bin/bash
#
# run_bench.sh - xpmem vs POSIX SHM 自動ベンチマークスクリプト
#
# このスクリプトは:
# 1. xpmemカーネルモジュールの確認
# 2. xpmemベンチマークの実行 (exporter + importer)
# 3. POSIX共有メモリベンチマークの実行
# 4. 結果の保存
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_DIR="$SCRIPT_DIR/results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$LOG_DIR/bench_${TIMESTAMP}.log"

# ========== ユーティリティ ==========

log() {
    echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG_FILE"
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

# ========== 前提条件チェック ==========

check_prerequisites() {
    log "=== 前提条件チェック ==="

    # バイナリの存在確認
    if [ ! -f "$SCRIPT_DIR/xpmem_exporter" ] || [ ! -f "$SCRIPT_DIR/xpmem_importer" ]; then
        log "xpmemバイナリが見つかりません。ビルドを試みます..."
        cd "$SCRIPT_DIR" && make xpmem 2>&1 | tee -a "$LOG_FILE"
    fi

    if [ ! -f "$SCRIPT_DIR/shm_bench" ]; then
        log "SHMバイナリが見つかりません。ビルドを試みます..."
        cd "$SCRIPT_DIR" && make shm 2>&1 | tee -a "$LOG_FILE"
    fi

    # xpmemモジュール確認
    if [ ! -e /dev/xpmem ]; then
        log "WARNING: /dev/xpmem が見つかりません"
        log "xpmemカーネルモジュールをロードします..."
        sudo modprobe xpmem 2>/dev/null || \
        sudo insmod /usr/local/lib/modules/$(uname -r)/xpmem.ko 2>/dev/null || {
            log "WARNING: xpmemモジュールのロードに失敗しました"
            log "xpmemベンチマークはスキップされます"
            log "手動でロード: sudo insmod <path>/xpmem.ko"
            SKIP_XPMEM=1
        }
    fi

    if [ -e /dev/xpmem ]; then
        # パーミッション確認
        if [ ! -r /dev/xpmem ] || [ ! -w /dev/xpmem ]; then
            log "パーミッション修正: /dev/xpmem"
            sudo chmod 666 /dev/xpmem
        fi
        log "xpmem: OK (/dev/xpmem 存在)"
    fi
}

# ========== システム情報 ==========

collect_sysinfo() {
    log ""
    log "=== システム情報 ==="
    log "日時: $(date)"
    log "ホスト名: $(hostname)"
    log "カーネル: $(uname -r)"
    log "CPU: $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
    log "CPUコア数: $(nproc)"
    log "メモリ: $(free -h | grep Mem | awk '{print $2}')"

    if [ -e /proc/version ]; then
        grep -qi "microsoft" /proc/version && log "環境: WSL2" || log "環境: ネイティブ Linux"
    fi

    log ""
}

# ========== xpmemベンチマーク ==========

run_xpmem_bench() {
    if [ "${SKIP_XPMEM:-0}" = "1" ]; then
        log "=== xpmem ベンチマーク: スキップ ==="
        return
    fi

    log "=== xpmem ベンチマーク開始 ==="

    # クリーンアップ
    rm -f /tmp/xpmem_segid /tmp/xpmem_ready /tmp/xpmem_done

    # エクスポータをバックグラウンドで起動
    log "エクスポータ起動中..."
    "$SCRIPT_DIR/xpmem_exporter" 2>&1 | tee -a "$LOG_FILE" &
    EXPORTER_PID=$!

    # エクスポータの準備完了を待つ
    local wait_count=0
    while [ ! -f /tmp/xpmem_ready ] && [ $wait_count -lt 30 ]; do
        sleep 1
        wait_count=$((wait_count + 1))
    done

    if [ ! -f /tmp/xpmem_ready ]; then
        log "ERROR: エクスポータがタイムアウトしました"
        kill $EXPORTER_PID 2>/dev/null
        return 1
    fi

    # インポータ実行
    log "インポータ (ベンチマーク) 開始..."
    "$SCRIPT_DIR/xpmem_importer" 2>&1 | tee -a "$LOG_FILE"

    # エクスポータの終了を待つ
    wait $EXPORTER_PID 2>/dev/null || true

    log "=== xpmem ベンチマーク完了 ==="
    log ""
}

# ========== SHMベンチマーク ==========

run_shm_bench() {
    log "=== POSIX共有メモリ ベンチマーク開始 ==="

    "$SCRIPT_DIR/shm_bench" 2>&1 | tee -a "$LOG_FILE"

    log "=== POSIX共有メモリ ベンチマーク完了 ==="
    log ""
}

# ========== メイン ==========

main() {
    mkdir -p "$LOG_DIR"

    echo "================================================="
    echo "  xpmem vs POSIX SHM ベンチマーク"
    echo "  結果ファイル: $LOG_FILE"
    echo "================================================="
    echo ""

    collect_sysinfo
    check_prerequisites
    run_xpmem_bench
    run_shm_bench

    log "================================================="
    log "  全ベンチマーク完了"
    log "  結果: $LOG_FILE"
    log "================================================="
}

main "$@"
