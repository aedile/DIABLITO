/* pico/util/pheap.h shim: the OPL callback queue holds at most ten timers, so the "pairing heap"
 * is a sorted singly linked list. `sibling` is the next node and `child` is always 0, which keeps
 * opl_pico.c's child/sibling walk in AdjustCallbacks valid. Node ids are 1-based; 0 means none. */
#pragma once
#include "pico.h"
typedef uint8_t pheap_node_id_t;
typedef struct { pheap_node_id_t child, sibling, parent; } pheap_node_t;
typedef bool (*pheap_comparator)(void *user_data, pheap_node_id_t a, pheap_node_id_t b);
typedef struct {
    pheap_node_t *nodes;
    pheap_comparator comparator;
    void *user_data;
    pheap_node_id_t max_nodes, root_id, free_head_id;
} pheap_t;
#define PHEAP_DEFINE_STATIC(name, n) static pheap_node_t name##_nodes[n]; static pheap_t name = { .nodes = name##_nodes, .max_nodes = (n) }
static inline pheap_node_t *ph_get_node(pheap_t *h, pheap_node_id_t id) { return &h->nodes[id - 1]; }
static inline void ph_clear(pheap_t *h) {
    h->root_id = 0; h->free_head_id = 1;
    for (int i = 1; i <= h->max_nodes; i++) { h->nodes[i - 1].child = 0; h->nodes[i - 1].parent = 0; h->nodes[i - 1].sibling = i < h->max_nodes ? i + 1 : 0; }
}
static inline void ph_post_alloc_init(pheap_t *h, uint max_nodes, pheap_comparator cmp, void *user_data) {
    h->max_nodes = max_nodes; h->comparator = cmp; h->user_data = user_data; ph_clear(h);
}
static inline pheap_node_id_t ph_new_node(pheap_t *h) {
    pheap_node_id_t id = h->free_head_id;
    if (id) { h->free_head_id = ph_get_node(h, id)->sibling; ph_get_node(h, id)->sibling = 0; }
    return id;
}
static inline pheap_node_id_t ph_insert_node(pheap_t *h, pheap_node_id_t id) {
    pheap_node_id_t *link = &h->root_id;
    while (*link && !h->comparator(h->user_data, id, *link)) link = &ph_get_node(h, *link)->sibling;
    ph_get_node(h, id)->sibling = *link; *link = id;
    return h->root_id;
}
static inline pheap_node_id_t ph_peek_head(pheap_t *h) { return h->root_id; }
static inline pheap_node_id_t ph_remove_head(pheap_t *h, bool free_node) {
    pheap_node_id_t id = h->root_id;
    if (!id) return 0;
    h->root_id = ph_get_node(h, id)->sibling;
    ph_get_node(h, id)->sibling = 0;
    if (free_node) { ph_get_node(h, id)->sibling = h->free_head_id; h->free_head_id = id; }
    return id;
}
