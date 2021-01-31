#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>

#include "err.h"
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
    void* stateptr;
    pthread_mutex_t actor_mutex;
    actor_id_t actor_id;
    bool is_dead;
    bool is_being_handled;
    bool is_in_queue;
    bool counted_as_dead;
    message_queue_t message_queue;
} actor_t;

typedef struct actor_queue {
    actor_id_t first_index;
    actor_id_t second_index;
    actor_id_t max_size;
    actor_id_t* actors;
} actor_queue_t;

typedef struct actor_system {
    pthread_mutex_t variables_mutex;
    pthread_cond_t waiting_threads;
    pthread_t thread_array[POOL_SIZE];
    pthread_t signal_handler;

    actor_id_t how_many_actors;
    actor_id_t dead_actors;
    bool can_create_new_actors;

    actor_t** actors;
    actor_id_t actor_array_size;

    actor_queue_t actors_with_messages;
} actor_system_t;

actor_system_t actor_system_data = { // Global data structure for actor system
        .variables_mutex = PTHREAD_MUTEX_INITIALIZER,
        .waiting_threads = PTHREAD_COND_INITIALIZER,
        .actors = NULL,
        .actors_with_messages.actors = NULL,
        .how_many_actors = 0,
        .dead_actors = 0
};

_Thread_local actor_id_t actor_id_thread; // Thread specific variable with id of actor being handled

static inline void lock(pthread_mutex_t* mutex) {
    int err;
    if ((err = pthread_mutex_lock(mutex)) != 0) {
        syserr(err, "Mutex lock error");
    }
}

static inline void unlock(pthread_mutex_t* mutex) {
    int err;
    if ((err = pthread_mutex_unlock(mutex)) != 0) {
        syserr(err, "Mutex unlock error");
    }
}

static int resize_message_queue(message_queue_t* message_queue) {
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
        exit(1); // Queue is invalid
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

static void resize_actor_queue(actor_queue_t* actor_queue) {
    actor_queue->max_size = actor_queue->max_size * 2;
    actor_queue->actors = realloc(actor_queue->actors, actor_queue->max_size);
    if (!actor_queue->actors) {
        exit(1); // Queue is invalid
    }

    if (actor_queue->first_index > actor_queue->second_index) {
        for (int i = 0; i < actor_queue->first_index; ++i) {
            actor_queue->actors[i + actor_queue->max_size / 2] = actor_queue->actors[i];
        }
        actor_queue->second_index = actor_queue->second_index + actor_queue->max_size / 2;
    }

}

static void add_actor_to_queue(actor_queue_t* actor_queue, actor_t* actor_ptr) {
    lock(&actor_ptr->actor_mutex);
    if (actor_ptr->is_being_handled || actor_ptr->is_in_queue) {
        unlock(&actor_ptr->actor_mutex);
        return;
    } else {
        actor_ptr->is_in_queue = true;
    }
    unlock(&actor_ptr->actor_mutex);

    lock(&actor_system_data.variables_mutex);
    if ((actor_queue->second_index + 1) % actor_queue->max_size ==
        actor_queue->first_index) { // No empty spaces in queue
        resize_actor_queue(actor_queue);
    }

    actor_queue->actors[actor_queue->second_index] = actor_ptr->actor_id; // Can access id without mutex
    actor_queue->second_index = (actor_queue->second_index + 1) % actor_queue->max_size;


    int err;
    // Wake up a thread that can handle the new message
    if ((err = pthread_cond_signal(&actor_system_data.waiting_threads)) != 0) {
        syserr(err, "Conditional signal error");
    }
    unlock(&actor_system_data.variables_mutex);
}

static int add_message_to_queue(message_queue_t* message_queue, message_t message) {
    // We have locked mutex for actor here

    if ((message_queue->second_index + 1) % message_queue->max_size == message_queue->first_index) {
        // No empty spaces in queue
        if (resize_message_queue(message_queue) != 0) {
            return -1;
        }
    }

    message_queue->messages[message_queue->second_index] = message;
    message_queue->second_index = (message_queue->second_index + 1) % message_queue->max_size;

    return 0;
}

// Returns -3 if actor queue is full
int send_message(actor_id_t actor, message_t message) {
    lock(&actor_system_data.variables_mutex);

    if (actor < 1 || actor > actor_system_data.how_many_actors) {
        unlock(&actor_system_data.variables_mutex);
        return -2;
    }

    actor_t* actor_ptr = actor_system_data.actors[actor];
    unlock(&actor_system_data.variables_mutex);

    lock(&actor_ptr->actor_mutex);

    if (actor_ptr->is_dead) {
        unlock(&actor_ptr->actor_mutex);
        return -1;
    }

    if (add_message_to_queue(&actor_ptr->message_queue, message) != 0) {
        unlock(&actor_ptr->actor_mutex);
        return -3;
    }

    unlock(&actor_ptr->actor_mutex);
    add_actor_to_queue(&actor_system_data.actors_with_messages, actor_ptr);

    return 0;
}

static bool system_alive() {
    lock(&actor_system_data.variables_mutex);

    bool result = actor_system_data.how_many_actors != actor_system_data.dead_actors;

    unlock(&actor_system_data.variables_mutex);

    return result;
}

static void resize_actor_array() {
    if (actor_system_data.actor_array_size * 2 > CAST_LIMIT + 1) { // +1 because we index actors from 1
        actor_system_data.actor_array_size = CAST_LIMIT + 1;
    } else {
        actor_system_data.actor_array_size *= 2;
    }
    actor_system_data.actors = realloc(actor_system_data.actors, actor_system_data.actor_array_size);
    if (!actor_system_data.actors) {
        exit(1); // Array is invalid
    }

}

// Returns NULL if creating actor failed (memory allocation error, cond initialization error, too many actors)
static actor_t* create_actor(role_t* role) {
    lock(&actor_system_data.variables_mutex);

    if (!actor_system_data.can_create_new_actors) {
        unlock(&actor_system_data.variables_mutex);
        return NULL;
    }

    if (actor_system_data.how_many_actors + 1 > CAST_LIMIT) {
        return NULL;
    }
    if (actor_system_data.how_many_actors + 1 == actor_system_data.actor_array_size) {
        resize_actor_array();
    }

    actor_t* actor = malloc(sizeof(actor_t));
    if (!actor) {
        return NULL;
    }

    actor->message_queue.messages = malloc(sizeof(message_t) * INITIAL_MESSAGE_QUEUE_LIMIT);
    if (actor->message_queue.messages == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&actor->actor_mutex, 0) != 0) {
        return NULL;
    }

    actor->message_queue.first_index = 0;
    actor->message_queue.second_index = 0;
    actor->message_queue.max_size = INITIAL_MESSAGE_QUEUE_LIMIT;

    actor->is_dead = false;
    actor->is_being_handled = false;
    actor->is_in_queue = false;
    actor->counted_as_dead = false;

    actor_system_data.how_many_actors++;

    actor->actor_id = actor_system_data.how_many_actors;
    actor_system_data.actors[actor_system_data.how_many_actors] = actor;
    actor->role = role;
    actor->stateptr = NULL;

    unlock(&actor_system_data.variables_mutex);
    return actor;
}

actor_id_t actor_id_self() {
    return actor_id_thread;
}

static message_t get_message(actor_t* actor_ptr) {
    lock(&actor_ptr->actor_mutex);
    message_t message = actor_ptr->message_queue.messages[actor_ptr->message_queue.first_index];
    actor_ptr->message_queue.first_index =
            (actor_ptr->message_queue.first_index + 1) % actor_ptr->message_queue.max_size;

    unlock(&actor_ptr->actor_mutex);

    return message;
}

static void check_fully_dead(actor_t* actor_ptr) {
    bool count = false;

    lock(&actor_ptr->actor_mutex);

    if (!actor_ptr->counted_as_dead && actor_ptr->is_dead &&
        actor_ptr->message_queue.first_index == actor_ptr->message_queue.second_index) {
        actor_ptr->counted_as_dead = true;
        count = true;
    }

    unlock(&actor_ptr->actor_mutex);

    if (count) {
        lock(&actor_system_data.variables_mutex);

        if (actor_system_data.dead_actors == -2) {
            actor_system_data.dead_actors = 0;
        }
        actor_system_data.dead_actors++;

        unlock(&actor_system_data.variables_mutex);
    }
}

static inline void handle_godie(actor_t* actor_ptr) {
    lock(&actor_ptr->actor_mutex);

    actor_ptr->is_dead = true;

    unlock(&actor_ptr->actor_mutex);
}

static void handle_spawn(actor_t* actor_ptr, message_t message) {
    role_t* role = (role_t*) message.data;

    actor_t* new_actor = create_actor(role);
    if (!new_actor) {
        return;
    }

    message_t new_message;

    new_message.message_type = MSG_HELLO;
    new_message.data = (void*) actor_ptr->actor_id;
    new_message.nbytes = sizeof(actor_id_t);

    send_message(new_actor->actor_id, new_message);
}

static void handle_actor(actor_t* actor_ptr) {
    actor_id_thread = actor_ptr->actor_id; // Can access without actor mutex

    message_t message = get_message(actor_ptr);

    if (message.message_type == MSG_GODIE) {
        handle_godie(actor_ptr);
    } else if (message.message_type == MSG_SPAWN) {
        handle_spawn(actor_ptr, message);
    } else {
        role_t* role = actor_ptr->role;
        act_t* prompts = role->prompts;
        prompts[message.message_type](&actor_ptr->stateptr, message.nbytes, message.data);
    }
}

static actor_t* get_actor() { // We have locked global mutex here
    actor_id_t first_actor_index = actor_system_data.actors_with_messages.first_index;
    actor_id_t actor_id = actor_system_data.actors_with_messages.actors[first_actor_index];
    actor_t* actor_to_handle = actor_system_data.actors[actor_id];

    actor_system_data.actors_with_messages.first_index =
            (1 + actor_system_data.actors_with_messages.first_index) %
            actor_system_data.actors_with_messages.max_size;

    return actor_to_handle;
}

static void* thread_run() {
    int err;

    while (system_alive()) {

        lock(&actor_system_data.variables_mutex);

        while (actor_system_data.actors_with_messages.first_index ==
               actor_system_data.actors_with_messages.second_index) { // Waits until there are messages for actors
            if ((err = pthread_cond_wait(&actor_system_data.waiting_threads,
                                         &actor_system_data.variables_mutex)) != 0) {
                syserr(err, "Conditional wait error");
            }
            if (actor_system_data.how_many_actors == actor_system_data.dead_actors) {
                if ((err = pthread_cond_signal(&actor_system_data.waiting_threads)) != 0) {
                    // If system is dead we wake up others thread, so they can finish
                    syserr(err, "Conditional signal error");
                }
                unlock(&actor_system_data.variables_mutex);
                return NULL;
            }
        }


        actor_t* actor_to_handle = get_actor();

        unlock(&actor_system_data.variables_mutex);

        lock(&actor_to_handle->actor_mutex);
        actor_to_handle->is_being_handled = true;
        actor_to_handle->is_in_queue = false;
        unlock(&actor_to_handle->actor_mutex);

        handle_actor(actor_to_handle);

        lock(&actor_to_handle->actor_mutex);
        bool actor_has_messages =
                actor_to_handle->message_queue.first_index != actor_to_handle->message_queue.second_index;
        bool is_dead = actor_to_handle->is_dead;
        actor_to_handle->is_being_handled = false;
        unlock(&actor_to_handle->actor_mutex);

        if (actor_has_messages) {
            add_actor_to_queue(&actor_system_data.actors_with_messages, actor_to_handle);
        } else if (is_dead) {
            check_fully_dead(actor_to_handle);
        }
    }

    if ((err = pthread_cond_signal(&actor_system_data.waiting_threads)) != 0) {
        // If system is dead we wake up others thread, so they can finish
        syserr(err, "Conditional signal error");
    }

    return NULL;
}

static int initialize_queue(actor_queue_t* queue) {
    queue->first_index = 0;
    queue->second_index = 0;
    queue->max_size = INITIAL_ACTOR_QUEUE_LIMIT;
    queue->actors = malloc(sizeof(actor_id_t) * INITIAL_ACTOR_QUEUE_LIMIT);
    if (!queue->actors) {
        return -1;
    }
    return 0;
}

static int initialize_system_data() {
    actor_system_data.can_create_new_actors = true;
    actor_system_data.how_many_actors = 0;
    actor_system_data.dead_actors = -2;

    actor_system_data.actors = malloc(sizeof(actor_t*) * INITIAL_ACTOR_QUEUE_LIMIT);
    if (!actor_system_data.actors) {
        return -1;
    }
    actor_system_data.actor_array_size = INITIAL_ACTOR_QUEUE_LIMIT;

    return initialize_queue(&actor_system_data.actors_with_messages);
}

static void handle_sigint() {
    lock(&actor_system_data.variables_mutex); // Safe, no other thread can enter this function with locked mutex
    actor_system_data.can_create_new_actors = false;
    actor_id_t num_of_actors = actor_system_data.how_many_actors;
    unlock(&actor_system_data.variables_mutex);

    // No new actors are created, we can safely access this array
    for (actor_id_t actor = 1; actor <= num_of_actors; ++actor) {
        actor_t* actor_ptr = actor_system_data.actors[actor];
        lock(&actor_ptr->actor_mutex);
        actor_ptr->is_dead = true;
        unlock(&actor_ptr->actor_mutex);

        check_fully_dead(actor_ptr);
    }
    int err;
    if ((err = pthread_cond_signal(&actor_system_data.waiting_threads)) != 0) {
        // If system is dead we wake up others thread, so they can finish
        syserr(err, "Conditional signal error");
    }
}

static void* signal_handler() {
    sigset_t sigint_mask;
    sigemptyset(&sigint_mask);
    sigaddset(&sigint_mask, SIGINT);

    int signal;
    sigwait(&sigint_mask, &signal);
    handle_sigint();

    return NULL;
}

static int make_signal_handler() {
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGINT);

    if (pthread_sigmask(SIG_BLOCK, &block_mask, NULL) != 0) {
        // Blocks SIGINT for all threads
        return -1;
    }

    pthread_t signal_thread;
    if (pthread_create(&signal_thread, NULL, signal_handler, NULL) != 0) {
        return -1;
    }
    actor_system_data.signal_handler = signal_thread;

    return 0;
}

static void cleanup_memory() { // We have locked variable mutex here
    for (actor_id_t actor = 1; actor <= actor_system_data.how_many_actors; ++actor) {
        actor_t* actor_ptr = actor_system_data.actors[actor];
        free(actor_ptr->message_queue.messages);
        pthread_mutex_destroy(&actor_ptr->actor_mutex);
        free(actor_ptr);
    }
    free(actor_system_data.actors);
    free(actor_system_data.actors_with_messages.actors);
}

int actor_system_create(actor_id_t* actor, role_t* const role) {
    if (make_signal_handler() < 0) {
        return -1;
    }

    if (initialize_system_data() < 0) {
        cleanup_memory();
        return -1;
    }

    for (int i = 0; i <= POOL_SIZE - 1; ++i) {
        if (pthread_create(&actor_system_data.thread_array[i], NULL, thread_run, NULL) != 0) {
            cleanup_memory();
            return -1;
        }
    }

    actor_t* first_actor = create_actor(role);
    if (!first_actor) {
        return -1;
    }

    *actor = first_actor->actor_id;

    message_t new_message;
    new_message.message_type = MSG_HELLO;
    new_message.data = NULL;
    new_message.nbytes = sizeof(actor_id_t);
    send_message(*actor, new_message); // Should always succeed

    return 0;
}

void actor_system_join(actor_id_t actor) {
    if (!system_alive()) {
        return;
    }

    lock(&actor_system_data.variables_mutex); // Static mutex, can be accessed anytime

    if (actor > actor_system_data.how_many_actors || actor < 1) {
        unlock(&actor_system_data.variables_mutex);
        return;
    }

    unlock(&actor_system_data.variables_mutex);

    void* return_value;
    int err;
    for (int i = 0; i < POOL_SIZE; ++i) {
        if ((err = pthread_join(actor_system_data.thread_array[i], &return_value)) != 0) {
            syserr(err, "Join error");
        }
    }

    lock(&actor_system_data.variables_mutex);
    cleanup_memory();
    actor_system_data.how_many_actors = 0;
    actor_system_data.dead_actors = 0;
    unlock(&actor_system_data.variables_mutex);

    pthread_cancel(actor_system_data.signal_handler); // May return error value, but we can't deal with it

    if ((err = pthread_join(actor_system_data.signal_handler, &return_value)) != 0) {
        syserr(err, "Join error");
    }

    sigset_t unblock_sigint;
    sigemptyset(&unblock_sigint);
    sigaddset(&unblock_sigint, SIGINT);

    pthread_sigmask(SIG_UNBLOCK, &unblock_sigint, NULL); // May return error value, but we can't deal with it
}