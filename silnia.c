#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "cacti.h"

#define MSG_CALCULATE (message_type_t)0x1
#define MSG_WAIT (message_type_t)0x2

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

// Każdy aktor ma otrzymywać w komunikacie dotychczas obliczoną częściową silnię k! wraz z liczbą k,
// tworzyć nowego aktora i wysyłać do niego (k+1)! oraz k+1.
// Po końcowego n! wynik powinien zostać wypisany na standardowe wyjście.

void message_hello(void **stateptr, size_t nbytes, void *data) {
    actor_id_t parent_id = (actor_id_t) data;

    factorial_info_t *current_state = (factorial_info_t *) stateptr; // todo tu chyba malloc na to
    // todo to wyzej chyba zle, tam jest null i ja dopiero sobie robie tam
    current_state = (factorial_info_t *) malloc(sizeof(factorial_info_t));

    current_state->parent_id = parent_id;

    printf("FINISHED message_hello handler with parent: %ld\n", parent_id);


// TODO TUTAJ WYSYLANIE WAIT DO OJCA

    // Sending message MSG_WAIT to parent to get next state of factorial.
    message_t message = {.message_type = MSG_WAIT,
                         .nbytes = sizeof(void *),
                         .data = (void *) actor_id_self()};

    int error_code = send_message(current_state->parent_id, message);
    assert(error_code == 0);
}

// Calculates factorial. If not finished makes new actor.
void calculate_factorial(void **stateptr, size_t nbystes, void *data) {
    factorial_info_t *current_factorial = (factorial_info_t *) data;
//    factorial_info_t *my_state = (factorial_info_t *) stateptr;
//    my_state = current_factorial;

    if (current_factorial->last_step == current_factorial->current_factor) {
        // finished, can print
        printf("RESULT ======== %d\n", current_factorial->current_factorial);
    }
    else {
        // Creates new actor. Now should wait for it to be ready.
        message_t message = {.message_type = MSG_SPAWN,
                             .nbytes = sizeof(&actor_role),
                             .data = (void *) &actor_role};

        int error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
    }
}

// When factorial is not finished new actor calls
// this to its parent to get last factorial state.
// Parent sends next factorial state to actor with id in *data.
void son_waiting_for_factorial(void **stateptr, size_t nbystes, void *data) {
    factorial_info_t *current_factorial = (factorial_info_t *) stateptr;

    int next_factor = current_factorial->current_factor + 1;
    factorial_info_t next = {.current_factorial = current_factorial->current_factorial * next_factor,
            .current_factor = next_factor,
            .last_step = current_factorial->last_step};

    message_t message = {.message_type = MSG_CALCULATE,
                         .nbytes = sizeof(factorial_info_t *),
                         .data = (void *) &next};

    printf("sending message to parent that I'm ready\n");

    int error_code = send_message(current_factorial->parent_id, message);
    assert(error_code == 0);
}

int main() {
    int n;
    scanf("%d", &n);
    printf("brute_factorial(%d) = %d\n", n, brute_factorial(n));

    printf("Teraz aktorzy xD!\n");

    int error_code;
    actor_id_t actor_id;

    actor_role = (role_t) {.nprompts = 2,
            .prompts = (act_t *) message_hello};


    error_code = actor_system_create(&actor_id, &actor_role);
    assert(error_code == 0);

    printf("First actor id = %ld", actor_id);

    factorial_info_t initial_state = {.current_factorial = 1,
                                      .current_factor = 0,
                                      .last_step = n};

    message_t message = {.message_type = MSG_CALCULATE,
                         .nbytes = sizeof(initial_state),
                         .data = (void *) &initial_state};

    error_code = send_message(actor_id, message);
    assert(error_code == 0);





//    actor_system_join(actor_id);

// ------------------------------------------------------------------------------------------

//    printf("START\n");
////    int result = actor_system_create(NULL, NULL);
////    printf("actor_system_create result = %d\n", result);
//
//    int error_code;
//    actor_id_t first_actor_id = -1;
//    role_t actor_role = (role_t) {.nprompts = 2, .prompts = {calculate, calculate}};
//
//    int result = actor_system_create(&first_actor_id, &actor_role);
//    printf("actor_system_create result = %d\n", result);
//    printf("new actor id: %ld\n", first_actor_id);

    return 0;
}
