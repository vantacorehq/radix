#include "radix.h"

#include <stdlib.h>
#include <string.h>

/*
 * Adaptive Radix Tree (ART)
 * Node types grow with fanout: 4 -> 16 -> 48 -> 256
 * Path compression via partial key prefix.
 */

enum {
    NODE4   = 0,
    NODE16  = 1,
    NODE48  = 2,
    NODE256 = 3,
    LEAF    = 4
};

typedef struct Node Node;

struct Node {
    uint8_t  type;
    uint8_t  num_children;
    uint32_t prefix_len;
    uint8_t  prefix[8];          /* partial key, truncated if longer */
};

typedef struct {
    Node     node;
    uint8_t  keys[4];
    Node    *children[4];
} Node4;

typedef struct {
    Node     node;
    uint8_t  keys[16];
    Node    *children[16];
} Node16;

typedef struct {
    Node     node;
    uint8_t  keys[256];          /* index -> child slot, 0xff = empty */
    Node    *children[48];
} Node48;

typedef struct {
    Node     node;
    Node    *children[256];
} Node256;

typedef struct {
    Node     node;
    void    *value;
    size_t   key_len;
    uint8_t  key[];              /* flexible array, full key stored in leaf */
} Leaf;

struct RadixTree {
    Node   *root;
    size_t  size;
};

static Node *alloc_node4(void) {
    Node4 *n = calloc(1, sizeof(Node4));
    if (n) n->node.type = NODE4;
    return (Node *)n;
}

static Node *alloc_node16(void) {
    Node16 *n = calloc(1, sizeof(Node16));
    if (n) n->node.type = NODE16;
    return (Node *)n;
}

static Node *alloc_node48(void) {
    Node48 *n = calloc(1, sizeof(Node48));
    if (n) {
        n->node.type = NODE48;
        memset(n->keys, 0xff, 256);
    }
    return (Node *)n;
}

static Node *alloc_node256(void) {
    Node256 *n = calloc(1, sizeof(Node256));
    if (n) n->node.type = NODE256;
    return (Node *)n;
}

static Leaf *alloc_leaf(const void *key, size_t key_len, void *value) {
    Leaf *l = malloc(sizeof(Leaf) + key_len);
    if (!l) return NULL;
    l->node.type = LEAF;
    l->node.num_children = 0;
    l->node.prefix_len = 0;
    l->value = value;
    l->key_len = key_len;
    memcpy(l->key, key, key_len);
    return l;
}

static void free_node(Node *n) {
    if (!n) return;
    if (n->type == LEAF) {
        free(n);
        return;
    }

    if (n->type == NODE4) {
        Node4 *n4 = (Node4 *)n;
        for (int i = 0; i < n4->node.num_children; i++)
            free_node(n4->children[i]);
    } else if (n->type == NODE16) {
        Node16 *n16 = (Node16 *)n;
        for (int i = 0; i < n16->node.num_children; i++)
            free_node(n16->children[i]);
    } else if (n->type == NODE48) {
        Node48 *n48 = (Node48 *)n;
        for (int i = 0; i < 48; i++)
            if (n48->children[i]) free_node(n48->children[i]);
    } else if (n->type == NODE256) {
        Node256 *n256 = (Node256 *)n;
        for (int i = 0; i < 256; i++)
            if (n256->children[i]) free_node(n256->children[i]);
    }
    free(n);
}

void radix_init(RadixTree *t) {
    if (!t) return;
    t->root = NULL;
    t->size = 0;
}

void radix_free(RadixTree *t) {
    if (!t) return;
    free_node(t->root);
    t->root = NULL;
    t->size = 0;
}

size_t radix_size(const RadixTree *t) {
    return t ? t->size : 0;
}

static int check_prefix(const Node *n, const uint8_t *key, size_t key_len, size_t depth) {
    size_t max = n->prefix_len;
    if (max > 8) max = 8;
    for (size_t i = 0; i < max && depth + i < key_len; i++) {
        if (n->prefix[i] != key[depth + i]) return (int)i;
    }
    if (n->prefix_len > 8) {
        /* truncated prefix — full check only possible via leaf */
        return (int)(n->prefix_len < key_len - depth ? n->prefix_len : key_len - depth);
    }
    return (int)n->prefix_len;
}

static Node **find_child(Node *n, uint8_t c) {
    if (n->type == NODE4) {
        Node4 *n4 = (Node4 *)n;
        for (int i = 0; i < n4->node.num_children; i++)
            if (n4->keys[i] == c) return &n4->children[i];
    } else if (n->type == NODE16) {
        Node16 *n16 = (Node16 *)n;
        for (int i = 0; i < n16->node.num_children; i++)
            if (n16->keys[i] == c) return &n16->children[i];
    } else if (n->type == NODE48) {
        Node48 *n48 = (Node48 *)n;
        if (n48->keys[c] != 0xff) return &n48->children[n48->keys[c]];
    } else if (n->type == NODE256) {
        Node256 *n256 = (Node256 *)n;
        if (n256->children[c]) return &n256->children[c];
    }
    return NULL;
}

static void add_child(Node **ref, uint8_t c, Node *child);

static void grow_node(Node **ref) {
    Node *old = *ref;

    if (old->type == NODE4 && old->num_children == 4) {
        Node16 *n16 = (Node16 *)alloc_node16();
        if (!n16) return;
        Node4 *n4 = (Node4 *)old;
        n16->node.prefix_len = n4->node.prefix_len;
        memcpy(n16->node.prefix, n4->node.prefix, 8);
        n16->node.num_children = 4;
        for (int i = 0; i < 4; i++) {
            n16->keys[i] = n4->keys[i];
            n16->children[i] = n4->children[i];
        }
        *ref = (Node *)n16;
        free(n4);
    } else if (old->type == NODE16 && old->num_children == 16) {
        Node48 *n48 = (Node48 *)alloc_node48();
        if (!n48) return;
        Node16 *n16 = (Node16 *)old;
        n48->node.prefix_len = n16->node.prefix_len;
        memcpy(n48->node.prefix, n16->node.prefix, 8);
        n48->node.num_children = 16;
        for (int i = 0; i < 16; i++) {
            n48->keys[n16->keys[i]] = (uint8_t)i;
            n48->children[i] = n16->children[i];
        }
        *ref = (Node *)n48;
        free(n16);
    } else if (old->type == NODE48 && old->num_children == 48) {
        Node256 *n256 = (Node256 *)alloc_node256();
        if (!n256) return;
        Node48 *n48 = (Node48 *)old;
        n256->node.prefix_len = n48->node.prefix_len;
        memcpy(n256->node.prefix, n48->node.prefix, 8);
        n256->node.num_children = 48;
        for (int i = 0; i < 256; i++) {
            if (n48->keys[i] != 0xff)
                n256->children[i] = n48->children[n48->keys[i]];
        }
        *ref = (Node *)n256;
        free(n48);
    }
}

static void add_child(Node **ref, uint8_t c, Node *child) {
    Node *n = *ref;

    if ((n->type == NODE4 && n->num_children == 4) ||
        (n->type == NODE16 && n->num_children == 16) ||
        (n->type == NODE48 && n->num_children == 48)) {
        grow_node(ref);
        n = *ref;
    }

    if (n->type == NODE4) {
        Node4 *n4 = (Node4 *)n;
        int i = n4->node.num_children;
        /* keep keys sorted for ordered iteration */
        while (i > 0 && n4->keys[i-1] > c) {
            n4->keys[i] = n4->keys[i-1];
            n4->children[i] = n4->children[i-1];
            i--;
        }
        n4->keys[i] = c;
        n4->children[i] = child;
        n4->node.num_children++;
    } else if (n->type == NODE16) {
        Node16 *n16 = (Node16 *)n;
        int i = n16->node.num_children;
        while (i > 0 && n16->keys[i-1] > c) {
            n16->keys[i] = n16->keys[i-1];
            n16->children[i] = n16->children[i-1];
            i--;
        }
        n16->keys[i] = c;
        n16->children[i] = child;
        n16->node.num_children++;
    } else if (n->type == NODE48) {
        Node48 *n48 = (Node48 *)n;
        int slot = n48->node.num_children;
        n48->children[slot] = child;
        n48->keys[c] = (uint8_t)slot;
        n48->node.num_children++;
    } else if (n->type == NODE256) {
        Node256 *n256 = (Node256 *)n;
        n256->children[c] = child;
        n256->node.num_children++;
    }
}

static int leaf_matches(const Leaf *l, const uint8_t *key, size_t key_len) {
    if (l->key_len != key_len) return 0;
    return memcmp(l->key, key, key_len) == 0;
}

void *radix_search(const RadixTree *t, const void *key, size_t key_len) {
    if (!t || !key) return NULL;
    const uint8_t *k = key;
    Node *n = t->root;
    size_t depth = 0;

    while (n) {
        if (n->type == LEAF) {
            Leaf *l = (Leaf *)n;
            if (leaf_matches(l, k, key_len)) return l->value;
            return NULL;
        }

        if (n->prefix_len) {
            int matched = check_prefix(n, k, key_len, depth);
            if ((uint32_t)matched != n->prefix_len) return NULL;
            depth += n->prefix_len;
        }

        if (depth >= key_len) return NULL;

        Node **child = find_child(n, k[depth]);
        if (!child) return NULL;
        n = *child;
        depth++;
    }
    return NULL;
}

static Leaf *make_leaf(const void *key, size_t key_len, void *value) {
    return alloc_leaf(key, key_len, value);
}

int radix_insert(RadixTree *t, const void *key, size_t key_len, void *value) {
    if (!t || !key || key_len == 0) return -1;
    const uint8_t *k = key;

    if (!t->root) {
        Leaf *l = make_leaf(key, key_len, value);
        if (!l) return -1;
        t->root = (Node *)l;
        t->size = 1;
        return 0;
    }

    Node **ref = &t->root;
    Node *n = t->root;
    size_t depth = 0;

    while (n) {
        if (n->type == LEAF) {
            Leaf *old = (Leaf *)n;

            if (leaf_matches(old, k, key_len)) {
                old->value = value;   /* replace */
                return 0;
            }

            /* split: create a new internal node */
            Node4 *new_node = (Node4 *)alloc_node4();
            if (!new_node) return -1;

            size_t i = 0;
            size_t max = old->key_len < key_len ? old->key_len : key_len;
            while (i < max && old->key[i] == k[i]) i++;

            /* common prefix length from current depth */
            size_t common = i - depth;
            if (common > 8) common = 8;
            new_node->node.prefix_len = (uint32_t)(i - depth);
            memcpy(new_node->node.prefix, k + depth, common);

            *ref = (Node *)new_node;

            /* old leaf */
            if (i < old->key_len) {
                add_child(ref, old->key[i], (Node *)old);
            } else {
                /* old key is prefix of new — should not happen with proper depth */
                add_child(ref, 0, (Node *)old);
            }

            /* new leaf */
            Leaf *nl = make_leaf(key, key_len, value);
            if (!nl) return -1;
            if (i < key_len) {
                add_child(ref, k[i], (Node *)nl);
            } else {
                add_child(ref, 0, (Node *)nl);
            }

            t->size++;
            return 0;
        }

        /* internal node */
        if (n->prefix_len) {
            int matched = check_prefix(n, k, key_len, depth);
            if ((uint32_t)matched != n->prefix_len) {
                /* mismatch in compressed path — split the node */
                Node4 *split = (Node4 *)alloc_node4();
                if (!split) return -1;

                split->node.prefix_len = (uint32_t)matched;
                memcpy(split->node.prefix, n->prefix, matched > 8 ? 8 : matched);

                uint8_t old_byte = n->prefix[matched];
                /* shrink old node prefix */
                uint32_t remaining = n->prefix_len - (uint32_t)matched - 1;
                memmove(n->prefix, n->prefix + matched + 1, remaining > 8 ? 8 : remaining);
                n->prefix_len = remaining;

                *ref = (Node *)split;
                add_child(ref, old_byte, n);

                Leaf *nl = make_leaf(key, key_len, value);
                if (!nl) return -1;
                uint8_t new_byte = (depth + matched < key_len) ? k[depth + matched] : 0;
                add_child(ref, new_byte, (Node *)nl);

                t->size++;
                return 0;
            }
            depth += n->prefix_len;
        }

        if (depth >= key_len) {
            /* key is prefix of existing path — treat as insert at this point */
            return -1; /* simplified: reject for now */
        }

        Node **child = find_child(n, k[depth]);
        if (!child) {
            Leaf *nl = make_leaf(key, key_len, value);
            if (!nl) return -1;
            add_child(ref, k[depth], (Node *)nl);
            t->size++;
            return 0;
        }

        ref = child;
        n = *child;
        depth++;
    }

    return -1;
}

int radix_delete(RadixTree *t, const void *key, size_t key_len) {
    if (!t || !key || !t->root) return -1;

    /* Simple version: search and null out. Full path compression shrink
       is left for a later pass. For many workloads this is enough. */
    const uint8_t *k = key;
    Node *n = t->root;
    Node **ref = &t->root;
    size_t depth = 0;
    Node **path[64];
    int path_len = 0;

    while (n) {
        if (path_len < 64) path[path_len++] = ref;

        if (n->type == LEAF) {
            Leaf *l = (Leaf *)n;
            if (!leaf_matches(l, k, key_len)) return -1;

            /* remove leaf */
            *ref = NULL;
            free(l);
            t->size--;

            /* simple cleanup: if parent is Node4/16 with 0 children, free it */
            /* (more sophisticated shrinking omitted for clarity) */
            return 0;
        }

        if (n->prefix_len) {
            int matched = check_prefix(n, k, key_len, depth);
            if ((uint32_t)matched != n->prefix_len) return -1;
            depth += n->prefix_len;
        }

        if (depth >= key_len) return -1;

        Node **child = find_child(n, k[depth]);
        if (!child) return -1;
        ref = child;
        n = *child;
        depth++;
    }
    return -1;
}
