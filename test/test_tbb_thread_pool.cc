#include "spoton-tree/tbb_thread_pool.h"

int main () {
    spoton::ThreadPool tp (4);

    for (int i = 0; i < 100; i++) {
        tp.submit ([i] () {
            // submit a job
            printf ("job %d\n", i);
        });
    }

    printf ("===== all job submitted\n");

    tp.wait_for_tasks ();
    return 0;
}