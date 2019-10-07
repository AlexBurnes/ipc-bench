#include <string.h>

#include "common/arguments.h"
#include "common/utility.h"
#include "shm-sync-common.h"


void init_sync(struct SyncMap * sync) {
    // These structures are used to initialize mutexes
    // and condition variables. We will use them to set
    // the PTHREAD_PROCESS_SHARED attribute, which enables
    // more than one process (and any thread in any of those
    // processes) to access the mutex and condition variable.
    pthread_mutexattr_t mutex_attributes;
    pthread_condattr_t condition_attributes;

    sync->mutex->count = 0;

    // These methods initialize the attribute structures
    // with default values so that we must only change
    // the one we are interested in.
    if (pthread_mutexattr_init(&mutex_attributes) != 0) {
        throw("Error initializing mutex attributes");
    }
    if (pthread_condattr_init(&condition_attributes) != 0) {
        throw("Error initializing condition variable attributes");
    }

    // Here we set the "process-shared"-attribute of the mutex
    // and condition variable to PTHREAD_PROCESS_SHARED. This
    // means multiple processes may access these objects. If
    // we wouldn't do this, the attribute would be PTHREAD_PROCESS
    // _PRIVATE, where only threads created by the process who
    // initialized the objects would be allowed to access them.
    // By passing PTHREAD_PROCESS_SHARED the objects may also live
    // longer than the process creating them.
    // clang-format off
    if (pthread_mutexattr_setpshared(
                &mutex_attributes, PTHREAD_PROCESS_SHARED) != 0) {
        throw("Error setting process-shared attribute for mutex");
    }

    if (pthread_condattr_setpshared(
                &condition_attributes, PTHREAD_PROCESS_SHARED) != 0) {
        throw("Error setting process-shared attribute for condition variable");
    }
    // clang-format on

    // Initialize the mutex and condition variable and pass the attributes
    if (pthread_mutex_init(&sync->mutex->mutex, &mutex_attributes) != 0) {
        throw("Error initializing mutex");
    }
    if (pthread_cond_init(&sync->mutex->condition, &condition_attributes) != 0) {
        throw("Error initializing condition variable");
    }

    // Destroy the attribute objects
    if (pthread_mutexattr_destroy(&mutex_attributes)) {
        throw("Error destroying mutex attributes");
    }
    if (pthread_condattr_destroy(&condition_attributes)) {
        throw("Error destroying condition variable attributes");
    }
}

void destroy_sync(struct SyncMap* sync) {
    if (pthread_mutex_destroy(&sync->mutex->mutex)) {
        throw("Error destroying mutex");
    }
    if (pthread_cond_destroy(&sync->mutex->condition)) {
        throw("Error destroying condition variable");
    }
}

void sync_wait(struct SyncMutex* sync) {
    // Lock the mutex
    //printf("[%d] wait lock\n", getpid());
    if (pthread_mutex_lock(&sync->mutex) != 0) {
        throw("Error locking mutex");
    }

    // Move into waiting for the condition variable to be signalled.
    // For this, it is essential that the mutex first be locked (above)
    // to avoid data races on the condition variable (e.g. the signal
    // being sent before the waiting process has begun). In fact, behaviour
    // is undefined otherwise. Once the mutex has begun waiting, the mutex
    // is unlocked so that other threads may do something and eventually
    // signal the condition variable. At that point, this thread wakes up
    // and *re-acquires* the lock immediately. As such, when this method
    // returns the lock will be owned by the calling thread.
    while (sync->count == 0) {
        //printf("[%d] wait count %d\n", getpid(), sync->count);
        if (pthread_cond_wait(&sync->condition, &sync->mutex) != 0) {
            throw("Error waiting for condition variable");
        }
    }
    sync->count--;

    //printf("[%d] wait unlock\n", getpid());
    if (pthread_mutex_unlock(&sync->mutex) != 0) {
        throw("Error locking mutex");
    }
}

void sync_notify(struct SyncMutex* sync) {
    // Signals to a single thread waiting on the condition variable
    // to wake up, if any such thread exists. An alternative would be
    // to call pthread_cond_broadcast, in which case *all* waiting
    // threads would be woken up.
    //printf("[%d] notify lock\n", getpid());
    if (pthread_mutex_lock(&sync->mutex) != 0) {
        throw("Error locking mutex");
    }
    //printf("[%d] notify count %d\n", getpid(), sync->count);
    if (sync->count == 0)
        if (pthread_cond_signal(&sync->condition) != 0) {
            throw("Error signalling condition variable");
        }
    sync->count++;
    //printf("[%d] notify unlock\n", getpid());
    pthread_mutex_unlock(&sync->mutex);
}

int cleanup(struct SyncMap *sync, struct Arguments* args) {

    if (munmap((void *)sync->shared_memory, args->size + sizeof(struct SyncMem))) {
        perror("munmap shared memory");
        return -1;
    }

    if (close(sync->shm_fd)) {
        perror("close");
        return -1;
    }

    return 0;

}


#include <stdio.h>

int create_segment(struct Arguments* args) {
    // The identifier for the shared memory segment
    int segment_id;

    // Key for the memory segment
    key_t segment_key = generate_key("shm");

    // The size for the segment
    int size = args->size + sizeof(*(struct SyncMem*)0);

    /*
        The call that actually allocates the shared memory segment.
        Arguments:
            1. The shared memory key. This must be unique across the OS.
            2. The number of bytes to allocate. This will be rounded up to the OS'
                 pages size for alignment purposes.
            3. The creation flags and permission bits, where:
                 - IPC_CREAT means that a new segment is to be created
                 - IPC_EXCL means that the call will fail if
                     the segment-key is already taken (removed)
                 - 0666 means read + write permission for user, group and world.
        When the shared memory key already exists, this call will fail. To see
        which keys are currently in use, and to remove a certain segment, you
        can use the following shell commands:
            - Use `ipcs -m` to show shared memory segments and their IDs
            - Use `ipcrm -m <segment_id>` to remove/deallocate a shared memory segment
    */
    segment_id = shmget(segment_key, size, IPC_CREAT | 0666);

    if (segment_id < 0) {
        throw("Error allocating segment");
    }

    return segment_id;
}

int open_segment(struct SyncMap *sync, char const *name, struct Arguments* args) {
    int mutex_created = 1;

    memset(sync, 0, sizeof(struct SyncMap));

    sync->shm_fd = shm_open(name, O_RDWR|O_CREAT|O_EXCL, 0660);
    if (errno == EEXIST) {
        //printf("already exists '%s'\n", name);
        sync->shm_fd = shm_open(name, O_RDWR, 0660);
        mutex_created = 0;
    }
    if (-1 == sync->shm_fd) {
        perror("shm_open");
        return -1;
    }

    //printf("open shared memory '%s' [%d]\n", name, sync->shm_fd);

    size_t size = sizeof(struct SyncMem) + args->size;

    if (0 != ftruncate(sync->shm_fd, size)) {
        perror("ftruncate");
        return -1;
    }
    // Map pthread mutex into the shared memory.

    void *addr;

    //printf("mmap size of %ld\n", size);
    addr = mmap(
        NULL,
        size,
        PROT_READ|PROT_WRITE,
        MAP_SHARED,
        sync->shm_fd,
        0
    );

    if (addr == MAP_FAILED) {
        perror("mmap shared_memory");
        return -1;
    }

    sync->shared_memory = addr;
    sync->mutex        = (struct SyncMutex *) ((char *)addr + args->size);

    //printf("empty shared memory\n");
    memset(sync->shared_memory, 0, args->size);

    if (mutex_created) {
        //printf("create mutex\n");
        init_sync(sync);
    }
    //printf("init done\n");
    return 0;
}
