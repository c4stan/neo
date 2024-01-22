#include "tk_worker.h"

#include <std_queue.h>

typedef struct {
    std_queue_shared_t deque;
} tk_worker_context_t;

typedef struct {
    std_queue_shared_t
} tk_worker_state_t;

