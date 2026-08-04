#include <stdio.h>
#include <stdlib.h>
#include "buddy.h"

int main() {
    printf("=== Test 1: non-power-of-2 init ===\n");
    void *p = malloc(40 * 4096); // 40 pages
    init_page(p, 40);
    
    // After init, check counts
    printf("Rank 5 count: %d (expect 1)\n", query_page_counts(5)); // 2^4=32 pages
    printf("Rank 4 count: %d (expect 1)\n", query_page_counts(4)); // 2^3=8 pages, remaining=40-32=8
    printf("Rank 3 count: %d (expect 0)\n", query_page_counts(3));
    printf("Rank 2 count: %d (expect 0)\n", query_page_counts(2));
    printf("Rank 1 count: %d (expect 0)\n", query_page_counts(1));
    
    printf("\n=== Test 2: alloc rank 5 ===\n");
    void *a = alloc_pages(5);
    printf("alloc rank 5: %s\n", IS_ERR(a) ? "FAIL" : "OK");
    printf("query_ranks: %d (expect 5)\n", query_ranks(a));
    printf("Rank 5 count: %d (expect 0)\n", query_page_counts(5));
    printf("Rank 4 count: %d (expect 1)\n", query_page_counts(4));
    
    printf("\n=== Test 3: alloc rank 4 ===\n");
    void *b = alloc_pages(4);
    printf("alloc rank 4: %s\n", IS_ERR(b) ? "FAIL" : "OK");
    printf("query_ranks: %d (expect 4)\n", query_ranks(b));
    printf("Rank 4 count: %d (expect 0)\n", query_page_counts(4));
    
    printf("\n=== Test 4: alloc another rank 4 (should fail - no space) ===\n");
    void *c = alloc_pages(4);
    printf("alloc rank 4: err=%ld (expect %d)\n", PTR_ERR(c), -ENOSPC);
    
    printf("\n=== Test 5: return rank 4 block ===\n");
    int ret = return_pages(b);
    printf("return: %d (expect %d)\n", ret, OK);
    printf("Rank 4 count: %d (expect 1)\n", query_page_counts(4));
    
    printf("\n=== Test 6: double free detection ===\n");
    ret = return_pages(b);
    printf("double free: %d (expect %d)\n", ret, -EINVAL);
    
    printf("\n=== Test 7: invalid address ===\n");
    ret = return_pages(NULL);
    printf("return NULL: %d (expect %d)\n", ret, -EINVAL);
    ret = return_pages(p + 40 * 4096);
    printf("return out of bounds: %d (expect %d)\n", ret, -EINVAL);
    
    printf("\n=== Test 8: return rank 5 -> merge to rank 6? ===\n");
    ret = return_pages(a);  // return rank 5 at p
    printf("return: %d (expect %d)\n", ret, OK);
    // After returning rank 5 and having rank 4 at p+32pages, they should merge to rank 6
    printf("Rank 4 count: %d (expect 0)\n", query_page_counts(4));
    printf("Rank 5 count: %d (expect 0)\n", query_page_counts(5));
    printf("Rank 6 count: %d (expect 1)\n", query_page_counts(6)); // 2^5=32+8=40? No: rank 5=32 + rank 4=8? 
    // Wait, rank 5 at p (32 pages) + rank 4 at p+32*4K (8 pages) = rank 5 + rank 4, can't merge because diff ranks!
    // Actually: rank 4 at p+32*4K is pages 32-39. But rank 5 at p is pages 0-31. 
    // The buddy of rank 5 at p is at p + 32*4K (pages 32-63). But we only have 40 pages. Buddy doesn't fully fit.
    // So no merge. rank 5 stays at p, rank 4 stays at p+32*4K.
    printf("Rank 5 count after return: %d (expect 1)\n", query_page_counts(5));
    printf("Rank 4 count after return: %d (expect 1)\n", query_page_counts(4));

    printf("\n=== Test 9: rank validation ===\n");
    void *d = alloc_pages(0);
    printf("alloc rank 0: err=%ld (expect %d)\n", PTR_ERR(d), -EINVAL);
    d = alloc_pages(17);
    printf("alloc rank 17: err=%ld (expect %d)\n", PTR_ERR(d), -EINVAL);
    
    printf("\n=== Test 10: query_page_counts invalid ===\n");
    printf("qpc rank 0: %d (expect %d)\n", query_page_counts(0), -EINVAL);
    printf("qpc rank 17: %d (expect %d)\n", query_page_counts(17), -EINVAL);
    
    printf("\n=== Test 11: query_ranks on free page interior ===\n");
    printf("query_ranks(p): %d (expect 5)\n", query_ranks(p));
    printf("query_ranks(p+4K): %d (expect 5)\n", query_ranks(p + 1*4096));
    
    printf("\n=== Test 12: alloc smaller from larger free block ===\n");
    void *e = alloc_pages(2); // rank 2 from rank 5 block
    printf("alloc rank 2: %s\n", IS_ERR(e) ? "FAIL" : "OK");
    printf("query_ranks(e): %d (expect 2)\n", query_ranks(e));
    printf("Rank 2 count: %d (expect 0)\n", query_page_counts(2));
    printf("Rank 3 count: %d (expect 1)\n", query_page_counts(3)); // buddy from split
    printf("Rank 4 count: %d (expect 1)\n", query_page_counts(4)); // still there
    printf("Rank 5 count: %d (expect 0)\n", query_page_counts(5)); // was split
    
    printf("\n=== Test 13: return rank 2 -> merge back ===\n");
    ret = return_pages(e); // return rank 2 at p
    printf("return: %d (expect %d)\n", ret, OK);
    printf("Rank 5 count after merge: %d (expect 1)\n", query_page_counts(5));
    
    printf("\nDone.\n");
    free(p);
    return 0;
}
