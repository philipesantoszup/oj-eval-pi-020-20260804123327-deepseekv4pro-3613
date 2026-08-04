#include "buddy.h"
#include <stdlib.h>

#define NULL ((void *)0)
#define PAGE_SIZE 4096
#define MAX_RANK 16

/* Doubly-linked free block node stored inside free blocks */
struct free_block {
    struct free_block *next;
    struct free_block *prev;
};

/* Global state */
static void          *mem_base       = NULL;
static int            total_pages    = 0;
static int           *page_rank      = NULL;   /* rank of the block each page belongs to */
static int           *page_free      = NULL;   /* 1 = free, 0 = allocated */
static struct free_block *free_lists[MAX_RANK + 1]; /* heads of free lists, index by rank */
static int            free_cnt[MAX_RANK + 1];   /* cached block count per rank */
static int            max_free_rank = 0;        /* highest rank with non-empty free list */

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static inline int page_index(void *p)
{
    return (int)(((char *)p - (char *)mem_base) / PAGE_SIZE);
}

static inline void *page_addr(int idx)
{
    return (char *)mem_base + idx * PAGE_SIZE;
}

/* Check whether p is a valid page-aligned address inside the pool */
static int is_valid_page(void *p)
{
    if (p == NULL) return 0;
    unsigned long offset = (unsigned long)((char *)p - (char *)mem_base);
    /* Check bounds using offset arithmetic to avoid UB from pointer comparison */
    if (offset >= (unsigned long)total_pages * PAGE_SIZE) return 0;
    if (offset % PAGE_SIZE != 0) return 0;
    return 1;
}

/* Remove a block from its rank's free list */
static void list_remove(void *p, int rank)
{
    struct free_block *blk = (struct free_block *)p;
    if (blk->prev)
        blk->prev->next = blk->next;
    else
        free_lists[rank] = blk->next;
    if (blk->next)
        blk->next->prev = blk->prev;
    free_cnt[rank]--;
    /* update max_free_rank if we emptied the highest rank */
    if (free_cnt[rank] == 0 && rank == max_free_rank) {
        while (max_free_rank > 0 && free_cnt[max_free_rank] == 0)
            max_free_rank--;
    }
}

/* Insert a block at the head of its rank's free list */
static void list_insert(void *p, int rank)
{
    struct free_block *blk = (struct free_block *)p;
    blk->next = free_lists[rank];
    blk->prev = NULL;
    if (free_lists[rank])
        free_lists[rank]->prev = blk;
    free_lists[rank] = blk;
    free_cnt[rank]++;
    if (rank > max_free_rank)
        max_free_rank = rank;
}

/* Set the metadata for every page belonging to a block */
static void mark_pages(void *p, int rank, int is_free)
{
    int idx   = page_index(p);
    int count = 1 << (rank - 1);
    for (int i = 0; i < count; ++i) {
        page_rank[idx + i] = rank;
        page_free[idx + i] = is_free;
    }
}

/* ------------------------------------------------------------------ */
/*  API implementation                                                */
/* ------------------------------------------------------------------ */

int init_page(void *p, int pgcount)
{
    /* Release previous metadata if any */
    if (page_rank) { free(page_rank); page_rank = NULL; }
    if (page_free) { free(page_free); page_free = NULL; }

    mem_base    = p;
    total_pages = pgcount;

    for (int r = 1; r <= MAX_RANK; ++r) {
        free_lists[r] = NULL;
        free_cnt[r]   = 0;
    }
    max_free_rank = 0;

    page_rank = (int *)malloc((unsigned)pgcount * sizeof(int));
    page_free = (int *)malloc((unsigned)pgcount * sizeof(int));
    if (!page_rank || !page_free) {
        if (page_rank) { free(page_rank); page_rank = NULL; }
        if (page_free) { free(page_free); page_free = NULL; }
        return -ENOSPC;
    }

    /* Start with everything free, then build maximal blocks */
    for (int i = 0; i < pgcount; ++i) {
        page_rank[i] = 0;
        page_free[i] = 1;
    }

    int remaining = pgcount;
    int offset    = 0;

    for (int rank = MAX_RANK; rank >= 1; --rank) {
        int blk_pages = 1 << (rank - 1);
        while (remaining >= blk_pages) {
            void *addr = page_addr(offset);
            mark_pages(addr, rank, 1);
            list_insert(addr, rank);
            offset    += blk_pages;
            remaining -= blk_pages;
        }
    }

    return OK;
}

void *alloc_pages(int rank)
{
    if (rank < 1 || rank > MAX_RANK)
        return ERR_PTR(-EINVAL);

    /* 1. Exact match */
    if (free_lists[rank]) {
        void *addr = (void *)free_lists[rank];
        list_remove(addr, rank);
        mark_pages(addr, rank, 0);
        return addr;
    }

    /* 2. Split a larger block */
    if (rank <= max_free_rank) {
        for (int r = rank + 1; r <= max_free_rank; ++r) {
            if (free_lists[r]) {
                void *addr = (void *)free_lists[r];
                list_remove(addr, r);

                /* Split from r down to rank */
                for (int s = r; s > rank; --s) {
                    int    half_pages = 1 << (s - 2);
                    void  *buddy      = (char *)addr + half_pages * PAGE_SIZE;
                    mark_pages(buddy, s - 1, 1);
                    list_insert(buddy, s - 1);
                }

                mark_pages(addr, rank, 0);
                return addr;
            }
        }
    }

    return ERR_PTR(-ENOSPC);
}

int return_pages(void *p)
{
    if (!is_valid_page(p))
        return -EINVAL;

    int idx  = page_index(p);
    int rank = page_rank[idx];

    if (page_free[idx])
        return -EINVAL;

    /* Must be the start of a block */
    if (idx % (1 << (rank - 1)) != 0)
        return -EINVAL;

    mark_pages(p, rank, 1);

    /* Merge with buddy as long as possible */
    while (rank < MAX_RANK) {
        unsigned long offset     = (unsigned long)((char *)p - (char *)mem_base);
        unsigned long blk_bytes  = (unsigned long)PAGE_SIZE << (rank - 1);
        unsigned long bud_off    = offset ^ blk_bytes;

        if (bud_off >= (unsigned long)total_pages * PAGE_SIZE)
            break;

        int   bud_idx   = (int)(bud_off / PAGE_SIZE);

        /* The entire buddy block must fit within the pool */
        if (bud_idx + (1 << (rank - 1)) > total_pages)
            break;

        void *buddy     = (char *)mem_base + bud_off;

        if (page_free[bud_idx] && page_rank[bud_idx] == rank) {
            list_remove(buddy, rank);
            if (buddy < p) {
                p   = buddy;
                idx = bud_idx;
            }
            ++rank;
            mark_pages(p, rank, 1);
        } else {
            break;
        }
    }

    list_insert(p, rank);
    return OK;
}

int query_ranks(void *p)
{
    if (!is_valid_page(p))
        return -EINVAL;

    int idx  = page_index(p);
    int rank = page_rank[idx];

    /* For allocated pages, p must be the start of its block */
    if (!page_free[idx] && idx % (1 << (rank - 1)) != 0)
        return -EINVAL;

    return rank;
}

int query_page_counts(int rank)
{
    if (rank < 1 || rank > MAX_RANK)
        return -EINVAL;

    return free_cnt[rank];
}