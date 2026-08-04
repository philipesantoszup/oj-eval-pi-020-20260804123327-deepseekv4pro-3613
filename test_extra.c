#include <stdio.h>
#include <stdlib.h>
#include "buddy.h"

/* Test that checks cache consistency */
int main() {
    void *p = malloc(32768 * 4096);
    int ret = init_page(p, 32768);
    printf("init: %d\n", ret);
    
    /* Phase A: allocate all pages */
    void *addrs[32768];
    for (int i = 0; i < 32768; i++) {
        addrs[i] = alloc_pages(1);
        if (IS_ERR(addrs[i])) {
            printf("FAIL: alloc %d failed\n", i);
            return 1;
        }
    }
    printf("Alloc all: OK\n");
    
    /* Phase B: return and verify counts */
    for (int i = 0; i < 32768; i++) {
        ret = return_pages(addrs[i]);
        if (ret != OK) {
            printf("FAIL: return %d failed: %d\n", i, ret);
            return 1;
        }
    }
    printf("Return all: OK\n");
    
    /* Phase C: verify final state */
    printf("qpc 16: %d (expect 1)\n", query_page_counts(16));
    printf("qpc 1-15: ");
    for (int r = 1; r <= 15; r++)
        printf("%d ", query_page_counts(r));
    printf("(expect all 0)\n");
    
    printf("query_ranks(p): %d (expect 16)\n", query_ranks(p));
    
    /* Phase D: alloc and return different ranks */
    void *a = alloc_pages(5);
    printf("alloc rank 5: %s\n", IS_ERR(a) ? "FAIL" : "OK");
    printf("qpc 5: %d (expect 0)\n", query_page_counts(5));
    printf("qpc 15: %d (expect 1)\n", query_page_counts(15));
    
    void *b = alloc_pages(3);
    printf("alloc rank 3: %s\n", IS_ERR(b) ? "FAIL" : "OK");
    
    void *c = alloc_pages(8);
    printf("alloc rank 8: %s\n", IS_ERR(c) ? "FAIL" : "OK");
    
    printf("return rank 3: %d\n", return_pages(b));
    printf("return rank 8: %d\n", return_pages(c));
    printf("return rank 5: %d\n", return_pages(a));
    
    printf("Final qpc 16: %d (expect 1)\n", query_page_counts(16));
    
    /* Invalid tests */
    printf("alloc 0: %ld\n", PTR_ERR(alloc_pages(0)));
    printf("alloc 17: %ld\n", PTR_ERR(alloc_pages(17)));
    printf("qpc 0: %d\n", query_page_counts(0));
    printf("qpc 17: %d\n", query_page_counts(17));
    printf("return NULL: %d\n", return_pages(NULL));
    printf("query NULL: %d\n", query_ranks(NULL));
    
    free(p);
    return 0;
}
