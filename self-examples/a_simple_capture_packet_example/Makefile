# 简体中文说明：
# 这个 Makefile 用于在当前目录编译 examples/3-capture_packet/main.c
# - 假设系统已安装 DPDK 并且有可用的 pkg-config 条目 `libdpdk`。
# - 如果出现 `pqos.h file not found`，请安装 Intel PQoS (libpqos) 或通过环境变量 EXTRA_CFLAGS 指定其 include 目录。
# 使用方法：
#   make            # 构建可执行文件
#   make clean      # 删除目标文件和可执行文件
#   make EXTRA_CFLAGS="-I/path/to/pqos/include" EXTRA_LIBS="-L/.. -lpqos"  # 自定义额外编译/链接参数

CC ?= gcc
PKGCONFIG ?= pkg-config

TARGET ?= capture_packet
SRCS := main.c
OBJS := $(SRCS:.c=.o)

# 从 pkg-config 获取 DPDK 编译/链接参数（若不可用，则为空）
DPDK_CFLAGS := $(shell $(PKGCONFIG) --cflags libdpdk 2>/dev/null)
DPDK_LIBS := $(shell $(PKGCONFIG) --libs libdpdk 2>/dev/null)

# 尝试获取 libpqos（Intel PQoS）的 pkg-config 信息（若存在）
PQOS_CFLAGS := $(shell $(PKGCONFIG) --cflags libpqos 2>/dev/null)
PQOS_LIBS := $(shell $(PKGCONFIG) --libs libpqos 2>/dev/null)

# 允许通过环境变量额外添加 include/lib 参数
EXTRA_CFLAGS ?=
EXTRA_LIBS ?=

CFLAGS := -O2 -g -Wall $(DPDK_CFLAGS) $(PQOS_CFLAGS) $(EXTRA_CFLAGS)
LDFLAGS := $(DPDK_LIBS) $(PQOS_LIBS) $(EXTRA_LIBS)

.PHONY: all clean distclean
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

# 完全清理（可能不常用）
distclean: clean
	rm -f *~

# 帮助信息
help:
	@echo "使用示例："
	@echo "  make                     # 构建 $(TARGET)"
	@echo "  make EXTRA_CFLAGS=\"-I/path/to/pqos/include\" EXTRA_LIBS=\"-L/path/to/pqos/lib -lpqos\""
	@echo "如果系统找不到 pqos.h：安装 libpqos 或指定 EXTRA_CFLAGS 指向其 include 目录。"
