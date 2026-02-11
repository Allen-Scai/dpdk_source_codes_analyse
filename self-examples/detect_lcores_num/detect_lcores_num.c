// detect_lcores.c
#include <arpa/inet.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        return -1;

    printf("Detected lcores: %u\n", rte_lcore_count());
    printf("Main lcore: %u\n", rte_get_main_lcore());

    unsigned int lcore_id;
    printf("Available lcores: ");
    RTE_LCORE_FOREACH(lcore_id)
    {
        printf("%u ", lcore_id);
    }
    printf("\n");

    rte_eal_cleanup();
    return 0;
}