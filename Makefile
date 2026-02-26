# xpmem ベンチマーク Makefile
#
# 使い方:
#   make              # 全てビルド
#   make xpmem        # xpmemバイナリのみ
#   make shm          # SHMバイナリのみ
#   make clean        # クリーンアップ

CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -std=gnu11 -march=native
LDFLAGS  =

# xpmem のインストール先 (configure --prefix で指定したパス)
XPMEM_PREFIX ?= /usr/local
XPMEM_INC    = -I$(XPMEM_PREFIX)/include
XPMEM_LIB    = -L$(XPMEM_PREFIX)/lib -lxpmem -Wl,-rpath,$(XPMEM_PREFIX)/lib

# ターゲット
XPMEM_TARGETS = xpmem_exporter xpmem_importer
SHM_TARGETS   = shm_bench
ALL_TARGETS   = $(XPMEM_TARGETS) $(SHM_TARGETS)

.PHONY: all xpmem shm clean help

all: $(ALL_TARGETS)

xpmem: $(XPMEM_TARGETS)

shm: $(SHM_TARGETS)

# xpmem バイナリ
xpmem_exporter: xpmem_exporter.c common.h
	$(CC) $(CFLAGS) $(XPMEM_INC) -o $@ $< $(XPMEM_LIB) -lrt

xpmem_importer: xpmem_importer.c common.h
	$(CC) $(CFLAGS) $(XPMEM_INC) -o $@ $< $(XPMEM_LIB) -lrt

# POSIX共有メモリベンチマーク (xpmemライブラリ不要)
shm_bench: shm_bench.c common.h
	$(CC) $(CFLAGS) -o $@ $< -lrt -lpthread

clean:
	rm -f $(ALL_TARGETS) *.o
	rm -f /tmp/xpmem_segid /tmp/xpmem_ready /tmp/xpmem_done

help:
	@echo "使い方:"
	@echo "  make           - 全てビルド"
	@echo "  make xpmem     - xpmemバイナリのみ"
	@echo "  make shm       - POSIX共有メモリベンチマークのみ"
	@echo "  make clean     - クリーンアップ"
	@echo ""
	@echo "XPMEM_PREFIX=$(XPMEM_PREFIX) でインストール先を変更可能"
