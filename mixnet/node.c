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

//#include <cstdint>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    mixnet_address neighbor_addr;
    mixnet_address root_addr; 
    uint16_t length_from_root;
    bool blocked;
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
        return true; // we are going update
    }
    return false;
}


void run_node(void *const handle,
              volatile bool *const keep_running,
              const struct mixnet_node_config c) {

    (void) c;
    (void) handle;
    // printf("running node: %d\n", c.node_addr);
    stp_info my_info = {c.node_addr, c.node_addr, 0};
    neighbor_state_t *neighbor_info = (neighbor_state_t*)calloc(c.num_neighbors, sizeof(neighbor_state_t));
    
    for (int i = 0; i < c.num_neighbors; i++) {
        neighbor_info[i].blocked = false;
        mixnet_packet *to_send_packet = (mixnet_packet*)malloc(18);
        to_send_packet->total_size = 18;
        to_send_packet->type = PACKET_TYPE_STP;
        mixnet_packet_stp* stp = (mixnet_packet_stp*)(to_send_packet->payload);
        stp->root_address = c.node_addr;
        stp->path_length= 0;
        stp->node_address= c.node_addr;
        mixnet_send(handle, i, to_send_packet);
    }

    while(*keep_running) {
        mixnet_packet *packet;
        uint8_t port = 0;
        // packet received
        if (mixnet_recv(handle, &port, &packet) == 1) {
            if (port == c.num_neighbors){ // source node
                // printf("user sent flood packet. sending flood out as source node\n");
                if (packet->type == 1) { // PACKET TYPE FLOOD
                    // printf("packet type is actually flood\n");
                    for (uint8_t port_n = 0; port_n <c.num_neighbors; port_n++) {
                        if (!neighbor_info[port_n].blocked) {
                            mixnet_packet *new_packet = (mixnet_packet*)malloc(packet->total_size);
                            memcpy(new_packet, packet, packet->total_size);
                            mixnet_send(handle, port_n, new_packet);
                        }
                    }
                } else { // PACKET TYPE PING OR DATA
                    // TODO: CP2 
                }
            } else { 
                // check if control packet. in cp1 not doing anything else tho
                if (packet->type == PACKET_TYPE_STP) {
                    mixnet_packet_stp* payload = (mixnet_packet_stp*)(packet->payload);
                    neighbor_info[port].neighbor_addr = payload->node_address;
                    bool to_update = node_compare(payload, my_info);

                    if (to_update) {
                        mixnet_address old_next_hop = my_info.next_hop;
                        my_info.root_addr = payload->root_address;
                        my_info.next_hop = payload->node_address;
                        my_info.path_len = payload->path_length + 1;

                        if (old_next_hop != c.node_addr) {
                            for (int i = 0; i < c.num_neighbors; i++) {
                                if (neighbor_info[i].neighbor_addr == old_next_hop) {
                                    neighbor_info[i].blocked = true;
                                }
                            }
                        }

                        for (int i = 0; i < c.num_neighbors; i++) {
                            mixnet_packet *to_send_packet = (mixnet_packet*)malloc(sizeof(mixnet_packet) + sizeof(mixnet_packet_stp));
                            to_send_packet->total_size = sizeof(mixnet_packet) + sizeof(mixnet_packet_stp);
                            to_send_packet->type = PACKET_TYPE_STP;
                            
                            mixnet_packet_stp* stp_payload = (mixnet_packet_stp*)(to_send_packet->payload);
                            stp_payload->root_address = my_info.root_addr;
                            stp_payload->path_length = my_info.path_len;
                            stp_payload->node_address = c.node_addr;
                            
                            mixnet_send(handle, i, to_send_packet);
                        }
                    }

                    bool should_unblock = false;
                    
                    if (my_info.root_addr == payload->root_address) {
                        if (payload->path_length + 1 == my_info.path_len && 
                            payload->node_address == my_info.next_hop) {
                            should_unblock = true;
                        } else if (payload->path_length == my_info.path_len + 1) {
                            should_unblock = true;
                        }
                    }
                    
                    neighbor_info[port].blocked = !should_unblock;
                } else if (packet->type == PACKET_TYPE_LSA) { // PACKET TYPE LSA
                    // TODO: CP2
                } else if (packet->type == PACKET_TYPE_FLOOD) {
                        for (uint8_t port_n = 0; port_n <= c.num_neighbors; port_n++) {
                            if (port_n < c.num_neighbors && !neighbor_info[port_n].blocked && port_n != port) {
                                // Forward to unblocked neighbors
                                mixnet_packet *new_packet = (mixnet_packet*)malloc(packet->total_size);
                                memcpy(new_packet, packet, packet->total_size);
                                mixnet_send(handle, port_n, new_packet);
                            } else if (port_n == c.num_neighbors) {
                                // Forward to user
                                mixnet_packet *new_packet = (mixnet_packet*)malloc(packet->total_size);
                                memcpy(new_packet, packet, packet->total_size);
                                mixnet_send(handle, port_n, new_packet);
                            }
                        }
                } else {
                    // TODO: CP2 
                }
            }
            free(packet);
        }
    }
    //printf("Node %d thinks %d is root\n", c.node_addr, my_info.root_addr);
    free(neighbor_info); // TODO: when to free??
}