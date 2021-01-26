#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "cacti.h"

int brute_factorial(int n) {
    int result = 1;

    for (int i = 2; i <= n; ++i) {
        result *= i;
    }

    return result;
}

typedef struct {
    int current_factorial; // k!
    int current_factor; // k
    int last_factor; // n
} factorial_info_t;

// Każdy aktor ma otrzymywać w komunikacie dotychczas obliczoną częściową silnię k! wraz z liczbą k,
// tworzyć nowego aktora i wysyłać do niego (k+1)! oraz k+1.
// Po końcowego n! wynik powinien zostać wypisany na standardowe wyjście.

void *calculate(void **stateptr, size_t nbystes, void *data) {
    factorial_info_t *factorial_info = (factorial_info_t *) data;

    if (factorial_info->last_factor == factorial_info->current_factor) {
        // koniec
    }
    else {
        int next_factor = factorial_info->current_factor + 1;
        factorial_info_t next = {factorial_info->current_factorial * next_factor,
                                 next_factor, factorial_info->last_factor};


    }
}

int main() {
//    int n;
//    scanf("%d", &n);
//    printf("brute_factorial(%d) = %d\n", n, brute_factorial(n));
//
//    printf("Teraz aktorzy xD!\n");
//
//    int error_code;
//    actor_id_t actor_id;
//    role_t actor_role;
//
//    actor_role = (role_t) {.nprompts = 2, .prompts = {calculate}};
//
////    actor_role.nprompts = 1;
////    actor_role.prompts = malloc(actor_role.nprompts * sizeof(act_t));
////    actor_role.prompts = &calculate;
////    actor_role.prompts++;
////    actor_role.prompts[0] = &calculate;


//    error_code = actor_system_create(&actor_id, &actor_role);
//    assert(error_code == 0);
//
//    actor_system_join(actor_id);


    printf("START\n");
    int result = actor_system_create(NULL, NULL);
    printf("actor_system_create result = %d\n", result);

    return 0;
}
