#include "rte_common.h"
#include <rte_ip4.h>
#include <stdint.h>
#include <sys/types.h>

struct flow_key_ {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;

}__rte_packed;

typedef struct flow_key_ flow_key_t;