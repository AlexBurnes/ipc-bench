#ifndef SHM_SYNC_COMMON_H
#define SHM_SYNC_COMMON_H

#include <pthread.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

struct Arguments;

struct SyncMutex {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int count;
};

// what to be in shared memory
struct SyncMem {
    //void * sizeof arg size
    struct SyncMutex mutex;
};

// and what to be in application
struct SyncMap {
    int shm_fd;
    void *shared_memory;
    struct SyncMutex *mutex;
};

void init_sync(struct SyncMap* sync);

void destroy_sync(struct SyncMap* sync);

void sync_wait(struct SyncMutex* sync);

void sync_notify(struct SyncMutex* sync);

int cleanup(struct SyncMap *sync, struct Arguments* args);

int create_segment(struct Arguments* args);

int open_segment(struct SyncMap *sync, char const *name, struct Arguments* args);

void* attach_segment(int segment_id, struct Arguments* args);

struct Sync* create_sync(void* shared_memory, struct Arguments* args);

#endif /* SHM_SYNC_COMMON_H */
