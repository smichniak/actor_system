#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "err.h"

// TODO Optimize includes


#include "cacti.h"

#define INITIAL_QUEUE_LIMIT 1024

typedef struct actor {
    actor_id_t actor_id;
    message_t* message_queue; // Array with messages to actor
    pthread_mutex_t mutex;

} actor_t;

typedef struct actor_system {
    pthread_mutex_t mutex;
    pthread_mutex_t waiting_threads;

//    message_t** actor_queues;
} actor_system_t;

void thread_run(actor_system_t* actor_system_data_ptr) {
    int err;
    if ((err = pthread_mutex_lock(&actor_system_data_ptr->waiting_threads)) != 0) {
        syserr(err, "lock failed"); //todo
    }

    while (1) {
        // todo
    }
}

int actor_system_create(actor_id_t* actor, role_t* const role) {
    actor_system_t* actor_system_data_ptr;
    actor_system_data_ptr = (actor_system_t*) malloc(sizeof(actor_system_t));
    if (!actor_system_data_ptr) {
        return -1;
    }


    if (pthread_mutex_init(&actor_system_data_ptr->mutex, 0) != 0) {
        return -1;
    }

    if (pthread_mutex_init(&actor_system_data_ptr->waiting_threads, 0) != 0) {
        return -1;
    }

    for (int i = 1; i <= POOL_SIZE; ++i) {
        switch (fork()) {
            case -1:
                return -1; // TODO handle

            case 0: // Child process
                thread_run(actor_system_data_ptr);
                return 0;

            default: // Parent process
                break;
        }
    }
    return 0;
}