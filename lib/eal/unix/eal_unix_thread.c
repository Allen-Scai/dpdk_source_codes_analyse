/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Red Hat, Inc.
 */

#include <errno.h>
#include <unistd.h>

#include <rte_debug.h>

#include "eal_private.h"

int eal_thread_wake_worker(unsigned int worker_id)
{
    // 获取与指定工作线程通信的管道文件描述符
    // m2w: 主线程到工作线程的管道写入端（主线程通过向这个管道写入数据来唤醒工作线程）
    int m2w = lcore_config[worker_id].pipe_main2worker[1];
    // w2m: 工作线程到主线程的管道读取端（主线程从这个管道读取数据，以等待工作线程的响应）
    int w2m = lcore_config[worker_id].pipe_worker2main[0];
    char c = 0;  // 用于读写的一个字节数据
    int n;       // 用于存储读写操作的返回值

    // 主线程向工作线程发送唤醒信号
    do {
        // 向管道写入一个字节，如果工作线程在等待读取这个管道，它将被唤醒
        n = write(m2w, &c, 1);
        // 如果写入返回0（通常不会发生）或者写入被中断（EINTR），则重试
    } while (n == 0 || (n < 0 && errno == EINTR));
    
    // 如果写入失败（非EINTR错误），返回-EPIPE错误
    if (n < 0)
        return -EPIPE;

    // 主线程等待工作线程的响应，对应eal_thread_ack_command()函数
    do {
        // 从工作线程到主线程的管道中读取一个字节，等待工作线程的确认
        n = read(w2m, &c, 1);
        // 如果读取被中断，则重试
    } while (n < 0 && errno == EINTR);
    
    // 如果读取失败或返回0（管道关闭），返回-EPIPE错误
    if (n <= 0)
        return -EPIPE;
    
    // 成功唤醒工作线程并收到确认，返回0
    return 0;
}

void
eal_thread_wait_command(void)
{
	unsigned int lcore_id = rte_lcore_id();
	int m2w;
	char c;
	int n;

	m2w = lcore_config[lcore_id].pipe_main2worker[0];
	do {
		n = read(m2w, &c, 1);
	} while (n < 0 && errno == EINTR);
	if (n <= 0)
		rte_panic("cannot read on configuration pipe\n");
}

void
eal_thread_ack_command(void)
{
	unsigned int lcore_id = rte_lcore_id();
	char c = 0;
	int w2m;
	int n;

	w2m = lcore_config[lcore_id].pipe_worker2main[1];
	do {
		n = write(w2m, &c, 1);
	} while (n == 0 || (n < 0 && errno == EINTR));
	if (n < 0)
		rte_panic("cannot write on configuration pipe\n");
}
