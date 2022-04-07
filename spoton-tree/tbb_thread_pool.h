#ifndef SPOTON_TREE_TBB_TREAHD_POOL_H
#define SPOTON_TREE_TBB_TREAHD_POOL_H

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

#include <functional>

namespace spoton {

class ThreadPool {
    using TPCallbackFunc = std::function<void ()>;

private:
    tbb::task_arena limited;
    tbb::task_group tg;

public:
    ThreadPool (int thread_count) : limited (thread_count) {}

    void submit (TPCallbackFunc fn) {
        limited.execute ([&] { tg.run (fn); });
    }

    void wait_for_tasks () {
        // Wait for completion of the task group in the limited arena.
        limited.execute ([&] { tg.wait (); });
    }
};
};  // namespace spoton
#endif