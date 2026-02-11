#include "hash_usage.h"
#include <arpa/inet.h>
#include <error.h>
#include <netinet/in.h>
#include <rte_eal.h>
#include <rte_hash.h>
#include <rte_ip4.h>
#include <rte_jhash.h>
#include <rte_log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

static void fill_flow_key_content(flow_key_t *key, char *srcip, char *dstip, uint16_t src_port, uint16_t dst_port, uint8_t prototol)
{
    if (!key) {
        return;
    }

    uint32_t src_ip = ntohl(inet_addr(srcip));
    uint32_t dst_ip = ntohl(inet_addr(dstip));

    key->src_ip = src_ip;
    key->dst_ip = dst_ip;
    key->src_port = src_port;
    key->dst_port = dst_port;
    key->protocol = prototol;
}

/* parameters
 * for rte
 * hash
 * table
 */
struct rte_hash_parameters flow_hash_tbl_params = {
    .name = "flow_table",          // name of the hash table
    .entries = 64,                 // number of entries in the hash table
    .key_len = sizeof(flow_key_t), // length of the key
    .hash_func = rte_jhash,        // hash function
    .hash_func_init_val = 0,       // initial value for the hash function
    .socket_id = 0,                // socket id
};

#define TCP_PROTOCOL_NUMBER 6

int main(int argc, char **argv)
{
    int ret = 0;
    struct rte_hash *flow_hash_table = NULL;
    flow_key_t flow_key = {0};
    char srcip[64] = {0};
    char dstip[64] = {0};
    int32_t hash_pos = -1;

    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Fail to initiate EAL parameters\n");
        goto clean_eal;
    }

    flow_hash_table = rte_hash_create(&flow_hash_tbl_params);
    if (flow_hash_table == NULL) {
        goto clean_eal;
    }

    for (;;) {
        printf("Please input srcip:\n");
        scanf("%s", srcip);
        printf("Please input dstip:\n");
        scanf("%s", dstip);

        if (!strcmp(srcip, "0") && !strcmp(dstip, "0")) {
            printf("Finish adding hash entries\n");
            break;
        }
        fill_flow_key_content(&flow_key, srcip, dstip, 999, 666, TCP_PROTOCOL_NUMBER);

        hash_pos = rte_hash_lookup(flow_hash_table, &flow_key);
        if (hash_pos < 0) {
            printf("Flow key not found in hash table, adding it now.\n");
        } else {
            printf("Flow key already exists in hash table at position %d, delete it first\n", hash_pos);
            hash_pos = rte_hash_del_key(flow_hash_table, &flow_key);
            if (hash_pos == ENOENT) {
                printf("Try to delete entry, but found no entry\n");
            } else if (hash_pos >= 0) {
                printf("Deleted flow key from hash table at position %d\n", hash_pos);
            }
        }

        hash_pos = rte_hash_add_key(flow_hash_table, &flow_key);
        if (hash_pos < 0) {
            printf("Failed to add flow key to hash table\n");
            ret = -1;
            goto clean_eal;
        } else {
            printf("Flow key added to hash table at position %d\n", hash_pos);
        }

        memset(srcip, 0, sizeof(srcip));
        memset(dstip, 0, sizeof(dstip));
    }

    memset(srcip, 0, sizeof(srcip));
    memset(dstip, 0, sizeof(dstip));
    for (;;) {
        printf("Please input srcip which need to lookup:\n");
        scanf("%s", srcip);
        printf("Please input dstip which need to lookup:\n");
        scanf("%s", dstip);

        if (!strcmp(srcip, "0") && !strcmp(dstip, "0")) {
            printf("Finish adding hash entries\n");
            break;
        }
        fill_flow_key_content(&flow_key, srcip, dstip, 999, 666, TCP_PROTOCOL_NUMBER);

        hash_pos = rte_hash_lookup(flow_hash_table, &flow_key);
        if (hash_pos < 0) {
            printf("Flow key not found in hash table\n");
        } else {
            printf("Flow key was be found in flow hash table, pos %d\n", hash_pos);
        }

        memset(srcip, 0, sizeof(srcip));
        memset(dstip, 0, sizeof(dstip));
    }

clean_eal:
    if (flow_hash_table) {
        rte_hash_free(flow_hash_table);
        printf("Free flow hash table successfully\n");
    }
    ret = rte_eal_cleanup();
    RTE_LOG(INFO, EAL, "EAL cleanup completed\n");

    return ret;
}