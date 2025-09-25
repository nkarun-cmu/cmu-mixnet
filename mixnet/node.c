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
    path_component* random_path;
    uint16_t random_path_size;
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


void pq_push(pq_entry **pq, int distance, mixnet_address node) {
    pq_entry *to_add = malloc(sizeof(pq_entry));
    if (to_add == NULL) {
        return;
    }
    to_add->distance = distance;
    to_add->node = node;
    to_add->next = *pq;
    *pq = to_add;
}


pq_entry* pq_pop(pq_entry **pq) {
    if (pq == NULL || *pq == NULL) {
        return NULL;
    }

    pq_entry *current = *pq;
    pq_entry *curr_min = *pq;
    pq_entry *pre_remove = NULL;

    while (current->next != NULL) {
        if (current->next->distance < curr_min->distance) {
            curr_min = current->next;
            pre_remove = current;
        }
        current = current->next;
    }

    if (curr_min == *pq) {
        *pq = (*pq)->next;
    } else {
        pre_remove->next = curr_min->next;
    }
    
    return curr_min;
}



path_component* add_to_path(const path_component *path_before, mixnet_address node_addr) {
    if (path_before == NULL) {
        path_component *new_path = malloc(sizeof(path_component));
        if (new_path == NULL) {
            // // printf("malloc 2 failed\n");
            return NULL;
        }
        new_path->node = node_addr;
        new_path->next = NULL;
        return new_path;
   }
  
   path_component *new_path = malloc(sizeof(path_component));
   if (new_path == NULL) {
        // // printf("malloc 3 failed\n");
        return NULL;
    }
    memcpy(new_path, path_before, sizeof(path_component));
    path_component *current_new = new_path;
    const path_component *current_old = path_before->next;

    while (current_old != NULL) {
        current_new->next = malloc(sizeof(path_component));
        if (current_new->next == NULL) {
            // // printf("malloc 4 failed\n");
            return NULL;
        }
        current_new = current_new->next;
        memcpy(current_new, current_old, sizeof(path_component));
        current_old = current_old->next;
    }
  
    path_component *final_node = malloc(sizeof(path_component));
    if (final_node == NULL) {
        // // printf("malloc 5 failed\n");
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
    // // printf("forwarding packet\n");
    mixnet_packet *new_packet = (mixnet_packet*)malloc(packet->total_size);
    if (new_packet == NULL) {
        // // printf("malloc 6 failed\n");
        return 0;
    }
    memcpy(new_packet, packet, packet->total_size);
    // // printf("forwarded packet\n");
    return mixnet_send(handle, port_n, new_packet);
}


int send_stp(void *const handle, const struct mixnet_node_config c, stp_info my_info, uint64_t* stp_packet_counter){
    // // printf("%d sending stp packet\n", c.node_addr);
    int fail;
    for (int i = 0; i < c.num_neighbors; i++) {
        mixnet_packet *to_send_packet = (mixnet_packet*)malloc(sizeof(mixnet_packet) + sizeof(mixnet_packet_stp));
        if (to_send_packet == NULL) {
            // // printf("malloc 7 failed\n");
            return 0;
        }
        to_send_packet->total_size = sizeof(mixnet_packet) + sizeof(mixnet_packet_stp);
        to_send_packet->type = PACKET_TYPE_STP;
        
        mixnet_packet_stp* stp_payload = (mixnet_packet_stp*)(to_send_packet->payload);
        stp_payload->root_address = my_info.root_addr;
        stp_payload->path_length = my_info.path_len;
        stp_payload->node_address = c.node_addr;
        
        fail = mixnet_send(handle, i, to_send_packet);
        (*stp_packet_counter)++; // Increment the counter for each STP packet sent
        if (fail == -1) {
            return -1;
        }
    }
    // // printf("%d stp packet sent\n", c.node_addr);
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

// void print_in_global_view_list(global_view *p, mixnet_address node_addr) {
//     printf("PRINTING %d global_view \n", node_addr);
//     global_view *current = p;
//     while (current != NULL) {
//         printf("global_view node: %d\n", current->node_addr);
//         // printf("    %d edge count: %d\n", current->node_addr, current->edge_count);
//         // printf("    %d edge list: [", current->node_addr);
//         // for (int i= 0; i < current->edge_count; i++) {
//         //     printf("%d ", current->edge_list[i].neighbor_mixaddr);
//         // }
//         // printf("]\n");
//         // printf("    %d path len: %d\n", current->node_addr, current->path_size);
//         // printf("    %d distance: %d\n", current->node_addr, current->distance);
//         // printf("    path to %d: [", current->node_addr);
//         // path_component *start = current->path;
//         // while (start != NULL) {
//         //     printf("%d ", start->node);
//         //     start = start->next;
//         // }
//         // printf("]\n");
//         printf("    path to %d: [", current->node_addr);
//         path_component *start = current->random_path;
//         while (start != NULL) {
//             printf("%d ", start->node);
//             start = start->next;
//         }
//         printf("]\n");
//         current = current->next;
//     }
// }

void add_to_global_view(global_view **p,
                      mixnet_address node_addr,
                      mixnet_lsa_link_params *edges,
                      uint16_t count) {
    if (p == NULL) return;


    global_view *existing = find_in_global_view_list(*p, node_addr);
    if (existing != NULL) {
        //printf("node already exists\n");
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
    // printf("%d original edge list: [", node_addr);
    //     for (int i= 0; i < count; i++) {
    //         printf("%d ", edges[i].neighbor_mixaddr);
    //     }
    // printf("]\n");
    // printf("%d new edge list: [", node_addr);
    //     for (int i= 0; i < new_node->edge_count; i++) {
    //         printf("%d ", new_node->edge_list[i].neighbor_mixaddr);
    //     }
    // printf("]\n");
    new_node->next = *p;
    *p = new_node;
}

void compute_shortest_paths(global_view *p, mixnet_address src_addr) {
    //printf("%d computing shortest paths\n", src_addr);
    //print_in_global_view_list(p, src_addr);
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
        printf("couldn't find source node???\n");
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
        pq_entry *u = pq_pop(&pq);
        global_view *u_node = find_in_global_view_list(p, u->node);
        //printf("u: %d\n", u_node->node_addr);       
        if (u_node->distance == (uint16_t)INT_MAX) continue;


        mixnet_lsa_link_params* edge_list = u_node->edge_list;
        uint16_t u_dist = u_node->distance;
        path_component *u_path = u_node->path;


        for (int i = 0; i < u_node->edge_count; i++) {
            mixnet_address v_addr = edge_list[i].neighbor_mixaddr;
            global_view *v_node = find_in_global_view_list(p, v_addr);
            //printf("    v: %d\n", v_node->node_addr); 
            if (v_node == NULL) {
                //printf("        %d not in global_view\n", v_addr);
                continue;
            }
            uint16_t weight = edge_list[i].cost;
            
            uint16_t new_dist = u_dist + weight;
            uint16_t old_dist = v_node->distance;
            // printf("        dist[v]: %d\n", old_dist);
            // printf("        dist[u]: %d\n", u_dist);
            // printf("        dist[u] + weight: %d\n", new_dist);

            bool should_update = false;

            if (new_dist < old_dist) {
                should_update = true;
            } else if (new_dist == old_dist) {
                mixnet_address check = u_node->node_addr;
                const path_component* current = v_node->path;
                bool do_up = true;
                if (current == NULL) {
                    do_up = false;
                }
                while (do_up && current->next != NULL) {
                    current = current->next;
                }
                if (do_up && check < current->node) {
                    should_update = true;
                }
            }   


            // In compute_shortest_paths(), inside the if (should_update) block

            if (should_update) {
                // printf("        update\n");
                // printf("        size of path to u: %d\n", u_node->path_size);
                // printf("        path to %d: [", u_node->node_addr);
                // path_component *start = u_node->path;
                // while (start != NULL) {
                //     printf("%d ", start->node);
                //     start = start->next;
                // }
                //printf("]\n");
                path_component* new_path = NULL;
                if (u_node->node_addr != src_addr) {
                    new_path = add_to_path(u_path, u_node->node_addr);
                }
                
                v_node->distance = new_dist;
                v_node->path = new_path;
                v_node->path_size = (u_node->node_addr == src_addr) ? 0 : u_node->path_size + 1;

                if (u_node->node_addr == src_addr) {
                    v_node->first_hop_addr = v_addr;
                } else {
                    v_node->first_hop_addr = u_node->first_hop_addr;
                }
                // printf("        dist[v]: %d\n", v_node->distance);
                // printf("        size of path to v: %d\n", v_node->path_size);
                // printf("        path to %d: [", v_node->node_addr);
                // path_component *curr = v_node->path;
                // while (curr != NULL) {
                //     printf("%d ", curr->node);
                //     curr = curr->next;
                // }
                // printf("]\n");
                pq_push(&pq, v_node->distance, v_addr);
            }
        }
    //    free(pq_min);
    }
    //printf("%d shortest paths computed\n", src_addr);
    //print_in_global_view_list(p, src_addr);
}

path_component *copy_prefix(path_component *head, int count) {
    if (count == 0 || head == NULL) return NULL;
    path_component *copy = malloc(sizeof(path_component));
    copy->node = head->node;
    copy->next = copy_prefix(head->next, count - 1);
    return copy;
}

path_component *prepend(uint16_t node, path_component *next) {
    path_component *pc = malloc(sizeof(path_component));
    pc->node = node;
    pc->next = next;
    return pc;
}

void compute_random_paths(global_view *p, mixnet_address src_addr, global_view* dest_node) {
    //printf("%d computing random path for %d\n", src_addr, dest_node->node_addr);
    path_component *shortest_path = dest_node->path;
    uint16_t shortest_size = dest_node->path_size;
    global_view *self = find_in_global_view_list(p, src_addr);
    if (self->edge_count > 1) {
        int num = (rand() % (2 - 1 + 1)) + 1;
        //printf("num: %d\n", num);
        //num % 2 == 0
            if (num == 2) {
                mixnet_address neighbor = 0;
                for (uint16_t i = 0; i < self->edge_count; i++) {
                    if (self->edge_list[i].neighbor_mixaddr != dest_node->node_addr) {
                        neighbor = self->edge_list[i].neighbor_mixaddr;
                        break;
                    }
                }
                path_component *copied_path = copy_prefix(shortest_path, shortest_size);
                path_component *new_path = prepend(src_addr, copied_path);
                new_path = prepend(neighbor, new_path);
                dest_node->random_path = new_path;
                dest_node->random_path_size = 2 + shortest_size;
            } else {
                dest_node->random_path = dest_node->random_path;
                dest_node->random_path_size = dest_node->path_size;
            }
    } else {
        dest_node->random_path = dest_node->random_path;
        dest_node->random_path_size = dest_node->path_size;
    }
    //printf("%d random paths computed\n", src_addr);
    //printf("random path to %d: [", dest_node->node_addr);
    // path_component *start = dest_node->random_path;
    // while (start != NULL) {
    //     printf("%d ", start->node);
    //     start = start->next;
    // }
    //printf("]\n");
}

mixnet_packet* create_forwarding_packet(
    const mixnet_packet* src_packet,
    const path_component* path,
    uint16_t path_len)
{
    mixnet_packet_routing_header* src_rh = (mixnet_packet_routing_header*)(src_packet->payload);
    
    size_t old_payload_size = 0;
    const char* old_payload_ptr = NULL;

    if (src_packet->type == PACKET_TYPE_DATA) {
        old_payload_size = src_packet->total_size - sizeof(mixnet_packet) - sizeof(mixnet_packet_routing_header);
        old_payload_ptr = (const char*)src_rh + sizeof(mixnet_packet_routing_header);
    } else if (src_packet->type == PACKET_TYPE_PING) {
        old_payload_size = sizeof(mixnet_packet_ping);
    }

    size_t new_rh_size = sizeof(mixnet_packet_routing_header) + (path_len * sizeof(mixnet_address));
    size_t new_total_size = sizeof(mixnet_packet) + new_rh_size + old_payload_size;

    mixnet_packet* new_packet = (mixnet_packet*)malloc(new_total_size);
    if (new_packet == NULL) return NULL;

    new_packet->total_size = new_total_size;
    new_packet->type = src_packet->type;
    
    mixnet_packet_routing_header* new_rh = (mixnet_packet_routing_header*)(new_packet->payload);
    char* new_payload_ptr = (char*)new_rh + new_rh_size; 

    new_rh->src_address = src_rh->src_address;
    new_rh->dst_address = src_rh->dst_address;
    new_rh->route_length = path_len;
    new_rh->hop_index = 0;

    const path_component* current_hop = path;
    for (int i = 0; i < path_len; i++) {
        if (current_hop == NULL) break;
        new_rh->route[i] = current_hop->node;
        current_hop = current_hop->next;
    }
    
    if (new_packet->type == PACKET_TYPE_PING) { // TODO: ask at OH what send time should be
        mixnet_packet_ping* src_payload = (mixnet_packet_ping*)(src_packet->payload);
        //mixnet_packet_ping* ping_payload = (mixnet_packet_ping*)new_payload_ptr;
        //memcpy(ping_payload, src_payload, old_payload_size);
        mixnet_packet_ping* ping_payload = (mixnet_packet_ping*)new_payload_ptr;
        ping_payload->is_request = true;
        ping_payload->send_time = src_payload->send_time;
    } else if (old_payload_size > 0) {
        memcpy(new_payload_ptr, old_payload_ptr, old_payload_size);
    }
    
    return new_packet;
}

void run_node(void *const handle,
             volatile bool *const keep_running,
             const struct mixnet_node_config c) {
    // // printf("in run_node\n");

    (void) c;
    (void) handle;
    int reelection_interval = c.reelection_interval_ms;

    uint64_t stp_packets_sent = 0;
    uint64_t last_stp_update_time = time_now();
    bool stp_converged = false;

    int hello_interval = c.root_hello_interval_ms;
    int temp_lsa_counter = 0;
    srand(time(NULL));

    // mixing stuff
    uint16_t packet_counter = 0;
    // // printf("defining mix packet store\n");
    mixing_packet_held *mix_packets = malloc(sizeof(mixing_packet_held)*c.mixing_factor);
    if (mix_packets == NULL) {
        // // printf("malloc 10 failed\n");
        return;
    }
    // // printf("mix packet store defined\n");

    mixnet_lsa_link_params *neighbhor_costs = malloc(sizeof(mixnet_lsa_link_params)*(c.num_neighbors));
    if (neighbhor_costs == NULL) {
        // // printf("malloc 11 failed\n");
        return;
    }

    for (int i = 0; i < c.num_neighbors; i++) {
        neighbhor_costs[i].cost = c.link_costs[i];
    }
    // // printf("starting global view\n");
    global_view *adj_list_start = malloc(sizeof(global_view)); //will start with pointer to our own list
    if (adj_list_start == NULL) {
        // // printf("malloc 12 failed\n");
        return;
    }
    adj_list_start->node_addr = c.node_addr;
    adj_list_start->edge_list = neighbhor_costs;
    adj_list_start->edge_count = c.num_neighbors;
    adj_list_start->next = NULL;
    // // printf("global view started\n");

    // Timer variables
    uint64_t last_hello_time = time_now();
    uint64_t start_time = time_now();
    uint64_t last_root_message_time = time_now();
    bool lsa_done = false;

    // printf("running node: %d\n", c.node_addr);
    // printf("num neigbors: %d\n", c.num_neighbors);
    stp_info my_info = {c.node_addr, c.node_addr, 0};
    neighbor_state_t *neighbor_info = (neighbor_state_t*)calloc(c.num_neighbors, sizeof(neighbor_state_t));
    


    for (int i = 0; i < c.num_neighbors; i++) {
        neighbor_info[i].blocked = false;
        // // printf("sending stp packet\n");
        mixnet_packet *to_send_packet = (mixnet_packet*)malloc(18);
        if (to_send_packet == NULL) {
            // // printf("malloc 13 failed\n");
            return;
        }
        to_send_packet->total_size = 18;
        to_send_packet->type = PACKET_TYPE_STP;
        mixnet_packet_stp* stp = (mixnet_packet_stp*)(to_send_packet->payload);
        stp->root_address = c.node_addr;
        stp->path_length= 0;
        stp->node_address= c.node_addr;
        mixnet_send(handle, i, to_send_packet);
        stp_packets_sent++; // Count initial STP packets
        // // printf("stp packet sent\n");
    }


    while(*keep_running) {
        uint64_t current_time = time_now();
        
        // Check if we need to send hello messages (only if we are the root)
        if (my_info.root_addr == c.node_addr &&
            current_time - last_hello_time >= (uint64_t)hello_interval) {
                send_stp(handle, c, my_info, &stp_packets_sent);
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
            send_stp(handle, c, my_info, &stp_packets_sent);          
            last_root_message_time = current_time;

            // A state change occurred, so reset convergence timer
            last_stp_update_time = current_time;
            stp_converged = false;
        }

        if (!*keep_running) {
            return;
        }

        // Check for and report convergence
        if (!stp_converged && (current_time - last_stp_update_time > 2 * c.reelection_interval_ms)) {
            stp_converged = true;
            uint64_t convergence_time_ms = current_time - start_time;
            // Output to stderr to avoid interfering with any autograder stdout checks
            fprintf(stderr, "[Node %u] STP Converged: Time=%llu ms, STP Packets Sent=%llu\n",
                    c.node_addr, (unsigned long long)convergence_time_ms, (unsigned long long)stp_packets_sent);
        }

        //// // // printf("before lsa broadcast\n");
        if (current_time - start_time >= 100 && !lsa_done) {
        // // printf("%d start lsa broadcast\n", c.node_addr);
            for (uint8_t port_n = 0; port_n < c.num_neighbors; port_n++) {
                if (!neighbor_info[port_n].blocked) {
                    //send LSA packet
                    //mixnet_packet *to_send_packet = (mixnet_packet*)malloc(sizeof(mixnet_packet) + sizeof(mixnet_packet_stp));
                    //to_send_packet->total_size = sizeof(mixnet_packet) + sizeof(mixnet_packet_stp);
                    //mixnet_packet_stp* stp_payload = (mixnet_packet_stp*)(to_send_packet->payload);
                    // // printf("mallocing lsa\n");
                    int packet_size = sizeof(mixnet_packet) + (4 + (4 * c.num_neighbors));
                    mixnet_packet *to_send_packet = (mixnet_packet*)malloc(packet_size);
                    if (to_send_packet == NULL) {
                        // // printf("malloc 14 failed\n");
                        return;
                    }
                    to_send_packet->total_size = packet_size;
                    to_send_packet->type = PACKET_TYPE_LSA;
                    
                    mixnet_packet_lsa* lsa_payload = (mixnet_packet_lsa*)(to_send_packet->payload);
                    lsa_payload->node_address = c.node_addr;
                    lsa_payload->neighbor_count = c.num_neighbors;
                    for (uint16_t i = 0; i < c.num_neighbors; i++) {
                        lsa_payload->links[i].neighbor_mixaddr = neighbhor_costs[i].neighbor_mixaddr;
                        lsa_payload->links[i].cost = neighbhor_costs[i].cost;
                    }
                    // printf("%d new LSA edge list: [", c.node_addr);
                    // for (int i= 0; i < lsa_payload->neighbor_count; i++) {
                    //     printf("%d ", lsa_payload->links[i].neighbor_mixaddr);
                    // }
                    // printf("]\n");
                    mixnet_send(handle, port_n, to_send_packet);
                }
            }
            lsa_done = true;
            // // printf("%d finish lsa broadcast\n", c.node_addr);
        }
        // // printf("after lsa broadcast\n");

        if (!*keep_running) {
            return;
        }

        mixnet_packet *packet;
        uint8_t port = 0;
        // packet received
        if (mixnet_recv(handle, &port, &packet) == 1) {
            if (port == c.num_neighbors){ // source node
                //// // printf("user sent flood packet. sending flood out as source node\n");
                if (packet->type == 1) { // PACKET TYPE FLOOD
                    // // // printf("packet type is actually flood\n");
                    for (uint8_t port_n = 0; port_n <c.num_neighbors; port_n++) {
                        if (!neighbor_info[port_n].blocked) {
                            forward_packet(handle, port_n, packet);
                        }
                    }
                    if (!*keep_running) {
                        return;
                    }
                } else { // PACKET TYPE PING OR DATA received from the user
                    mixnet_packet* new_packet = NULL;
                    uint16_t forward_to = (uint16_t)-1;

                    if (packet->type == PACKET_TYPE_DATA) {
                        mixnet_packet_routing_header* payload = (mixnet_packet_routing_header*)(packet->payload);
                        payload->src_address = c.node_addr;
                        //printf("user sending data packet from %d to %d\n", c.node_addr, payload->dst_address);
                        // print_in_global_view_list(adj_list_start, c.node_addr);
                        global_view* destination_node = find_in_global_view_list(adj_list_start, payload->dst_address);

                        if (destination_node != NULL && destination_node->distance != (uint16_t)INT_MAX) {
                            if (c.do_random_routing){
                                compute_random_paths(adj_list_start, c.node_addr, destination_node);
                                new_packet = create_forwarding_packet(packet, destination_node->random_path, destination_node->random_path_size);
                            } else {
                                new_packet = create_forwarding_packet(packet, destination_node->path, destination_node->path_size);
                            }
                            mixnet_address first_hop_addr = destination_node->first_hop_addr;
                            for (int i = 0; i < c.num_neighbors; i++) {
                                if (neighbor_info[i].neighbor_addr == first_hop_addr) {
                                    forward_to = i;
                                    break;
                                }
                            }
                        }
                    } else { // PACKET TYPE PING
                        mixnet_packet_routing_header* payload = (mixnet_packet_routing_header*)(packet->payload);
                        payload->src_address = c.node_addr;
                        global_view* destination_node = find_in_global_view_list(adj_list_start, payload->dst_address);
                        
                        if (destination_node != NULL && destination_node->distance != (uint16_t)INT_MAX) {
                            new_packet = create_forwarding_packet(packet, destination_node->path, destination_node->path_size);
                            
                            mixnet_address first_hop_addr = destination_node->first_hop_addr;
                            for (int i = 0; i < c.num_neighbors; i++) {
                                if (neighbor_info[i].neighbor_addr == first_hop_addr) {
                                    forward_to = i;
                                    break;
                                }
                            }
                        }
                    }

                    if (new_packet != NULL && forward_to != (uint16_t)-1) {
                        // Inside the user port block, after creating the PING packet
                        // if (new_packet->type == PACKET_TYPE_PING) {
                        //     // mixnet_packet_routing_header* rh = (mixnet_packet_routing_header*)(new_packet->payload);
                        //     // printf("[Node %d] creating pring request to %d. route length: %d. rout: ", c.node_addr, rh->dst_address, rh->route_length);
                        //     // for (int i = 0; i < rh->route_length; i++) {
                        //     //     printf("%d ", rh->route[i]);
                        //     // }
                        //     // printf("\n");
                        // } 
                        mix_packets[packet_counter].packet = new_packet;
                        mix_packets[packet_counter].port = forward_to;
                        packet_counter++;

                        if (packet_counter >= c.mixing_factor) {
                            for (uint16_t i = 0; i < packet_counter; i++) {
                                mixnet_send(handle, mix_packets[i].port, mix_packets[i].packet);
                            }
                            packet_counter = 0;
                        }
                    }
                }
            } else {
                if (packet->type == PACKET_TYPE_STP) {
                    mixnet_packet_stp* payload = (mixnet_packet_stp*)(packet->payload);
                    neighbor_info[port].neighbor_addr = payload->node_address;
                    neighbhor_costs[port].neighbor_mixaddr = payload->node_address;
                    //// // printf("received stp packet from %d claiming %d is the root with path len %d\n", payload->node_address, payload->root_address, payload->path_length);
                    
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
                        send_stp(handle, c, my_info, &stp_packets_sent);
                        last_root_message_time = current_time;
                    }


                    if (to_update) {
                        mixnet_address old_next_hop = my_info.next_hop;
                        my_info.root_addr = payload->root_address;
                        my_info.next_hop = payload->node_address;
                        my_info.path_len = payload->path_length + 1;
                        
                        last_root_message_time = current_time;
                        
                        // STP state has changed, so reset convergence timer
                        last_stp_update_time = current_time;
                        stp_converged = false;


                        if (old_next_hop != c.node_addr) {
                            for (int i = 0; i < c.num_neighbors; i++) {
                                if (neighbor_info[i].neighbor_addr == old_next_hop) {
                                    neighbor_info[i].blocked = true;
                                }
                            }
                        }


                        send_stp(handle, c, my_info, &stp_packets_sent);
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
                    // // // printf("root: %d, next hop: %d, path len: %d\n", my_info.root_addr, my_info.next_hop, my_info.path_len);
                    // for (int i = 0; i < c.num_neighbors; i++) {
                    //     // // printf("link to %d blocked: %s\n", neighbor_info[i].neighbor_addr, neighbor_info[i].blocked ? "true" : "false");
                    // }

                    if (!*keep_running) {
                        return;
                    }
                } else if (packet->type == PACKET_TYPE_LSA) { // PACKET TYPE LSA
                    if (!neighbor_info[port].blocked) {
                        temp_lsa_counter++;
                        mixnet_packet_lsa* payload = (mixnet_packet_lsa*)(packet->payload);
                        add_to_global_view(&adj_list_start, payload->node_address, payload->links, payload->neighbor_count);
                        
                        // Forward this LSA to other neighbors
                        for (uint8_t port_n = 0; port_n < c.num_neighbors; port_n++) {
                            if (!neighbor_info[port_n].blocked && port_n != port) {
                                int packet_size = sizeof(mixnet_packet) + (4 + (4 * payload->neighbor_count));
                                
                                mixnet_packet *to_send_packet = (mixnet_packet*)malloc(packet_size);
                                if (to_send_packet == NULL) { return; }
                                
                                memcpy(to_send_packet, packet, packet_size);
                                to_send_packet->total_size = packet_size;
                                
                                mixnet_send(handle, port_n, to_send_packet);
                            }
                        }
                        compute_shortest_paths(adj_list_start, c.node_addr);
                        // global_view* curr = adj_list_start;
                        // while (curr != NULL) {
                        //     printf("Node %d: Path to %d: ", c.node_addr, curr->node_addr);
                        //     path_component* p = curr->path;
                        //     while (p != NULL) {
                        //         printf("%d ", p->node);
                        //         p = p->next;
                        //     }
                        //     printf("(first_hop: %d)\n", curr->first_hop_addr);
                        //     curr = curr->next;
                        // }
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
               } else { // DATA or PING packet for forwarding or destination
                    mixnet_packet_routing_header* payload = (mixnet_packet_routing_header*)(packet->payload);
                    // printf("%d receiving data/ping packet from %d\n", c.node_addr, neighbor_info[port].neighbor_addr);
                    // printf("    must forward from %d to %d\n", payload->src_address, payload->dst_address);
                   if (payload->dst_address == c.node_addr) { // Packet is for me
                        if (packet->type == PACKET_TYPE_PING) {
                            size_t rh_size = sizeof(mixnet_packet_routing_header) + (payload->route_length * sizeof(mixnet_address));
                            mixnet_packet_ping* ping_payload = (mixnet_packet_ping*)((char*)payload + rh_size);
                            // printf("[Node %d] receiving ping from %d. is_request: %d. hop_index: %d\n", c.node_addr, payload->src_address, ping_payload->is_request, payload->hop_index);
                            // printf("forwarding to user\n");
                            forward_packet(handle, c.num_neighbors, packet);
                            //mixnet_send(handle, c.num_neighbors, packet);

                            if (ping_payload->is_request) {
                                for(int i = 0; i < payload->route_length / 2; i++) {
                                    mixnet_address temp = payload->route[i];
                                    payload->route[i] = payload->route[payload->route_length - 1 - i];
                                    payload->route[payload->route_length - 1 - i] = temp;
                                }
                                mixnet_address temp_addr = payload->src_address;
                                payload->src_address = payload->dst_address;
                                payload->dst_address = temp_addr;
                                payload->hop_index = 0;
                                ping_payload->is_request = false;
                                
                                mixnet_address next_hop_addr = (payload->route_length > 0) ? payload->route[0] : payload->dst_address;

                                uint16_t forward_to = (uint16_t)-1;
                                for (int i = 0; i < c.num_neighbors; i++) {
                                    if (neighbor_info[i].neighbor_addr == next_hop_addr) {
                                        forward_to = i;
                                        break;
                                    }
                                }
                                if (forward_to != (uint16_t)-1) {
                                    mixnet_send(handle, forward_to, packet);
                                }
                            } else {
                                uint64_t rtt = time_now() - ping_payload->send_time;
                                printf("RTT %d:%d is %lu\n", payload->src_address, payload->dst_address, rtt);
                            }
                            //forward_packet(handle, c.num_neighbors, packet);
                            //mixnet_send(handle, c.num_neighbors, packet);
                        } else { // It's a DATA packet for me, send to user
                            //printf("forwarding to user\n");
                            forward_packet(handle, c.num_neighbors, packet);
                        }                               
                    } else {
                        mixnet_packet_routing_header* received_rh = (mixnet_packet_routing_header*)(packet->payload);
                        //printf("[Node %d] forwarding ping from %d. is_request: %d. hop_index: %d\n", c.node_addr, received_rh->src_address, received_rh->is_request, received_rh->hop_index);
                        uint16_t current_hop_index = received_rh->hop_index;
                        mixnet_address next_hop_addr;

                        if (current_hop_index + 1 < received_rh->route_length) {
                            next_hop_addr = received_rh->route[current_hop_index + 1];
                        } else {
                            next_hop_addr = received_rh->dst_address;
                        }

                        uint16_t forward_to = (uint16_t)-1;
                        for (int i = 0; i < c.num_neighbors; i++) {
                            if (neighbor_info[i].neighbor_addr == next_hop_addr) {
                                forward_to = i;
                                break;
                            }
                        }

                        if (forward_to != (uint16_t)-1) {
                            mixnet_packet* packet_to_send = (mixnet_packet*)malloc(packet->total_size);
                            memcpy(packet_to_send, packet, packet->total_size);
                            mixnet_packet_routing_header* outgoing_rh = (mixnet_packet_routing_header*)(packet_to_send->payload);
                            outgoing_rh->hop_index++;
                            
                            mix_packets[packet_counter].packet = packet_to_send;
                            mix_packets[packet_counter].port = forward_to;
                            packet_counter++;

                            if (packet_counter >= c.mixing_factor) {
                                for (uint16_t i = 0; i < packet_counter; i++) {
                                    mixnet_send(handle, mix_packets[i].port, mix_packets[i].packet);
                                }
                                packet_counter = 0;
                            }
                        }
                    }
                }
            }
        //    free(packet);
        }
    }
    //// // printf("Node %d thinks %d is root\n", c.node_addr, my_info.root_addr);
    // free(neighbor_info);
}