#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/common.h"
#include "shm-sync-common.h"


void communicate(struct SyncMap* sync,
                 struct Arguments* args) {

    struct Benchmarks bench;
    int message;
    void* buffer = malloc(args->size);

    // Wait for signal from client
    sync_wait(sync->server);
    setup_benchmarks(&bench);

    for (message = 0; message < args->count; ++message) {
        bench.single_start = now();

        //printf("1\n");

        // Write into the memory
        memset(sync->shared_memory, '1', args->size);

        sync_notify(sync->client);
        sync_wait(sync->server);

        //printf("2\n");

        // Read
        memcpy(buffer, sync->shared_memory, args->size);

        sync_notify(sync->client);

        //printf("3\n");

        benchmark(&bench);
    }

    evaluate(&bench, args);

    sleep(1);
    free(buffer);
}

int main(int argc, char* argv[]) {

    // The synchronization object
    struct SyncMap sync;

    // Fetch command-line arguments
    struct Arguments args;
    parse_arguments(&args, argc, argv);

    open_segment(&sync, "shm-sync", &args);

    communicate(&sync, &args);

    cleanup(&sync, &args);

    return EXIT_SUCCESS;
}
