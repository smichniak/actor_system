#include "cacti.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

void hello(void** a, size_t b, void* c) {
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);

    if (c == NULL) {
        printf("Hello, My id is: %ld, I am the first actor, thread is: %llu\n", actor_id_self(), tid);
    } else {
        actor_id_t parent = (actor_id_t) c;
        printf("Hello, My id is: %ld My parent id is %ld, thread is: %llu\n", actor_id_self(), parent, tid);
    }

    message_t new_msg;
    new_msg.data = NULL;
    new_msg.message_type = 1;
    new_msg.nbytes = 1;
    send_message(actor_id_self(), new_msg);

}

void function(void** a, size_t b, void* c);

static act_t f1[2] = {hello, function};
static role_t r1 = {2, f1};

void function(void** a, size_t b, void* c) {

    actor_id_t id = actor_id_self();

    printf("Function, My id is: %ld\n", id);

//    sleep(1);

    if (id < 7) {
        message_t new_msg;
        new_msg.data = &r1;
        new_msg.message_type = MSG_SPAWN;
        new_msg.nbytes = sizeof(r1);
        send_message(id, new_msg);
    }


    message_t new_msg;
    new_msg.data = NULL;
    new_msg.message_type = MSG_GODIE;
    new_msg.nbytes = sizeof(void*);
    send_message(id, new_msg);

    sleep(3);

    printf("End of function, My id is: %ld\n", id);
}


int main() {
    printf("XD\n");
    actor_id_t first_id;

    printf("%d\n", actor_system_create(&first_id, &r1));
    printf("First id: %ld\n", first_id);
//
//    message_t new_msg;
//    new_msg.data = NULL;
//    new_msg.message_type = 1;
//    new_msg.nbytes = 1;
//    send_message(first_id, new_msg);


    actor_system_join(1);

    printf("%d\n", actor_system_create(&first_id, &r1));
    printf("Second system, First id: %ld\n", first_id);


    printf("XD\n");
    return 0;
}
