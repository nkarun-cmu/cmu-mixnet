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
#include "config.h"
#include "connection.h"
#include "packet.h"

#include <cstdint>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    mixnet_address neighbor_addr;
    mixnet_address root_addr; 
    uint16_t length_from_root;
    // cost field 
} neighbor_state_t;

typedef struct {
    mixnet_address root_addr; 
    mixnet_address next_hop;
    uint16_t path_len;
} stp_info;

bool node_compare(mixnet_packet_stp* payload, stp_info info) {
                    mixnet_address update_root = payload->root_address;
                    mixnet_address update_node_addr = payload->node_address;
                    uint16_t update_path_len = payload->path_length;
                    mixnet_address state_root = info.root_addr;
                    mixnet_address state_next_hop = info.next_hop;
                    uint16_t state_path_len = info.path_len;
                    bool cond1 = update_root < state_root;
                    bool cond2 = update_root == state_root && update_path_len + 1 < state_path_len;
                    bool cond3 = update_root == state_root && update_path_len + 1 == state_path_len && update_node_addr < state_next_hop;
                    if (cond1 || cond2 || cond3) {
                        return true; // UPDATE!!!
                    }
                    return false;
                 }


// need to do dijkstras for cp2

void run_node(void *const handle,
              volatile bool *const keep_running,
              const struct mixnet_node_config c) {

    (void) c;
    (void) handle;

    stp_info info = {c.node_addr, c.node_addr, 0};
    neighbor_state_t *neighbor_info = (neighbor_state_t*)calloc(c.num_neighbors, sizeof(neighbor_state_t));
    
    while(*keep_running) {
        mixnet_packet *packet;
        uint8_t port = 0;
        // packet received
        if (mixnet_recv(handle, &port, &packet) == 1) {
            if (port == c.num_neighbors){ // source node
                if (packet->type == 1) { // PACKET TYPE FLOOD
                    // TODO: CP1
                    // broadcast along spanning tree
                    // send to user
                } else { // PACKET TYPE PING OR DATA
                    // TODO: CP2 
                }
            } else { 
                // check if control packet. in cp1 not doing anything else tho
                if (packet->type == 0 || packet->type == 2) {
                    if (packet->type == 0) { // PACKET TYPE STP
                        mixnet_packet_stp* payload = (mixnet_packet_stp*)(packet->payload);
                        // TODO: CP1
                        // neighbor discovery
                        neighbor_info[port].neighbor_addr = payload->node_address;
                        neighbor_info[port].length_from_root = payload->path_length;
                        neighbor_info[port].root_addr = payload->root_address;

                        // Deciding whether to take update (slide 8 reci)
                        bool to_update = node_compare(payload, info);

                        if (to_update) {
                            
                        }
                    } else { // PACKET TYPE LSA
                        // TODO: CP2 8
                    }
                } else {
                    if (packet->type == 1) { // PACKET TYPE FLOOD
                        // TODO: CP1
                    } else {
                        // TODO: CP2 
                    }
                }
            }
        } else { // no packet received 

        }
    }
}
