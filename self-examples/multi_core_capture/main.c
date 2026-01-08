#include <arpa/inet.h>
#include <rte_log.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define RX_RING_SIZE 1024
#define NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define TX_RING_SIZE 1024

static volatile bool force_quit = false;

static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM)
        force_quit = true;
}

static void print_ether_type(uint16_t ether_type) {
    printf("EtherType: 0x%04x\n", ether_type);
}

static void print_ipv4_info(const struct rte_ipv4_hdr *ipv4) {
    char src[INET_ADDRSTRLEN] = {0};
    char dst[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &ipv4->src_addr, src, sizeof(src));
    inet_ntop(AF_INET, &ipv4->dst_addr, dst, sizeof(dst));
    printf("IPv4: %s -> %s, proto=%u, ttl=%u\n", src, dst, ipv4->next_proto_id, ipv4->time_to_live);
}

static void print_ipv6_info(const struct rte_ipv6_hdr *ipv6) {
    char src[INET6_ADDRSTRLEN] = {0};
    char dst[INET6_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET6, ipv6->src_addr.a, src, sizeof(src));
    inet_ntop(AF_INET6, ipv6->dst_addr.a, dst, sizeof(dst));
    printf("IPv6: %s -> %s, next-header=%u, hop-limit=%u\n", src, dst, ipv6->proto, ipv6->hop_limits);
}

static void print_arp_info(const struct rte_arp_hdr *arp) {
    char sip[INET_ADDRSTRLEN] = {0};
    char tip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &arp->arp_data.arp_sip, sip, sizeof(sip));
    inet_ntop(AF_INET, &arp->arp_data.arp_tip, tip, sizeof(tip));
    printf("ARP: op=%u, %s -> %s\n", rte_be_to_cpu_16(arp->arp_opcode), sip, tip);
}

static void inspect_packet(struct rte_mbuf *m) {
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    uint16_t ether_type = rte_be_to_cpu_16(eth->ether_type);

    printf("Packet len: %u, lcore: %u\n", m->pkt_len, rte_lcore_id());
    print_ether_type(ether_type);

    if (ether_type == RTE_ETHER_TYPE_IPV4) {
        const struct rte_ipv4_hdr *ipv4 =
            rte_pktmbuf_mtod_offset(m, const struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
        print_ipv4_info(ipv4);
    } else if (ether_type == RTE_ETHER_TYPE_IPV6) {
        const struct rte_ipv6_hdr *ipv6 =
            rte_pktmbuf_mtod_offset(m, const struct rte_ipv6_hdr *, sizeof(struct rte_ether_hdr));
        print_ipv6_info(ipv6);
    } else if (ether_type == RTE_ETHER_TYPE_ARP) {
        const struct rte_arp_hdr *arp =
            rte_pktmbuf_mtod_offset(m, const struct rte_arp_hdr *, sizeof(struct rte_ether_hdr));
        print_arp_info(arp);
    }

    printf("----\n");
}

struct worker_ctx {
    uint16_t queue_id;
    uint16_t nb_ports;
};

static int worker_loop(void *arg) {
    struct worker_ctx *ctx = (struct worker_ctx *)arg;
    uint16_t qid = ctx->queue_id;
    uint16_t nb_ports = ctx->nb_ports;

    while (!force_quit) {
        for (uint16_t port = 0; port < nb_ports; port++) {
            struct rte_mbuf *bufs[BURST_SIZE];
            uint16_t nb_rx = rte_eth_rx_burst(port, qid, bufs, BURST_SIZE);
            for (uint16_t i = 0; i < nb_rx; i++) {
                inspect_packet(bufs[i]);
                rte_pktmbuf_free(bufs[i]);
            }
        }
    }
    return 0;
}

static int init_port(uint16_t port_id, uint16_t nb_rx_q, uint16_t nb_tx_q, struct rte_mempool *pool) {
    struct rte_eth_conf port_conf = {
        .rxmode = {.mq_mode = RTE_ETH_MQ_RX_NONE},
        .txmode = {.mq_mode = RTE_ETH_MQ_TX_NONE},
    };
    struct rte_eth_dev_info dev_info;
    int ret = rte_eth_dev_info_get(port_id, &dev_info);
    if (ret != 0)
        return ret;

    if (dev_info.max_rx_queues < nb_rx_q) {
        printf("Port %u supports only %u rx queues, need %u\n", port_id, dev_info.max_rx_queues, nb_rx_q);
        return -1;
    }
    if (dev_info.max_tx_queues < nb_tx_q) {
        printf("Port %u supports only %u tx queues, need %u\n", port_id, dev_info.max_tx_queues, nb_tx_q);
        return -1;
    }

    ret = rte_eth_dev_configure(port_id, nb_rx_q, nb_tx_q, &port_conf);
    if (ret != 0)
        return ret;

    for (uint16_t q = 0; q < nb_rx_q; q++) {
        ret = rte_eth_rx_queue_setup(port_id, q, RX_RING_SIZE, rte_eth_dev_socket_id(port_id), NULL, pool);
        if (ret != 0)
            return ret;
    }

    struct rte_eth_txconf tx_conf = dev_info.default_txconf;
    tx_conf.offloads = port_conf.txmode.offloads;
    for (uint16_t q = 0; q < nb_tx_q; q++) {
        ret = rte_eth_tx_queue_setup(port_id, q, TX_RING_SIZE, rte_eth_dev_socket_id(port_id), &tx_conf);
        if (ret != 0)
            return ret;
    }

    ret = rte_eth_dev_start(port_id);
    if (ret != 0)
        return ret;

    ret = rte_eth_promiscuous_enable(port_id);
    if (ret != 0)
        return ret;

    struct rte_ether_addr mac;
    rte_eth_macaddr_get(port_id, &mac);
    char mac_buf[RTE_ETHER_ADDR_FMT_SIZE];
    rte_ether_format_addr(mac_buf, sizeof(mac_buf), &mac);
    printf("Port %u MAC %s initialized with %u rx queues\n", port_id, mac_buf, nb_rx_q);
    return 0;
}

int main(int argc, char **argv) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "EAL init failed\n");

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* 使用 EAL 根据 -c/-l 计算出的 lcore 数量，作为实际工作的核心数 */
    unsigned int lcore_count = rte_lcore_count();
    if (lcore_count == 0)
        rte_exit(EXIT_FAILURE, "No lcores available\n");
    unsigned int workers = lcore_count;

    uint16_t nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports\n");

    struct rte_mempool *pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports * workers,
                                                       MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
                                                       rte_socket_id());
    if (pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    uint16_t port_id;
    RTE_ETH_FOREACH_DEV(port_id) {
        ret = init_port(port_id, workers, workers, pool);
        if (ret != 0)
            rte_exit(EXIT_FAILURE, "Port %u init failed: %d\n", port_id, ret);
    }

    struct worker_ctx ctxs[workers];
    unsigned int idx = 0;

    /* assign first 'workers' lcores (including master) to queues 0..workers-1 */
    unsigned lcore_id;
    RTE_LCORE_FOREACH(lcore_id) {
        ctxs[idx].queue_id = idx;
        ctxs[idx].nb_ports = nb_ports;
        if (lcore_id == rte_get_main_lcore()) {
            // master runs directly
            idx++;
            continue;
        }
        if (idx >= workers)
            break;
        rte_eal_remote_launch(worker_loop, &ctxs[idx], lcore_id);
        idx++;
        if (idx >= workers)
            break;
    }

    // master core does work for queue 0
    worker_loop(&ctxs[0]);

    rte_eal_mp_wait_lcore();

    RTE_ETH_FOREACH_DEV(port_id) {
        rte_eth_dev_stop(port_id);
        rte_eth_dev_close(port_id);
    }
    rte_eal_cleanup();

    return 0;
}
