#include <arpa/inet.h>
#include <inttypes.h>
#include <rte_arp.h>
#include <rte_eth_ctrl.h>
#include <rte_lcore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h> // 关键：包含IP相关头文件
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_time.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024 // 新增TX环大小定义
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

// 全局变量
static volatile bool force_quit = false;
static struct rte_mempool *mbuf_pool = NULL;

// 时间戳相关变量
static uint64_t tsc_hz = 0;        // TSC频率
static uint64_t tsc_base_time = 0; // tsc基准时间（纳秒）
static uint64_t tsc_start = 0;     // 程序启动时的TSC值

// 统计信息
static uint64_t total_packets = 0;
static uint64_t total_bytes = 0;

// 信号处理函数
static void signal_handler(int signum)
{
    // SIGINT: Ctrl + C
    // SIGTERM: kill命令
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = true;
    }
}

// 初始化时间戳系统
static int init_timestamp_system(void)
{
    // 获取TSC频率
    tsc_hz = rte_get_tsc_hz();
    if (tsc_hz == 0) {
        printf("Error: Cannot get TSC frequency\n");
        return -1;
    }

    // 记录程序启动时的TSC值
    tsc_start = rte_rdtsc();

    // 设置基准时间，转化为纳秒值
    struct timespec base_ts;
    clock_gettime(CLOCK_REALTIME, &base_ts);
    // 将时钟的秒和纳秒，统一转换成纳秒
    tsc_base_time = (uint64_t)base_ts.tv_sec * 1000000000ULL + base_ts.tv_nsec;

    return 0;
}

// 高性能时间戳获取函数
static void get_packet_timestamp(uint64_t *tsc_cycles, uint64_t *wall_time_ns)
{
    // 获取TSC周期数（最高性能）
    *tsc_cycles = rte_rdtsc();

    // 检查TSC频率是否有效
    if (tsc_hz == 0) {
        printf("Warning: TSC frequency is 0, using system time\n");
        struct timespec time;
        clock_gettime(CLOCK_REALTIME, &time);
        *wall_time_ns = (uint64_t)time.tv_sec * 1000000000ULL + time.tv_nsec;
        return;
    }

    // 转换为纳秒时间戳
    // 计算从程序启动到现在的TSC差值，然后转换为时间差
    uint64_t tsc_elapsed = *tsc_cycles - tsc_start;

    // 使用更高精度的计算方法
    // 先计算秒数，再计算纳秒数，避免大数相乘溢出
    uint64_t elapsed_seconds = tsc_elapsed / tsc_hz;
    uint64_t elapsed_nanoseconds = ((tsc_elapsed % tsc_hz) * 1000000000ULL) / tsc_hz;

    *wall_time_ns = tsc_base_time + elapsed_seconds * 1000000000ULL + elapsed_nanoseconds;
}

// 修改后的端口初始化函数 (关键修改点)
static int init_port(uint16_t port, struct rte_mempool *mbuf_pool)
{
    // 注意：使用局部变量，以便根据设备信息修改配置
    struct rte_eth_conf port_conf = {
        .rxmode =
            {
                .mtu = RTE_ETHER_MAX_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN,
            },
        .txmode =
            {
                .mq_mode = RTE_ETH_MQ_TX_NONE,
            },
    };

    int retval;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE; // 新增TX描述符变量
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txq_conf;

    // 检查端口是否有效
    if (!rte_eth_dev_is_valid_port(port))
        return -1;

    // 获取设备信息（关键修改1：在配置前获取）
    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) {
        printf("Error getting device info for port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    // 关键修改2：根据设备能力调整配置（仿照l2fwd）
    // 如果设备支持快速释放卸载，则启用它（更优性能）
    if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

    // 关键修改3：配置设备：1个RX队列，1个TX队列（原为1,0）
    retval = rte_eth_dev_configure(port, 1, 1, &port_conf);
    if (retval != 0) {
        printf("Error configuring port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    // 关键修改4：调整RX和TX描述符数量（原TX参数为NULL）
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0) {
        printf("Error adjusting descriptors for port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    // 设置RX队列（保持不变）
    retval = rte_eth_rx_queue_setup(port, 0, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0) {
        printf("Error setting up RX queue for port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    // 关键修改5：设置TX队列（即使我们不发送数据，驱动也可能需要它）
    txq_conf = dev_info.default_txconf;
    txq_conf.offloads = port_conf.txmode.offloads;
    retval = rte_eth_tx_queue_setup(port, 0, nb_txd, rte_eth_dev_socket_id(port), &txq_conf);
    if (retval < 0) {
        printf("Error setting up TX queue for port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    // 启动设备
    retval = rte_eth_dev_start(port);
    if (retval < 0) {
        printf("Error starting port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    // 可选：禁用PTYPE解析以减少开销（仿照l2fwd）
    // 第二个参数是希望网卡帮忙识别出来的报文类型mask
    retval = rte_eth_dev_set_ptypes(port, RTE_PTYPE_UNKNOWN, NULL, 0);
    if (retval < 0) {
        printf("Port %u, Failed to disable Ptype parsing (non-fatal)\n", port);
    }

    // 获取并显示MAC地址
    struct rte_ether_addr mac_addr;
    char addr_buf[64];
    retval = rte_eth_macaddr_get(port, &mac_addr);
    if (retval != 0) {
        printf("Error getting MAC address for port %u: %s\n", port, strerror(-retval));
        return retval;
    }
    rte_ether_format_addr(addr_buf, sizeof(addr_buf), &mac_addr);
    printf("This port's mac address is %s\n", addr_buf);

    // 启用混杂模式以接收所有数据包
    retval = rte_eth_promiscuous_enable(port);
    if (retval != 0) {
        printf("Error enabling promiscuous mode for port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    printf("Port %u initialized successfully !\n", port);
    return 0;
}

// 简化的数据包处理函数
static void process_packet(struct rte_mbuf *pkt)
{
    // 1.从rte_mbuf结构中获取ethernet头
    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);

    // 1.1 获取ether_type
    uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
    // printf("ether_type: %04x\n", ether_type);

    // 1.2 获取mac address地址，并输出其值
    char src_mac[RTE_ETHER_ADDR_FMT_SIZE];
    char dst_mac[RTE_ETHER_ADDR_FMT_SIZE];
    rte_ether_format_addr(src_mac, RTE_ETHER_ADDR_FMT_SIZE, &(eth_hdr->src_addr));
    rte_ether_format_addr(dst_mac, RTE_ETHER_ADDR_FMT_SIZE, &(eth_hdr->dst_addr));
    printf("===========================\n");
    printf("MAC:  %s -> %s\n", src_mac, dst_mac);

    if (ether_type == RTE_ETHER_TYPE_IPV4) {
        // 2.从rte_mbuf结构中获取ipv4头
        struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(pkt, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));

        // 2.1 获取源地址和目的地址
        // uint32_t src_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);
        // uint32_t dst_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
        char srcip_str[64] = {0}; // INET_ADDRSTRLEN 定义为16
        char dstip_str[64] = {0};

        inet_ntop(AF_INET, &ipv4_hdr->src_addr, srcip_str, sizeof(srcip_str));
        inet_ntop(AF_INET, &ipv4_hdr->dst_addr, dstip_str, sizeof(dstip_str));

        // 以点分十进制格式输出IP地址
        printf("IPv4: %s -> %s\n", srcip_str, dstip_str);

        // 2.2 获取下一层的协议类型protocol
        uint8_t protocol = ipv4_hdr->next_proto_id;
        printf("protocol: %02x\n", protocol);

        switch (protocol) {
        case IPPROTO_TCP:
            printf("L4 protocol is  tcp!\n");
            break;
        case IPPROTO_UDP:
            printf("L4 protocol is udp!\n");
        default:
            printf("L4 protocol is %x!\n", protocol);
        }
    } else if (ether_type == RTE_ETHER_TYPE_ARP) {
        struct rte_arp_hdr *arp_hdr = rte_pktmbuf_mtod_offset(pkt, struct rte_arp_hdr *, sizeof(struct rte_ether_hdr));
        char sender_ip[64] = {0};
        char target_ip[64] = {0};
        inet_ntop(AF_INET, &arp_hdr->arp_data.arp_sip, sender_ip, sizeof(sender_ip));
        inet_ntop(AF_INET, &arp_hdr->arp_data.arp_tip, target_ip, sizeof(target_ip));

        printf("Packet is arp, %s -> %s\n", sender_ip, target_ip);
    }
    printf("===========================\n");
    // 更新统计
    total_packets++;
    total_bytes += pkt->pkt_len;
}

// 主抓包循环
static void capture_loop(void)
{
    uint16_t port;

    printf("\nStarting packet capture on %u ports. Input [Ctrl+C to quit], current lcore %u\n", rte_eth_dev_count_avail(), rte_lcore_id());

    while (!force_quit) {
        // 遍历所有端口
        RTE_ETH_FOREACH_DEV(port)
        {
            struct rte_mbuf *bufs[BURST_SIZE];

            // 批量接收数据包
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

            if (likely(nb_rx > 0)) {
                for (uint16_t i = 0; i < nb_rx; i++) {
                    // 处理每个数据包
                    process_packet(bufs[i]);
                    rte_pktmbuf_free(bufs[i]); // 释放mbuf
                }
            }
        }
    }
}

// 打印最终统计
static void print_final_stats(void)
{
    printf("\n=== Final Statistics ===\n");
    printf("Total packets captured: %" PRIu64 "\n", total_packets);
    printf("Total bytes captured: %" PRIu64 "\n", total_bytes);
    if (total_packets > 0) {
        printf("Average packet size: %.2f bytes\n", (double)total_bytes / total_packets);
    }
    printf("========================\n");
}

// 主函数
int main(int argc, char *argv[])
{
    int ret;
    uint16_t nb_ports;
    uint16_t portid;

    // 1. 初始化EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化时间戳系统
    if (init_timestamp_system() != 0)
        rte_exit(EXIT_FAILURE, "Error initializing timestamp system\n");

    // 2. 检查可用端口
    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");

    printf("Found %u avail Ethernet ports\n", nb_ports);

    // 3. 创建内存池
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    // 4. 初始化所有端口 (RX、TX队列都需要初始化)
    RTE_ETH_FOREACH_DEV(portid)
    {
        if (init_port(portid, mbuf_pool) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n", portid);
    }

    // 5. 开始抓包
    capture_loop();

    // 6. 清理工作
    printf("\n Try to shutdown all ports ......\n");

    RTE_ETH_FOREACH_DEV(portid)
    {
        printf("Closing port %u...", portid);
        rte_eth_dev_stop(portid);
        rte_eth_dev_close(portid);
        printf(" Done\n");
    }

    // 打印统计信息
    print_final_stats();

    // 7. 清理EAL
    rte_eal_cleanup();

    return 0;
}