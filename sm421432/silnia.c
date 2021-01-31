#include "cacti.h"

#include <stdio.h>

#define ignore (void)

#define MSG_HELLO_PARENT 1
#define MSG_GET_DATA 2

typedef struct factorial_data {
    int n;
    int k;
    unsigned long long current_product;
} factorial_t;

void hello(void** stateptr, size_t nbytes, void* data);

void hello_parent(void** stateptr, size_t nbytes, void* data);

void get_data(void** stateptr, size_t nbytes, void* data);

act_t prompts[] = {
        hello,
        hello_parent,
        get_data,

};
role_t factorial_role = {
        .nprompts = 3,
        .prompts = prompts
};

void hello(void** stateptr, size_t nbytes, void* data) {
    ignore nbytes;
    ignore stateptr;

    if (data != NULL) {
        actor_id_t parent = (actor_id_t) data;

        message_t new_msg;
        new_msg.data = (void*) actor_id_self();
        new_msg.message_type = MSG_HELLO_PARENT;
        new_msg.nbytes = sizeof(actor_id_t);
        send_message(parent, new_msg);
    }

}

void hello_parent(void** stateptr, size_t nbytes, void* data) {
    ignore nbytes;

    actor_id_t child = (actor_id_t) data;
    factorial_t* factorial_data = *stateptr;

    factorial_data->k++;
    factorial_data->current_product = factorial_data->current_product * factorial_data->k;

    message_t die_msg;
    die_msg.data = NULL;
    die_msg.message_type = MSG_GODIE;
    die_msg.nbytes = sizeof(NULL);


    if (factorial_data->n == factorial_data->k) {
        printf("%llu\n", factorial_data->current_product);

        send_message(child, die_msg);
    } else {
        message_t new_msg;
        new_msg.data = factorial_data;
        new_msg.message_type = MSG_GET_DATA;
        new_msg.nbytes = sizeof(factorial_t*);
        send_message(child, new_msg);
    }

    send_message(actor_id_self(), die_msg);
}

void get_data(void** stateptr, size_t nbytes, void* data) {
    ignore nbytes;

    factorial_t* factorial_data = (factorial_t*) data;
    *stateptr = factorial_data;

    message_t new_msg;
    new_msg.data = &factorial_role;
    new_msg.message_type = MSG_SPAWN;
    new_msg.nbytes = sizeof(factorial_role);
    send_message(actor_id_self(), new_msg);
}

int main() {
    actor_id_t first_id;
    int n;
//
    scanf("%d", &n);
//    printf("%d\n", n);

    factorial_t factorial_data;
    factorial_data.current_product = 1;
    factorial_data.k = 0;
    factorial_data.n = n;

    actor_system_create(&first_id, &factorial_role);

    message_t start_factorial;
    start_factorial.data = &factorial_data;
    start_factorial.message_type = MSG_GET_DATA;
    start_factorial.nbytes = sizeof(factorial_t*);

    send_message(first_id, start_factorial);

    actor_system_join(first_id);
    return 0;
}
