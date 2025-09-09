/**
 * Copyright (C) 2023 Carnegie Mellon University
 *
 * This file is part of the Mixnet course project developed for
 * the Computer Networks course (15-441/641) taught at Carnegie
 * Mellon University.
 *
 * No part of the Mixnet project may be copied and/or distributed
 * without the express permission of the 15-441/641 course staff.
 */
#include "node.h"

#include "address.h"
#include "connection.h"
#include "packet.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    mixnet_address root; 
    uint16_t cost;
} stp_edge_t;

typedef struct {
    bool           have_addr;
    mixnet_address neighbor_addr;
    stp_edge_t     root_cost;
} stp_state_t;

int node_compare(mixnet_address curr_root, mixnet_address from_root,)


// need to do dijkstras for cp2

void run_node(void *const handle,
              volatile bool *const keep_running,
              const struct mixnet_node_config c) {

    (void) c;
    (void) handle;
    while(*keep_running) {
        
    }
}
