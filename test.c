#include "radix.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int failed = 0;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL: %s:%d\n", __FILE__, __LINE__); failed++; } \
} while (0)

int main(void) {
    RadixTree t;
    radix_init(&t);

    CHECK(radix_size(&t) == 0);
    CHECK(radix_search(&t, "a", 1) == NULL);

    CHECK(radix_insert(&t, "hello", 5, (void*)(uintptr_t)1) == 0);
    CHECK(radix_insert(&t, "world", 5, (void*)(uintptr_t)2) == 0);
    CHECK(radix_insert(&t, "hell", 4, (void*)(uintptr_t)3) == 0);
    CHECK(radix_insert(&t, "help", 4, (void*)(uintptr_t)4) == 0);
    CHECK(radix_insert(&t, "abc", 3, (void*)(uintptr_t)5) == 0);

    CHECK(radix_size(&t) == 5);

    CHECK((uintptr_t)radix_search(&t, "hello", 5) == 1);
    CHECK((uintptr_t)radix_search(&t, "world", 5) == 2);
    CHECK((uintptr_t)radix_search(&t, "hell", 4) == 3);
    CHECK((uintptr_t)radix_search(&t, "help", 4) == 4);
    CHECK((uintptr_t)radix_search(&t, "abc", 3) == 5);
    CHECK(radix_search(&t, "hel", 3) == NULL);
    CHECK(radix_search(&t, "hellox", 6) == NULL);

    /* replace */
    CHECK(radix_insert(&t, "hello", 5, (void*)(uintptr_t)99) == 0);
    CHECK((uintptr_t)radix_search(&t, "hello", 5) == 99);
    CHECK(radix_size(&t) == 5);

    /* delete */
    CHECK(radix_delete(&t, "world", 5) == 0);
    CHECK(radix_search(&t, "world", 5) == NULL);
    CHECK(radix_size(&t) == 4);

    CHECK(radix_delete(&t, "missing", 7) == -1);

    /* more keys to force node growth */
    char buf[16];
    for (int i = 0; i < 40; i++) {
        int len = snprintf(buf, sizeof(buf), "key%02d", i);
        CHECK(radix_insert(&t, buf, (size_t)len, (void*)(uintptr_t)(100 + i)) == 0);
    }
    CHECK(radix_size(&t) == 44);

    for (int i = 0; i < 40; i++) {
        int len = snprintf(buf, sizeof(buf), "key%02d", i);
        CHECK((uintptr_t)radix_search(&t, buf, (size_t)len) == (uintptr_t)(100 + i));
    }

    radix_free(&t);
    CHECK(radix_size(&t) == 0);

    if (failed == 0) {
        printf("all tests passed\n");
        return 0;
    } else {
        printf("%d checks failed\n", failed);
        return 1;
    }
}
