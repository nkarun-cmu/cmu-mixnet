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


//#include <cstdlib>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>


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


typedef struct path_component {
    mixnet_address node;
    struct path_component* next;
} path_component;


typedef struct global_view {
    mixnet_address node_addr;
    mixnet_lsa_link_params* edge_list;
    uint16_t edge_count;
    struct global_view* next;
    uint16_t distance;
    path_component* path;
    uint16_t path_size;
    mixnet_address first_hop_addr;
} global_view;


typedef struct pq_entry {
    int distance;
    mixnet_address node;
    struct pq_entry* next;
} pq_entry;


typedef struct {
    uint16_t port;
    mixnet_packet *packet;
} mixing_packet_held;


bool pq_empty(pq_entry *pq) {
    return (pq == NULL);
}


void pq_push(pq_entry *pq, int distance, mixnet_address node) {
    pq_entry *to_add = malloc(sizeof(pq_entry));
    if (to_add == NULL) {
        printf("malloc 1 failed");
        return;
    }
    to_add->distance = distance;
    to_add->node = node;
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


path_component* add_to_path(const path_component *path_before, mixnet_address node_addr) {
    if (path_before == NULL) {
        path_component *new_path = malloc(sizeof(path_component));
        if (new_path == NULL) {
            printf("malloc 2 failed\n");
            return NULL;
        }
        new_path->node = node_addr;
        new_path->next = NULL;
        return new_path;
   }
  
   path_component *new_path = malloc(sizeof(path_component));
   if (new_path == NULL) {
        printf("malloc 3 failed\n");
        return NULL;
    }
    memcpy(new_path, path_before, sizeof(path_component));
    path_component *current_new = new_path;
    const path_component *current_old = path_before->next;

    while (current_old != NULL) {
        current_new->next = malloc(sizeof(path_component));
        if (current_new->next == NULL) {
            printf("malloc 4 failed\n");
            return NULL;
        }
        current_new = current_new->next;
        memcpy(current_new, current_old, sizeof(path_component));
        current_old = current_old->next;
    }
  
    path_component *final_node = malloc(sizeof(path_component));
    if (final_node == NULL) {
        printf("malloc 5 failed\n");
        return NULL;
    }
    final_node->node = node_addr;
    final_node->next = NULL;
    current_new->next = final_node;

    return new_path;
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


int forward_packet(void *const handle, uint8_t port_n, mixnet_packet *packet) {
    printf("forwarding packet\n");
    mixnet_packet *new_packet = (mixnet_packet*)malloc(packet->total_size);
    if (new_packet == NULL) {
        printf("malloc 6 failed\n");
        return 0;
    }
    memcpy(new_packet, packet, packet->total_size);
    printf("forwarded packet\n");
    return mixnet_send(handle, port_n, new_packet);
}


int send_stp(void *const handle, const struct mixnet_node_config c, stp_info my_info){
    printf("%d sending stp packet\n", c.node_addr);
    int fail;
    for (int i = 0; i < c.num_neighbors; i++) {
        mixnet_packet *to_send_packet = (mixnet_packet*)malloc(sizeof(mixnet_packet) + sizeof(mixnet_packet_stp));
        if (to_send_packet == NULL) {
            printf("malloc 7 failed\n");
            return 0;
        }
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
    printf("%d stp packet sent\n", c.node_addr);
    return 1;
}


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
            // free(existing->edge_list);
        }
        existing->edge_list = edges;
        existing->edge_count = count;
        return;
    }

    global_view *new_node = malloc(sizeof(global_view));
    if (!new_node) {
    //    free(edges);
        return;
    }
    new_node->node_addr = node_addr;
    new_node->edge_list = edges;
    new_node->distance = (uint16_t)INT_MAX;
    new_node->edge_count = count;
    new_node->next = *p;
    *p = new_node;
}

void compute_shortest_paths(global_view *p, mixnet_address src_addr) {
    printf("%d computing shortest paths\n", src_addr);
    global_view *current = p;
    while (current != NULL) {
        current->distance = (uint16_t)INT_MAX;
        current->path = NULL;
        current->path_size = 0;
        current->first_hop_addr = INVALID_MIXADDR;
        current = current->next;
    }


    global_view *source = find_in_global_view_list(p, src_addr);
    if (source == NULL) {
        return; // Something very wrong
    }
    source->distance = 0;


    pq_entry *pq =  malloc(sizeof(pq_entry));
    if (pq == NULL) {
        printf("malloc 9 failed\n");
        return;
    }
    pq->distance = 0;
    pq->node = src_addr;
    pq->next = NULL;


    while (!pq_empty(pq)) {
        pq_entry *pq_min = pq_pop(pq);
        global_view *pq_min_node = find_in_global_view_list(p, pq_min->node);       
        if (pq_min_node->distance == (uint16_t)INT_MAX) continue;


        mixnet_lsa_link_params* edge_list = pq_min_node->edge_list;
        uint16_t u_dist = pq_min_node->distance;
        path_component *u_path = pq_min_node->path;


        for (int i = 0; i < pq_min_node->edge_count; i++) { // TODO: maybe do edgecount-1
            mixnet_address v_addr = edge_list[i].neighbor_mixaddr;
            global_view *v_node = find_in_global_view_list(p, v_addr);
            uint16_t weight = edge_list[i].cost;
            
            uint16_t new_dist = u_dist + weight;
            uint16_t old_dist = v_node->distance;

            bool should_update = false;

            if (new_dist < old_dist) {
                should_update = true;
            } else if (new_dist == old_dist) {
                mixnet_address new_first_hop;
                if (pq_min_node->node_addr == src_addr) {
                    new_first_hop = v_addr;
                } else {
                    new_first_hop = pq_min_node->first_hop_addr;
                }
                if (new_first_hop < v_node->first_hop_addr) {
                    should_update = true;
                }
            }


            if (should_update) {
                v_node->distance = new_dist;
                v_node->path = add_to_path(u_path, v_node->node_addr);
                v_node->path_size = pq_min_node->path_size + 1;
                if (pq_min_node->node_addr == src_addr) {
                    v_node->first_hop_addr = v_addr;
                } else {
                    v_node->first_hop_addr = pq_min_node->first_hop_addr;
                }
                
                pq_push(pq, v_node->distance, v_addr);
            }
        }
    //    free(pq_min);
    }
    printf("%d shortest paths computed\n", src_addr);
}

// mixnet_packet* create_forwarding_packet(
//     const mixnet_packet* src_packet,
//     const path_component* path,
//     uint16_t path_len)
// {
//     size_t new_rh_size = sizeof(mixnet_packet_routing_header) + (path_len * sizeof(mixnet_address));
//     size_t old_data_size = src_packet->total_size - sizeof(mixnet_packet) - sizeof(mixnet_packet_routing_header);
//     size_t new_total_size = sizeof(mixnet_packet) + new_rh_size + old_data_size;

//     mixnet_packet* new_packet = (mixnet_packet*)malloc(new_total_size);

//     memcpy(new_packet, src_packet, sizeof(mixnet_packet));
//     new_packet->total_size = new_total_size;
//     mixnet_packet_routing_header* old_rh = (mixnet_packet_routing_header*)(src_packet->payload);
//     mixnet_packet_routing_header* new_rh = (mixnet_packet_routing_header*)(new_packet->payload);
    
//     const char* old_data = (const char*)old_rh + sizeof(mixnet_packet_routing_header);
//     char* new_data = (char*)new_rh + new_rh_size;

//     new_rh->src_address = old_rh->src_address;
//     new_rh->dst_address = old_rh->dst_address;
//     new_rh->route_length = path_len;
//     new_rh->hop_index = 0;

//     path_component* current_hop = (path_component*)path;
//     for (int i = 0; i < path_len; i++) {
//         if (current_hop == NULL) break;
//         new_rh->route[i] = current_hop->node;
//         current_hop = current_hop->next;
//     }
//     if (old_data_size > 0) {
//         memcpy(new_data, old_data, old_data_size);
//     }    
//     return new_packet;
// }

void run_node(void *const handle,
             volatile bool *const keep_running,
             const struct mixnet_node_config c) {
    printf("in run_node\n");

    (void) c;
    (void) handle;
    int reelection_interval = c.reelection_interval_ms;
    int hello_interval = c.root_hello_interval_ms;


    // mixing stuff
    uint16_t packet_counter = 0;
    printf("defining mix packet store\n");
    mixing_packet_held *mix_packets = malloc(sizeof(mixing_packet_held)*c.mixing_factor);
    if (mix_packets == NULL) {
        printf("malloc 10 failed\n");
        return;
    }
    printf("mix packet store defined\n");

    mixnet_lsa_link_params *neighbhor_costs = malloc(sizeof(mixnet_lsa_link_params)*(c.num_neighbors));
    if (neighbhor_costs == NULL) {
        printf("malloc 11 failed\n");
        return;
    }

    for (int i = 0; i < c.num_neighbors; i++) {
        neighbhor_costs[i].cost = c.link_costs[i];
    }
    printf("starting global view\n");
    global_view *adj_list_start = malloc(sizeof(global_view)); //will start with pointer to our own list
    if (adj_list_start == NULL) {
        printf("malloc 12 failed\n");
        return;
    }
    adj_list_start->node_addr = c.node_addr;
    adj_list_start->edge_list = neighbhor_costs;
    adj_list_start->edge_count = c.num_neighbors;
    adj_list_start->next = NULL;
    printf("global view started\n");

    // Timer variables
    uint64_t last_hello_time = time_now();
    uint64_t start_time = time_now();
    uint64_t last_root_message_time = time_now();
    bool lsa_done = false;

    printf("running node: %d\n", c.node_addr);
    printf("num neigbors: %d\n", c.num_neighbors);
    stp_info my_info = {c.node_addr, c.node_addr, 0};
    neighbor_state_t *neighbor_info = (neighbor_state_t*)calloc(c.num_neighbors, sizeof(neighbor_state_t));


    for (int i = 0; i < c.num_neighbors; i++) {
        neighbor_info[i].blocked = false;
        printf("sending stp packet\n");
        mixnet_packet *to_send_packet = (mixnet_packet*)malloc(18);
        if (to_send_packet == NULL) {
            printf("malloc 13 failed\n");
            return;
        }
        to_send_packet->total_size = 18;
        to_send_packet->type = PACKET_TYPE_STP;
        mixnet_packet_stp* stp = (mixnet_packet_stp*)(to_send_packet->payload);
        stp->root_address = c.node_addr;
        stp->path_length= 0;
        stp->node_address= c.node_addr;
        mixnet_send(handle, i, to_send_packet);
        printf("stp packet sent\n");
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
            
            // Block the old path to root
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

        if (!*keep_running) {
            return;
        }

        //printf("before lsa broadcast\n");
        if (current_time - start_time >= 500 && !lsa_done) { // TODO: possibly keep broadcasting
        printf("%d start lsa broadcast\n", c.node_addr);
            for (uint8_t port_n = 0; port_n < c.num_neighbors; port_n++) {
                if (!neighbor_info[port_n].blocked) {
                    //send LSA packet
                    //mixnet_packet *to_send_packet = (mixnet_packet*)malloc(sizeof(mixnet_packet) + sizeof(mixnet_packet_stp));
                    //to_send_packet->total_size = sizeof(mixnet_packet) + sizeof(mixnet_packet_stp);
                    //mixnet_packet_stp* stp_payload = (mixnet_packet_stp*)(to_send_packet->payload);
                    printf("mallocing lsa\n");
                    int packet_size = sizeof(mixnet_packet) + (4 + (4 * c.num_neighbors));
                    mixnet_packet *to_send_packet = (mixnet_packet*)malloc(packet_size);
                    if (to_send_packet == NULL) {
                        printf("malloc 14 failed\n");
                        return;
                    }
                    to_send_packet->total_size = packet_size;
                    to_send_packet->type = PACKET_TYPE_LSA;
                    
                    mixnet_packet_lsa* lsa_payload = (mixnet_packet_lsa*)(to_send_packet->payload);
                    lsa_payload->node_address = c.node_addr;
                    lsa_payload->neighbor_count = c.num_neighbors;
                    for (uint16_t i = 0; i < c.num_neighbors; i++) {
                        lsa_payload->links[i].neighbor_mixaddr = adj_list_start->edge_list[i].neighbor_mixaddr;
                        lsa_payload->links[i].cost = adj_list_start->edge_list[i].cost;
                    }
                    
                    mixnet_send(handle, port_n, to_send_packet);
                }
            }
            lsa_done = true;
            printf("%d finish lsa broadcast\n", c.node_addr);
        }
        //printf("after lsa broadcast\n");

        if (!*keep_running) {
            return;
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
                            forward_packet(handle, port_n, packet);
                        }
                    }
                    if (!*keep_running) {
                        return;
                    }
                } else { // PACKET TYPE PING OR DATA
                    packet_counter++;
                    uint16_t forward_to;
                    mixnet_packet *new_packet = NULL;
                    printf("%d before first data type work\n", c.node_addr);
                    if (packet->type == PACKET_TYPE_DATA) {
                        mixnet_packet_routing_header* payload = (mixnet_packet_routing_header*)(packet->payload);
                        new_packet = (mixnet_packet*)malloc(packet->total_size);
                        if (new_packet == NULL) {
                            printf("malloc 16 failed\n");
                            return;
                        }
                        memcpy(new_packet, packet, packet->total_size);
                        mixnet_packet_routing_header* new_payload = (mixnet_packet_routing_header*)(new_packet->payload);
                        // TODO: when random, add if check for c.do_random_routing
                        global_view *destination_node = find_in_global_view_list(adj_list_start, payload->dst_address);                      
                        //new_packet = create_forwarding_packet(packet, destination_node->path, destination_node->path_size);            
                        mixnet_address first_hop_addr = destination_node->path->node;
                        for (int i = 0; i < c.num_neighbors; i++) {
                            if (neighbor_info[i].neighbor_addr == first_hop_addr) {
                                forward_to = i;
                                break;
                            }
                        }

                        forward_to = -1;
                        for (int i = 0; i < c.num_neighbors; i++) {
                            if (neighbor_info[i].neighbor_addr == new_payload->route[new_payload->hop_index]) { //hop_index should be 0
                                forward_to = i;
                                break;
                            }
                        }
                        // to send: source addr, dest addr, route length -- depends on routing type, hop idx, route -- depends on routing type, data
                    }
                    printf("%d after first data type work\n", c.node_addr);
                //    else { // packet type PING
                //        // TODO
                //    }
                    
                    printf("%d before mixing 1\n", c.node_addr);
                    if (new_packet != NULL) {
                        if (packet_counter < c.mixing_factor) {
                                mix_packets[packet_counter-1].port = forward_to;
                                mix_packets[packet_counter-1].packet = new_packet;
                        } else {
                            for (int i = 0; i < c.mixing_factor; i++) {
                                mixnet_send(handle, mix_packets[i].port, mix_packets[i].packet);
                            }
                        }
                    }
                    printf("%d after mixing 1\n", c.node_addr);

                    if (!*keep_running) {
                        return;
                    }
                }
            } else {
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
                        send_stp(handle, c, my_info);
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
                    if (!*keep_running) {
                        return;
                    }
                    
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

                    if (!*keep_running) {
                        return;
                    }
                } else if (packet->type == PACKET_TYPE_LSA) { // PACKET TYPE LSA
                    if (!neighbor_info[port].blocked) {
                        printf("%d received lsa\n", c.node_addr);
                        mixnet_packet_lsa* payload = (mixnet_packet_lsa*)(packet->payload);
                        add_to_global_view(&adj_list_start, payload->node_address, payload->links, payload->neighbor_count);
                        for (uint8_t port_n = 0; port_n < c.num_neighbors; port_n++) {
                            if (!neighbor_info[port_n].blocked && port_n != port) {
                                int packet_size = sizeof(mixnet_packet) + (4 + (4 * c.num_neighbors));
                                printf("%d before malloc here\n", c.node_addr);
                                mixnet_packet *to_send_packet = (mixnet_packet*)malloc(packet_size);
                                printf("%d after malloc here\n", c.node_addr);
                                if (to_send_packet == NULL) {
                                    printf("malloc 17 failed\n");
                                    return;
                                }
                                printf("set size\n");
                                to_send_packet->total_size = packet_size; 
                                printf("set type\n");
                                to_send_packet->type = PACKET_TYPE_LSA; 
                                printf("cast payload\n");
                                mixnet_packet_lsa* lsa_payload = (mixnet_packet_lsa*)(to_send_packet->payload);
                                printf("set payload node address\n");
                                lsa_payload->node_address = c.node_addr;
                                printf("set num neighbors\n");
                                lsa_payload->neighbor_count = c.num_neighbors;
                                printf("loop and write links\n");
                                printf("edge count %d\n", adj_list_start->edge_count);
                                printf("neighbor count %d\n", c.num_neighbors);
                                for (uint16_t i = 0; i < c.num_neighbors; i++) {
                                    lsa_payload->links[i].neighbor_mixaddr = adj_list_start->edge_list[i].neighbor_mixaddr;
                                    lsa_payload->links[i].cost = adj_list_start->edge_list[i].cost;
                                    // lsa_payload->links[i].neighbor_mixaddr = 62;
                                    // lsa_payload->links[i].cost = 1;
                                }
                                printf("send packet\n");
                                mixnet_send(handle, port_n, to_send_packet);
                            }
                        }
                        //compute_shortest_paths(adj_list_start, c.node_addr);
                        printf("%d forwarded lsa\n", c.node_addr);
                    }

                    if (!*keep_running) {
                        return;
                    }
                } else if (packet->type == PACKET_TYPE_FLOOD) {
                    if (!neighbor_info[port].blocked) {
                        for (uint8_t port_n = 0; port_n <= c.num_neighbors; port_n++) {
                            if (port_n < c.num_neighbors && !neighbor_info[port_n].blocked && port_n != port) {
                                // Forward to unblocked neighbors
                                forward_packet(handle, port_n, packet);
                            } else if (port_n == c.num_neighbors) {
                                // Forward to user
                                forward_packet(handle, port_n, packet);
                            }
                        }
                    }
                } else {
                    packet_counter++;
                    uint16_t forward_to;
                    mixnet_packet *new_packet = NULL;
                    if (packet->type == PACKET_TYPE_DATA) {
                        mixnet_packet_routing_header* payload = (mixnet_packet_routing_header*)(packet->payload);
                        if (payload->dst_address == c.node_addr) {
                            forward_packet(handle, c.num_neighbors, packet);
                            packet_counter--;
                        } else {
                            new_packet = (mixnet_packet*)malloc(packet->total_size);
                            if (new_packet == NULL) {
                                printf("malloc 18 failed\n");
                                return;
                            }
                            memcpy(new_packet, packet, packet->total_size);
                            mixnet_packet_routing_header* new_payload = (mixnet_packet_routing_header*)(new_packet->payload);
                            forward_to = -1;
                            for (int i = 0; i < c.num_neighbors; i++) {
                                if (neighbor_info[i].neighbor_addr == new_payload->route[new_payload->hop_index]) {
                                    forward_to = i;
                                    break;
                                }
                            }
                            new_payload->hop_index++;
                        }
                    }
                    else { // packet type PING
                        //TODO: produce new_packet and forward_to
                    }
                    
                    if (new_packet != NULL) {
                        if (packet_counter < c.mixing_factor) {
                                mix_packets[packet_counter-1].port = forward_to;
                                mix_packets[packet_counter-1].packet = new_packet;
                        } else {
                            for (int i = 0; i < c.mixing_factor; i++) {
                                mixnet_send(handle, mix_packets[i].port, mix_packets[i].packet);
                            }
                        }
                    }
                    if (!*keep_running) {
                        return;
                    }
                }
            }
        //    free(packet);
        }
    }
    //printf("Node %d thinks %d is root\n", c.node_addr, my_info.root_addr);
    // free(neighbor_info);
}
