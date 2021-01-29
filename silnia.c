#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include "cacti.h"

#define MSG_CALCULATE (message_type_t)0x1
#define MSG_WAIT (message_type_t)0x2
#define MSG_CLEAN (message_type_t)0x3
#define MSG_INIT (message_type_t)0x4

// All actors will share same role.
role_t actor_role;

// Type for storing factorial.
typedef unsigned long long big_int;

// todo remove
big_int brute_factorial(big_int n) {
    big_int result = 1;

    for (big_int i = 2; i <= n; ++i) {
        result *= i;
    }

    return result;
}

// Actor's state data.
typedef struct {
    actor_id_t parent_id;
    big_int current_factorial; // 1 -> k!
    big_int current_factor; // 0 -> k
    big_int last_step; // n -> n
} factorial_info_t;


// Hello message handler.
// Saves parent's id and sends MSG_WAIT to parent.
void message_hello(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    actor_id_t parent_id = (actor_id_t) data;

    // Allocates memory for actor's state.
    factorial_info_t *current_state =
            (factorial_info_t *) malloc(sizeof(factorial_info_t));
    *stateptr = (void *) current_state;

    current_state->parent_id = parent_id;

    // Sending message MSG_WAIT to parent to get next state of factorial.
    message_t message = {
            .message_type = MSG_WAIT,
            .nbytes = sizeof(actor_id_t),
            .data = (void *) actor_id_self()
    };

    int error_code = send_message(current_state->parent_id, message);
    assert(error_code == 0);
}

// Calculates factorial. If not finished makes new actor.
// Passed data is next_factorial state
void calculate_factorial(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;

    factorial_info_t *current_state = (factorial_info_t *) *stateptr;
    factorial_info_t *current_factorial = (factorial_info_t *) data;

    // Copy all except parent_id.
    actor_id_t parent_id = current_state->parent_id;
    *current_state = *current_factorial;
    current_state->parent_id = parent_id;

    if (current_state->last_step == current_state->current_factor) {
        assert(brute_factorial(current_state->last_step)
               == current_state->current_factorial);
        // Finished, can print.
        printf("%lld\n", current_state->current_factorial);

        // Clean recursively.
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
                .data = (void *) &actor_role
        };

        int error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
    }

    free(current_factorial);
}

// When factorial is not finished new actor calls
// this to its parent to get last factorial state.
// Parent sends next factorial state to actor with id in data.
void son_waiting_for_factorial(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    factorial_info_t *current_factorial = (factorial_info_t *) *stateptr;
    actor_id_t child_id = (actor_id_t) data;

    big_int next_factor = current_factorial->current_factor + 1;

    factorial_info_t *next_state =
            (factorial_info_t *) malloc(sizeof(factorial_info_t));
    *next_state = (factorial_info_t) {
            .current_factorial = current_factorial->current_factorial * next_factor,
            .current_factor = next_factor,
            .last_step = current_factorial->last_step
    };

    message_t message = {
            .message_type = MSG_CALCULATE,
            .nbytes = sizeof(factorial_info_t *),
            .data = (void *) next_state
    };

    int error_code = send_message(child_id, message);
    assert(error_code == 0);
}

// Handles MSG_CLEAN to clean up this node and message its parent.
// nbytes is 0 and data is NULL
void clean_up(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    (void) data;

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

    // Stop recursion.
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
// Makes actor ready for receiving calculate factorial.
void initial_message(void **stateptr, size_t nbystes, void *data) {
    (void) nbystes;
    (void) data;

    factorial_info_t *current_state =
            (factorial_info_t *) malloc(sizeof(factorial_info_t));

    current_state->parent_id = -1;
    *stateptr = (void *) current_state;
}

int main() {
//    int last_value;
//    scanf("%d", &last_value);
//    big_int n = (big_int) last_value;
    big_int n = 5;

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

    // Is initialized as actor data and freed by MSG_CLEAN.
    factorial_info_t *initial_factorial =
            (factorial_info_t *) malloc(sizeof(factorial_info_t));

    *initial_factorial = (factorial_info_t) {
            .current_factorial = 1,
            .current_factor = 0,
            .last_step = n
    };

    message_t message = {
            .message_type = MSG_INIT,
            .nbytes = 0,
            .data = NULL
    };

    error_code = send_message(actor_id, message);
    assert(error_code == 0);

    message = (message_t) {
            .message_type = MSG_CALCULATE,
            .nbytes = sizeof(factorial_info_t *),
            .data = (void *) initial_factorial
    };

    error_code = send_message(actor_id, message);
    assert(error_code == 0);

    actor_system_join(actor_id);
    return 0;
}
