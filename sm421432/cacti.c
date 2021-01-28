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
// TODO make functions static, so they are not visible


#include "cacti.h"

#define INITIAL_MESSAGE_QUEUE_LIMIT 1024
#define INITIAL_ACTOR_QUEUE_LIMIT 128

typedef struct message_queue {
    actor_id_t first_index;
    actor_id_t second_index;
    message_t* messages;
} message_queue_t;

typedef struct actor {
    pthread_mutex_t actor_mutex;
    actor_id_t actor_id;
    bool is_dead;
    message_queue_t message_queue;
} actor_t;

typedef struct actor_queue {
    actor_id_t first_index;
    actor_id_t second_index;
    actor_id_t max_size;
    actor_t** actors;
} actor_queue_t;

typedef struct actor_system {
    pthread_mutex_t variables_mutex;
    pthread_cond_t waiting_threads;
    pthread_t thread_array[POOL_SIZE];
    bool messages_to_handle;
    actor_id_t how_many_actors;
    actor_id_t dead_actors;

    actor_t* actors;

    actor_queue_t actors_with_messages;

} actor_system_t;

actor_system_t actor_system_data; // Global data structure for actor system
pthread_key_t actor_id_key; // Global thread specific key for storing actor that is currently being handled

void lock(pthread_mutex_t* mutex) {
    int err;
    if ((err = pthread_mutex_lock(mutex)) != 0) {
        syserr(err, "Lock failed");
    }
}

void unlock(pthread_mutex_t* mutex) {
    int err;
    if ((err = pthread_mutex_unlock(mutex)) != 0) {
        syserr(err, "Unlock failed");
    }
}

bool system_alive() {
    lock(&actor_system_data.variables_mutex);

    bool result = actor_system_data.how_many_actors == actor_system_data.dead_actors;

    unlock(&actor_system_data.variables_mutex);

    return result;
}

void resize_actor_queue(actor_queue_t* actor_queue) {
    actor_queue->max_size = actor_queue->max_size * 2;
    actor_queue->actors = realloc(actor_queue->actors, actor_queue->max_size);
    if (!actor_queue->actors) {
        printf("Memory error"); // todo handle, syserr probably
        return;
    }

    if (actor_queue->first_index > actor_queue->second_index) {
        for (int i = 0; i < actor_queue->first_index; ++i) {
            actor_queue->actors[i + actor_queue->max_size / 2] = actor_queue->actors[i];
        }
        actor_queue->second_index = actor_queue->second_index + actor_queue->max_size / 2;
    }

}

void add_actor_to_queue(actor_queue_t* actor_queue, actor_t* actor_ptr) { // We have locked mutex for this queue
    if ((actor_queue->second_index + 1) % actor_queue->max_size ==
        actor_queue->first_index) { // No empty spaces in queue
        resize_actor_queue(actor_queue);
    }

    actor_queue->actors[actor_queue->second_index] = actor_ptr;
    actor_queue->second_index = (actor_queue->second_index + 1) % actor_queue->max_size;

}

actor_id_t actor_id_self() {
    actor_id_t* actor_id_ptr = pthread_getspecific(actor_id_key);
    if (!actor_id_ptr) {
        // todo error
    }
    return *actor_id_ptr;
}

void handle_actor(actor_t* actor_ptr) {
    pthread_setspecific(actor_id_key, &actor_ptr->actor_id);
    

}

void* thread_run() {
    int err;

    while (system_alive()) {

        lock(&actor_system_data.variables_mutex);

        while (!actor_system_data.messages_to_handle) { // Waits until there are messages for actors
            if ((err = pthread_cond_wait(&actor_system_data.waiting_threads,
                                         &actor_system_data.variables_mutex)) != 0) {
                syserr(err, "Cond wait failed");
            }
            if (actor_system_data.how_many_actors == actor_system_data.dead_actors) {
                return NULL;
            }
        }
        actor_id_t first_actor_index = actor_system_data.actors_with_messages.first_index;
        actor_t* actor_to_handle = actor_system_data.actors_with_messages.actors[first_actor_index];

        actor_system_data.actors_with_messages.first_index =
                (1 + actor_system_data.actors_with_messages.first_index) %
                actor_system_data.actors_with_messages.max_size;

        actor_system_data.messages_to_handle = actor_system_data.actors_with_messages.first_index !=
                                               actor_system_data.actors_with_messages.second_index;

        unlock(&actor_system_data.variables_mutex);

        handle_actor(actor_to_handle);

        lock(&actor_system_data.variables_mutex);
        lock(&actor_to_handle->actor_mutex);

        if (actor_to_handle->message_queue.first_index != actor_to_handle->message_queue.second_index) {
            add_actor_to_queue(&actor_system_data.actors_with_messages, actor_to_handle);
        } else if (actor_to_handle->is_dead) {
            actor_system_data.dead_actors++;
        }

        unlock(&actor_to_handle->actor_mutex);
        unlock(&actor_system_data.variables_mutex);

    }

    return NULL; // TODO good function type?
}

void initialize_queue(actor_queue_t* queue) {
    queue->first_index = 0;
    queue->second_index = 0;
    queue->max_size = INITIAL_MESSAGE_QUEUE_LIMIT;
    queue->actors = malloc(sizeof(actor_t*) * INITIAL_MESSAGE_QUEUE_LIMIT);
}

void initialize_system_data() {
    actor_system_data.messages_to_handle = false;
    actor_system_data.how_many_actors = 1;
    actor_system_data.dead_actors = 0;

    actor_system_data.actors = malloc(sizeof(actor_t) * INITIAL_ACTOR_QUEUE_LIMIT);

    initialize_queue(&actor_system_data.actors_with_messages);
}

int actor_system_create(actor_id_t* actor, role_t* const role) {
    // todo new thread fo handling signals
    /*
     * czyli chyba trzeba najpierw zrobić ten 1 wątek do sygnałów bez zmieniania maski
        potem zmienić maskę sygnałów
        a potem zrobić POOL_SIZE wątków z nową maską
        poza tym, pytania na moodlu są do północy
        więc jak mamy jakieś, to trzeba je zadawać asap
        It is possible to create a new thread with a specific signal mask without using these functions. On the thread that calls pthread_create, the required steps for the general case are:
        Mask all signals, and save the old signal mask, using pthread_sigmask. This ensures that the new thread will be created with all signals masked, so that no signals can be delivered to the thread until the desired signal mask is set.
        Call pthread_create to create the new thread, passing the desired signal mask to the thread start routine (which could be a wrapper function for the actual thread start routine). It may be necessary to make a copy of the desired signal mask on the heap, so that the life-time of the copy extends to the point when the start routine needs to access the signal mask.
        Restore the thread’s signal mask, to the set that was saved in the first step
     */

//    if (pthread_mutex_init(&actor_system_data.thread_mutex, 0) != 0) {
//        return -1;
//    }

    pthread_key_create(&actor_id_key, NULL);

    if (pthread_cond_init(&actor_system_data.waiting_threads, 0) != 0) {
        return -1;
    }

    if (pthread_mutex_init(&actor_system_data.variables_mutex, 0) != 0) {
        return -1;
    }

    initialize_system_data();

    // todo handle memory leak if return -1

    for (int i = 0; i <= POOL_SIZE - 1; ++i) {
//        pthread_attr_t attr; // todo necessary?
        if (pthread_create(&actor_system_data.thread_array[i], NULL, thread_run, NULL) != 0) {
            return -1; // todo memory
        }
    }
    // todo send message to first actor

    // todo set actor to actor id

    return 0;
}