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
    actor_id_t max_size;
    message_t* messages;
} message_queue_t;

typedef struct actor {
    role_t* role;
    void** stateptr;
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

    actor_t** actors;
    actor_id_t actor_array_size;

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

int resize_message_queue(message_queue_t* message_queue) {
    if (message_queue->max_size == ACTOR_QUEUE_LIMIT) {
        return -1;
    }
    actor_id_t old_size = message_queue->max_size;

    if (message_queue->max_size * 2 > ACTOR_QUEUE_LIMIT) {
        message_queue->max_size = ACTOR_QUEUE_LIMIT;
    } else {
        message_queue->max_size *= 2;
    }

    message_queue->messages = realloc(message_queue->messages, message_queue->max_size);
    if (!message_queue->messages) {
        printf("Memory error"); // todo handle
        return -1;
    }


    // todo does not work if new_size < 2 * old_size
    if (message_queue->first_index > message_queue->second_index) {
        for (int i = 0; i < message_queue->first_index; ++i) {
            message_queue->messages[i + old_size] = message_queue->messages[i];
        }
        message_queue->second_index = message_queue->second_index + old_size;
    }
    return 0;

}

int add_message_to_queue(message_queue_t* message_queue, message_t message) { // We have locked mutex for actor here
    if ((message_queue->second_index + 1) % message_queue->max_size ==
            message_queue->first_index) { // No empty spaces in queue

        if (resize_message_queue(message_queue) != 0) {
            return -1;
        }
    }

    message_queue->messages[message_queue->second_index] = message;
    message_queue->second_index = (message_queue->second_index + 1) % message_queue->max_size;
    return 0;
}

int send_message(actor_id_t actor, message_t message) {
    lock(&actor_system_data.variables_mutex);

    if (actor < 1 || actor > actor_system_data.how_many_actors) {
        unlock(&actor_system_data.variables_mutex);
        return -2;
    }

    actor_t* actor_ptr = actor_system_data.actors[actor];
    lock(&actor_ptr->actor_mutex);

    if (actor_ptr->is_dead) {
        unlock(&actor_ptr->actor_mutex);
        return -1;
    }

    int result = add_message_to_queue(&actor_ptr->message_queue, message);

    unlock(&actor_ptr->actor_mutex);
    unlock(&actor_system_data.variables_mutex);
    return result;
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

void resize_actor_array() {
    if (actor_system_data.actor_array_size * 2 > CAST_LIMIT + 1) { // +1 because we index actors from 1
        actor_system_data.actor_array_size = CAST_LIMIT + 1;
    } else {
        actor_system_data.actor_array_size *= 2;
    }
    actor_system_data.actors = realloc(actor_system_data.actors, actor_system_data.actor_array_size);
    if (!actor_system_data.actors) {
        printf("Memory error"); // todo handle, syserr probably
        return;
    }

}

actor_t* create_actor(role_t* role) {
    lock(&actor_system_data.variables_mutex);

    if (actor_system_data.how_many_actors + 1 > CAST_LIMIT) {
        //todo too many actors
    }
    if (actor_system_data.how_many_actors + 1 == actor_system_data.actor_array_size) {
        resize_actor_array();
    }

    actor_t* actor = malloc(sizeof(actor_t));
    if (actor == NULL) { // todo change to !actor
        // todo syserr
    }

    actor->message_queue.messages = malloc(sizeof(message_t) * INITIAL_MESSAGE_QUEUE_LIMIT);
    if (actor->message_queue.messages == NULL) {
        // todo syserr
    }

    if (pthread_mutex_init(&actor->actor_mutex, 0) != 0) {
        // todo syserr
    }

    actor->message_queue.first_index = 0;
    actor->message_queue.second_index = 0;
    actor->message_queue.max_size = INITIAL_MESSAGE_QUEUE_LIMIT;

    actor->is_dead = false;

    actor_system_data.how_many_actors++;

    actor->actor_id = actor_system_data.how_many_actors;
    actor_system_data.actors[actor_system_data.how_many_actors] = actor;
    actor->role = role;
    actor->stateptr = NULL;

    unlock(&actor_system_data.variables_mutex);

    return actor;
}

actor_id_t actor_id_self() {
    actor_id_t* actor_id_ptr = pthread_getspecific(actor_id_key);
    if (!actor_id_ptr) {
        // todo error
    }
    return *actor_id_ptr;
}

message_t get_message(actor_t* actor_ptr) {
    lock(&actor_ptr->actor_mutex);
    message_t message = actor_ptr->message_queue.messages[actor_ptr->message_queue.first_index];
    actor_ptr->message_queue.first_index =
            (actor_ptr->message_queue.first_index + 1) % actor_ptr->message_queue.max_size;

    unlock(&actor_ptr->actor_mutex);

    return message;
}

void handle_godie(actor_t* actor_ptr) {
    lock(&actor_ptr->actor_mutex);

    actor_ptr->is_dead = true;

    unlock(&actor_ptr->actor_mutex);
}

void handle_spawn(actor_t* actor_ptr, message_t message) {
    role_t* role = (role_t*) message.data;

    actor_t* new_actor = create_actor(role);
    message_t new_message;

    new_message.message_type = MSG_HELLO;
    new_message.data = (void*) actor_ptr->actor_id; //todo address or value?;
    new_message.nbytes = sizeof(actor_id_t);

    send_message(new_actor->actor_id, new_message);
}

void handle_actor(actor_t* actor_ptr) {
    pthread_setspecific(actor_id_key, &actor_ptr->actor_id);

    message_t message = get_message(actor_ptr);

    if (message.message_type == MSG_GODIE) {
        handle_godie(actor_ptr);
    } else if (message.message_type == MSG_SPAWN) {
        handle_spawn(actor_ptr, message);
    } else {
        role_t* role = actor_ptr->role;
        act_t* prompts = role->prompts;
        prompts[message.message_type](actor_ptr->stateptr, message.nbytes, message.data);

    }


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
            actor_system_data.messages_to_handle = true;
        } else if (actor_to_handle->is_dead) {
            if (actor_system_data.dead_actors == -1) {
                actor_system_data.dead_actors = 0;
            }
            actor_system_data.dead_actors++;
        }

        unlock(&actor_to_handle->actor_mutex);
        unlock(&actor_system_data.variables_mutex);

    }

    return NULL; // TODO good function type?
}

int initialize_queue(actor_queue_t* queue) {
    queue->first_index = 0;
    queue->second_index = 0;
    queue->max_size = INITIAL_ACTOR_QUEUE_LIMIT;
    queue->actors = malloc(sizeof(actor_t*) * INITIAL_ACTOR_QUEUE_LIMIT);
    if (!queue->actors) {
        return -1;
    }
    return 0;
}

int initialize_system_data() {
    actor_system_data.messages_to_handle = false;
    actor_system_data.how_many_actors = 0;
    actor_system_data.dead_actors = -2;

    actor_system_data.actors = malloc(sizeof(actor_t*) * INITIAL_ACTOR_QUEUE_LIMIT);
    if (!actor_system_data.actors) {
        return -1;
    }
    actor_system_data.actor_array_size = INITIAL_ACTOR_QUEUE_LIMIT;

    return initialize_queue(&actor_system_data.actors_with_messages);
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


    pthread_key_create(&actor_id_key, NULL);

    if (pthread_cond_init(&actor_system_data.waiting_threads, 0) != 0) {
        return -1;
    }

    if (pthread_mutex_init(&actor_system_data.variables_mutex, 0) != 0) {
        return -1;
    }

    if (initialize_system_data() < 1) {
        // todo cleanup memory
        return -1;
    }

    for (int i = 0; i <= POOL_SIZE - 1; ++i) {
//        pthread_attr_t attr; // todo necessary?
        if (pthread_create(&actor_system_data.thread_array[i], NULL, thread_run, NULL) != 0) {
            return -1; // todo memory cleanup
        }
    }
    actor_t* first_actor = create_actor(role);
    actor = &first_actor->actor_id;

    message_t new_message;
    new_message.message_type = MSG_HELLO;
    new_message.data = NULL;
    new_message.nbytes = sizeof(actor_id_t);
    send_message(*actor, new_message);

    return 0;
}

void actor_system_join(actor_id_t actor) {
    lock(&actor_system_data.variables_mutex);





    //todo cleanup memory


}