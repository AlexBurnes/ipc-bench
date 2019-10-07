#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/common.h"
#include "shm-sync-common.h"

void communicate(struct SyncMap* sync,
                 struct Arguments* args
                ) {
    // Buffer into which to read data
    void* buffer = malloc(args->size);

    sync_notify(sync->server);

    //printf("4\n");

    for (; args->count > 0; --args->count) {
        sync_wait(sync->client);

        //printf("5\n");

        // Read from memory
        memcpy(buffer, sync->shared_memory, args->size);
        // Write back
        memset(sync->shared_memory, '2', args->size);

        //printf("6\n");

        sync_notify(sync->server);

        //printf("7\n");
    }

    free(buffer);
}

int main(int argc, char* argv[]) {

    // The synchronization object
    struct SyncMap sync;

    // Fetch command-line arguments
    struct Arguments args;
    parse_arguments(&args, argc, argv);

    sleep(1);

    open_segment(&sync, "shm-sync", &args);

    communicate(&sync, &args);

    cleanup(&sync, &args);

    return EXIT_SUCCESS;
}
