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

#include <cstdlib>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    mixnet_address neighbor_addr;
    mixnet_address root_addr; 
    uint16_t length_from_root;
    bool blocked;
} neighbor_state_t;

typedef struct {
    mixnet_address root_addr; 
    mixnet_address next_hop;
    uint16_t path_len;
} stp_info;

// typedef struct {
//     mixnet_address neighbor_addr;
//     uint16_t cost;
//     //addr_cost* next;
// } addr_cost;

typedef struct global_view {
    mixnet_address node_addr;
    mixnet_lsa_link_params* edge_list;
    uint16_t edge_count;
    struct global_view* next;
} global_view;

typedef struct {
    mixnet_address node_addr;
    uint16_t distance;
} distance_vector;

typedef struct pq_entry {
    int distance;
    mixnet_address source;
    struct pq_entry* next;
} pq_entry;

bool pq_empty(pq_entry *pq) {
    return (pq == NULL);
}

void pq_push(pq_entry *pq, pq_entry *to_add) {
    to_add->next = pq;
    pq = to_add;
}

pq_entry* pq_pop(pq_entry *pq) {
    pq_entry *current = pq;
    pq_entry *curr_min = pq;
    while (current != NULL) {
        if (current->distance < curr_min->distance) {
            curr_min = current;
        }
        current = current->next;
    }
    pq_entry *pre_remove = pq;
    while (pre_remove != curr_min) {
        pre_remove = pre_remove->next;
    }
    pre_remove->next = curr_min->next;
    return curr_min;
}

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

int forward_stp(void *const handle, uint8_t port_n, mixnet_packet *packet) {
    mixnet_packet *new_packet = (mixnet_packet*)malloc(packet->total_size);
    memcpy(new_packet, packet, packet->total_size);
    return mixnet_send(handle, port_n, new_packet);
}

int send_stp(void *const handle, const struct mixnet_node_config c, stp_info my_info){
    int fail;
    for (int i = 0; i < c.num_neighbors; i++) {
        mixnet_packet *to_send_packet = (mixnet_packet*)malloc(sizeof(mixnet_packet) + sizeof(mixnet_packet_stp));
        to_send_packet->total_size = sizeof(mixnet_packet) + sizeof(mixnet_packet_stp);
        to_send_packet->type = PACKET_TYPE_STP;
        
        mixnet_packet_stp* stp_payload = (mixnet_packet_stp*)(to_send_packet->payload);
        stp_payload->root_address = my_info.root_addr;
        stp_payload->path_length = my_info.path_len;
        stp_payload->node_address = c.node_addr;
        
        fail = mixnet_send(handle, i, to_send_packet);
        if (fail == -1) {
            return -1;
        }
    }
    return 1;
}

// int send_lsa(void *const handle, const struct mixnet_node_config c, stp_info my_info){

// }

static uint64_t time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

global_view* find_in_global_view_list(global_view *p, mixnet_address to_find_addr) {
   global_view *current = p;
   while (current != NULL) {
       if (current->node_addr == to_find_addr) {
           return current;
       }
       current = current->next;
   }
   return NULL;
}


void add_to_global_view(global_view **p,
                       mixnet_address node_addr,
                       mixnet_lsa_link_params *edges,
                       uint16_t count) {
   if (p == NULL) return;

   global_view *existing = find_in_global_view_list(*p, node_addr);
   if (existing != NULL) {
       if (existing->edge_list != NULL) {
        free(existing->edge_list);
       }
       existing->edge_list = edges;
       existing->edge_count = count;
       return;
   }


   global_view *new_node = malloc(sizeof(global_view));
   if (!new_node) {
       free(edges);
       return;
   }
   new_node->node_addr = node_addr;
   new_node->edge_list = edges;
   new_node->edge_count = count;
   new_node->next = *p;
   *p = new_node;
}

void compute_shortest_paths(global_view *p, mixnet_address src_addr) {
    global_view *current = p;
    int count = 0;
    while (current != NULL) {
       count++;
       current = current->next;
    }
    pq_entry *pq =  malloc(sizeof(pq_entry));
    pq->distance = 0;
    pq->source = src_addr;
    pq->next = NULL;
    
} 

void run_node(void *const handle,
              volatile bool *const keep_running,
              const struct mixnet_node_config c) {

    (void) c;
    (void) handle;
    int reelection_interval = c.reelection_interval_ms;
    int hello_interval = c.root_hello_interval_ms;
    uint16_t cost = c.link_costs[4];
    printf("cost: %d\n", cost);
    
    mixnet_lsa_link_params *neighbhor_costs = malloc(sizeof(mixnet_lsa_link_params)*(c.num_neighbors-1));
    for (int i = 0; i < c.num_neighbors; i++) {
        neighbhor_costs[i].cost = c.link_costs[i];
    }
    global_view *adj_list_start = malloc(sizeof(global_view)); //will start with pointer to our own list
    adj_list_start->node_addr = c.node_addr;
    adj_list_start->edge_list = neighbhor_costs;
    adj_list_start->edge_count = c.num_neighbors;
    adj_list_start->next = NULL;
    
    // Timer variables
    uint64_t last_hello_time = time_now();
    uint64_t start_time = time_now();
    uint64_t last_root_message_time = time_now();
    bool lsa_done = false;
    
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
        uint64_t current_time = time_now();
        
        // Check if we need to send hello messages (only if we are the root)
        if (my_info.root_addr == c.node_addr && 
            current_time - last_hello_time >= (uint64_t)hello_interval) {
                send_stp(handle, c, my_info);
                last_hello_time = current_time;
        }
        
        // Check for reelection timeout (only if we are NOT the root)
        if (my_info.root_addr != c.node_addr && 
            current_time - last_root_message_time >= (uint64_t)reelection_interval) {
            
            // Start reelection - assume we are the new root
            mixnet_address old_next_hop = my_info.next_hop;
            my_info.root_addr = c.node_addr;
            my_info.next_hop = c.node_addr;
            my_info.path_len = 0;
            
            ///Block the old path to root
            if (old_next_hop != c.node_addr) {
                for (int i = 0; i < c.num_neighbors; i++) {
                    if (neighbor_info[i].neighbor_addr == old_next_hop) {
                        neighbor_info[i].blocked = true;
                    }
                }
            }
            
            // Broadcast our new root claim
            send_stp(handle, c, my_info);           
            last_root_message_time = current_time;
        }

        //broadcast LSA?
        if (current_time - start_time >= 500 && !lsa_done) { // TODO: possibly change time interview or ask in OH if better time to send out periodic lsa's
            for (uint8_t port_n = 0; port_n < c.num_neighbors; port_n++) {
                if (!neighbor_info[port_n].blocked) {
                    //send LSA packet
                    int packet_size = 12 + (4 + (4 * c.num_neighbors));
                    mixnet_packet *to_send_packet = (mixnet_packet*)malloc(packet_size);
                    to_send_packet->total_size = packet_size;
                    to_send_packet->type = PACKET_TYPE_LSA;
                    
                    mixnet_packet_lsa* lsa_payload = (mixnet_packet_lsa*)(to_send_packet->payload);
                    lsa_payload->node_address = c.node_addr;
                    lsa_payload->neighbor_count = c.num_neighbors;
                    for (uint16_t i = 0; i < adj_list_start->edge_count; i++) {
                        lsa_payload->links[i].neighbor_mixaddr = adj_list_start->edge_list[i].neighbor_mixaddr;
                        lsa_payload->links[i].cost = adj_list_start->edge_list[i].cost;
                    }
                    
                    mixnet_send(handle, port_n, to_send_packet);
                } 
            }
            lsa_done = true;
        }

        mixnet_packet *packet;
        uint8_t port = 0;
        // packet received
        if (mixnet_recv(handle, &port, &packet) == 1) {
            if (port == c.num_neighbors){ // source node
                //printf("user sent flood packet. sending flood out as source node\n");
                if (packet->type == 1) { // PACKET TYPE FLOOD
                    // printf("packet type is actually flood\n");
                    for (uint8_t port_n = 0; port_n <c.num_neighbors; port_n++) {
                        if (!neighbor_info[port_n].blocked) {
                            forward_stp(handle, port_n, packet);
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
                    neighbhor_costs[port].neighbor_mixaddr = payload->node_address;
                    //printf("received stp packet from %d claiming %d is the root with path len %d\n", payload->node_address, payload->root_address, payload->path_length);
                    
                    // Update the time we last received a message from root path
                    if (my_info.root_addr != c.node_addr && 
                        payload->root_address == my_info.root_addr &&
                        payload->node_address == my_info.next_hop) {
                        last_root_message_time = current_time;
                    }
                    
                    bool to_update = node_compare(payload, my_info);

                    if ((payload->node_address == my_info.next_hop) &&
                        (payload->root_address == my_info.root_addr && 
                        payload->path_length + 1 == my_info.path_len)) {
                        //send on all ports!!!
                        send_stp(handle, c, my_info);
                        //reset reelection timer
                        last_root_message_time = current_time;
                    }

                    if (to_update) {
                        //printf("updating root\n");
                        mixnet_address old_next_hop = my_info.next_hop;
                        my_info.root_addr = payload->root_address;
                        my_info.next_hop = payload->node_address;
                        my_info.path_len = payload->path_length + 1;
                        
                        // Reset timer since we have a new root path
                        last_root_message_time = current_time;

                        if (old_next_hop != c.node_addr) {
                            for (int i = 0; i < c.num_neighbors; i++) {
                                if (neighbor_info[i].neighbor_addr == old_next_hop) {
                                    neighbor_info[i].blocked = true;
                                }
                            }
                        }

                        send_stp(handle, c, my_info);
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
                    // printf("root: %d, next hop: %d, path len: %d\n", my_info.root_addr, my_info.next_hop, my_info.path_len);
                    // for (int i = 0; i < c.num_neighbors; i++) {
                    //     printf("link to %d blocked: %s\n", neighbor_info[i].neighbor_addr, neighbor_info[i].blocked ? "true" : "false");
                    // }
                } else if (packet->type == PACKET_TYPE_LSA) { // PACKET TYPE LSA
                    //if link not blocked 
                        //send lsa packets to other unblocked links besides the one that we just heard from
                    if (!neighbor_info[port].blocked) {
                        //first update our global view info
                        mixnet_packet_lsa* payload = (mixnet_packet_lsa*)(packet->payload);
                        add_to_global_view(&adj_list_start, payload->node_address, payload->links, payload->neighbor_count);
                        // run dijkstras
                        //then send our own LSA packet
                        for (uint8_t port_n = 0; port_n < c.num_neighbors; port_n++) {
                            if (!neighbor_info[port_n].blocked && port_n != port) {
                                //send LSA packet
                                int packet_size = 12 + (4 + (4 * c.num_neighbors));
                                mixnet_packet *to_send_packet = (mixnet_packet*)malloc(packet_size);
                                to_send_packet->total_size = packet_size;
                                to_send_packet->type = PACKET_TYPE_LSA;
                                
                                mixnet_packet_lsa* lsa_payload = (mixnet_packet_lsa*)(to_send_packet->payload);
                                lsa_payload->node_address = c.node_addr;
                                lsa_payload->neighbor_count = c.num_neighbors;
                                for (uint16_t i = 0; i < adj_list_start->edge_count; i++) {
                                    lsa_payload->links[i].neighbor_mixaddr = adj_list_start->edge_list[i].neighbor_mixaddr;
                                    lsa_payload->links[i].cost = adj_list_start->edge_list[i].cost;
                                }
                                
                                mixnet_send(handle, port_n, to_send_packet);
                            } 
                        }
                    }
                    
                } else if (packet->type == PACKET_TYPE_FLOOD) {
                    if (!neighbor_info[port].blocked) {
                        for (uint8_t port_n = 0; port_n <= c.num_neighbors; port_n++) {
                            if (port_n < c.num_neighbors && !neighbor_info[port_n].blocked && port_n != port) {
                                // Forward to unblocked neighbors
                                forward_stp(handle, port_n, packet);
                            } else if (port_n == c.num_neighbors) {
                                // Forward to user
                                forward_stp(handle, port_n, packet);
                            }
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