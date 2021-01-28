#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "cacti.h"

// TODO
#include <unistd.h>
#include <stdbool.h>

#define MSG_CALCULATE (message_type_t)0x1
#define MSG_WAIT (message_type_t)0x2
#define MSG_CLEAN (message_type_t)0x3
#define MSG_INIT (message_type_t)0x4
// todo wiadomosc na czyszczenie zasobow?

// All actors will share same role.
role_t actor_role;

int brute_factorial(int n) {
    int result = 1;

    for (int i = 2; i <= n; ++i) {
        result *= i;
    }

    return result;
}

typedef struct {
    actor_id_t parent_id;
    int current_factorial; // 1 -> k!
    int current_factor; // 0 -> k
    int last_step; // n -> n
} factorial_info_t;

//typedef struct {
//    actor_id_t parent_id;
//    factorial_info_t *my_factorial;
//} actor_state_t;

// Każdy aktor ma otrzymywać w komunikacie dotychczas obliczoną częściową silnię k! wraz z liczbą k,
// tworzyć nowego aktora i wysyłać do niego (k+1)! oraz k+1.
// Po końcowego n! wynik powinien zostać wypisany na standardowe wyjście.

// Hello message handler.
// Saves parent's id and sends MSG_WAIT to parent.
void message_hello(void **stateptr, size_t nbytes, void *data) {
    printf("IN HELLO MESSAGE\n");
    actor_id_t parent_id = (actor_id_t) data;
//    actor_id_t parent_id = *((actor_id_t *) data);

    factorial_info_t *current_state = (factorial_info_t *) malloc(sizeof(factorial_info_t));
    *stateptr = (void *) current_state;

    current_state->parent_id = parent_id;

    printf("FINISHED message_hello handler with parent: %ld\n", parent_id);

    // Sending message MSG_WAIT to parent to get next state of factorial.
    message_t message = {
            .message_type = MSG_WAIT,
            .nbytes = sizeof(actor_id_t),
            .data = (void *) actor_id_self()};
//    message_t message = {
//            .message_type = MSG_WAIT,
//            .nbytes = sizeof(actor_id_t *),
//            .data = (void *) &actor_id_self};

    int error_code = send_message(current_state->parent_id, message);
    assert(error_code == 0);

    printf("SENT MSG_WAIT to parent: %ld\n", parent_id);
}

// Calculates factorial. If not finished makes new actor.
// Data is next_factorial state
void calculate_factorial(void **stateptr, size_t nbystes, void *data) {
    printf("IN CALCULATE FACTORIAL\n");

    factorial_info_t *current_state = (factorial_info_t *) *stateptr;
    factorial_info_t *current_factorial = (factorial_info_t *) data;

    actor_id_t parent_id = current_state->parent_id;
    *current_state = *current_factorial;
    current_state->parent_id = parent_id;

    if (current_factorial->last_step == current_factorial->current_factor) {
        // finished, can print
        printf("RESULT ======== %d\n", current_factorial->current_factorial);

        message_t message = {
                .message_type = MSG_CLEAN,
                .nbytes = 0,
                .data = NULL
        };

        int error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
    }
    else {
        // Creates new actor. Now should wait for it to be ready.
        message_t message = {
                .message_type = MSG_SPAWN,
                .nbytes = sizeof(role_t *),
                .data = (void *) &actor_role};

        int error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
    }
}

// When factorial is not finished new actor calls
// this to its parent to get last factorial state.
// Parent sends next factorial state to actor with id in *data.
void son_waiting_for_factorial(void **stateptr, size_t nbystes, void *data) {
    printf("IN WAIT MESSAGE\n");
    factorial_info_t *current_factorial = (factorial_info_t *) *stateptr;
    actor_id_t child_id = (actor_id_t) data;
//    actor_id_t child_id = *((actor_id_t *) data);

    int next_factor = current_factorial->current_factor + 1;
    factorial_info_t *next_state = (factorial_info_t *) malloc(sizeof(factorial_info_t));
    *next_state = (factorial_info_t) {
            .current_factorial = current_factorial->current_factorial * next_factor,
            .current_factor = next_factor,
            .last_step = current_factorial->last_step};

    message_t message = {
            .message_type = MSG_CALCULATE,
            .nbytes = sizeof(factorial_info_t *),
            .data = (void *) next_state};

    printf("SENDING MSG_CALCULATE to child %ld\n", child_id);

    int error_code = send_message(child_id, message);
    assert(error_code == 0);
}

// Handles MSG_CLEAN to clean up this node and message its parent.
// nbytes is 0 and data is NULL
void clean_up(void **stateptr, size_t nbytes, void *data) {
    printf("CLEANING AFTER NODE\n");

    factorial_info_t *current_factorial = (factorial_info_t *) *stateptr;

    actor_id_t parent_id = current_factorial->parent_id;
    bool first_actor = current_factorial->current_factor == 0;
    free(current_factorial);

    message_t message = {
            .message_type = MSG_GODIE,
            .nbytes = 0,
            .data = NULL
    };

    int error_code = send_message(actor_id_self(), message);
    assert(error_code == 0);

    if (!first_actor) {
        message = (message_t) {
                .message_type = MSG_CLEAN,
                .nbytes = 0,
                .data = NULL
        };

        error_code = send_message(parent_id, message);
        assert(error_code == 0);
    }
}

// Allocates memory for first actor.
// Waits for sending calculate factorial.
// data is NULL
void initial_message(void **stateptr, size_t nbystes, void *data) {
    printf("IN INIT MESSAGE!\n");
    assert(nbystes == 0 && data == NULL);
    assert(stateptr != NULL);

    factorial_info_t *current_state = (factorial_info_t *) malloc(sizeof(factorial_info_t));
    current_state->parent_id = -1;
    *stateptr = (void *) current_state;
}

int main() {
    int n;
//    scanf("%d", &n);
    n = 5;
    printf("brute_factorial(%d) = %d\n", n, brute_factorial(n));

    printf("Teraz aktorzy xD!\n");

    int error_code;
    actor_id_t actor_id = -1;

    actor_role.nprompts = 4;
    actor_role.prompts = (act_t[]) {
            message_hello,
            calculate_factorial,
            son_waiting_for_factorial,
            clean_up,
            initial_message};

    error_code = actor_system_create(&actor_id, &actor_role);
    assert(error_code == 0);

    printf("First actor id = %ld\n", actor_id);

    factorial_info_t *initial_factorial =
            (factorial_info_t *) malloc(sizeof(factorial_info_t));

    *initial_factorial = (factorial_info_t) {
            .current_factorial = 1,
            .current_factor = 0,
            .last_step = n};

    message_t message = {
            .message_type = MSG_INIT,
            .nbytes = 0,
            .data = NULL};

    error_code = send_message(actor_id, message);
    assert(error_code == 0);


    message = (message_t) {
            .message_type = MSG_CALCULATE,
            .nbytes = sizeof(factorial_info_t *),
            .data = (void *) initial_factorial
    };

    error_code = send_message(actor_id, message);
    assert(error_code == 0);


    printf("MAIN THREAD SLEEP\n");
//    sleep(3000);
    actor_system_join(actor_id);
    printf("THIS SHOULD BE LAST MESSAGE\n");

    return 0;
}
