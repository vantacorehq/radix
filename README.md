# radix

Pure C adaptive radix tree (ART).

Ordered in-memory index. Zero dependencies.

Supports insert, search, delete, and ordered iteration.

## Build

```bash
cc -std=c11 -Wall -Wextra -O2 -o test test.c radix.c
./test
```

## Usage

```c
#include "radix.h"

RadixTree t;
radix_init(&t);

radix_insert(&t, "hello", 5, (void*)(uintptr_t)42);
void *v = radix_search(&t, "hello", 5);

radix_free(&t);
```

Keys are arbitrary byte sequences. Values are opaque pointers.
