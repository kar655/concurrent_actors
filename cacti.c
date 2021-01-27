#include <assert.h>
#include "cacti.h"
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h> // todo


//static inline bool is_correct_actor_id(actor_id_t actor) {
//    return actor >= 0;
//}

// Cyclic queue of actors' events.
typedef struct cyclic_queue {
    size_t first_empty;
    size_t first_full;
    size_t current_size;

    message_t *messages[ACTOR_QUEUE_LIMIT];
} cyclic_queue_t;

// Adds new_message to actor's event queue.
static void queue_add_message(actor_id_t actor_id, cyclic_queue_t *queue, message_t *message);

// Returns pointer to message that was first in actor's event queue.
static message_t *queue_get_message(cyclic_queue_t *queue) {
    if (queue->current_size == 0) {
        // Current queue is empty.
        assert(false); // todo remove this
        return NULL;
    }

    queue->current_size--;

    message_t *result = queue->messages[queue->first_full];
    queue->messages[queue->first_full] = NULL; // todo nie potrzebne raczej
    queue->first_full = (queue->first_full + 1) % ACTOR_QUEUE_LIMIT;

    return result;
}

// Actor's necessary data.
typedef struct actor {
    actor_id_t id;
    bool is_dead;
    cyclic_queue_t *messages;
    role_t *role;
    void **state; // TODO cos takiego?

    // todo tu jakis cond na czekanie na wiadomosc
} actor_t;

// Queue of actors waiting for free thread.
typedef struct actor_queue {
    size_t first_empty;
    size_t first_full;
    size_t current_size;

    actor_id_t actors[CAST_LIMIT];
} actor_queue_t;

// Adds actor to actor_queue.
static void queue_add_actor(actor_queue_t *queue, actor_id_t actor) {
    if (queue->current_size == CAST_LIMIT) {
        // actor_queue is full.
        assert(false); //todo
    }

    queue->current_size++;
    queue->actors[queue->first_empty] = actor;
    queue->first_empty = (queue->first_empty + 1) % CAST_LIMIT;
}

// Return first actor's id from queue.
static actor_id_t queue_get_actor(actor_queue_t *queue) {
    if (queue->current_size == 0) {
        assert(false); // todo
    }

    queue->current_size--;

    actor_id_t result = queue->actors[queue->first_full];
    queue->actors[queue->first_full] = -1; // todo
    queue->first_full = (queue->first_full + 1) % CAST_LIMIT;

    return result;
}

// -----------------------------------------------------------------------------------------// -----------------------------------------------------------------------------------------
// Data structure containing all information about actors.
typedef struct actors_system {
    // Mutex for working with actors_system.
    pthread_mutex_t mutex;

    // Conditional for waiting for actors.
    pthread_cond_t wait_for_actor;

//    pthread_cond_t *wait_for_end;

    // Number of threads waiting for actor. // todo czy potrzebne?
    size_t waiting_for_actor;
//    size_t waiting_for_end;

    // Cyclic queue for threads of actors' ids.
    actor_queue_t *actors_queue;

    // Id of next actor.
    size_t first_empty;

    // All actors in system.
    actor_t *actors_data[CAST_LIMIT];

    // Array of threads.
    pthread_t threads[POOL_SIZE];

    // Number of living actors
    size_t living_actors;

    // Keep working until all actors died or signal was sent.
    bool keep_working;
} actors_system_t;

// -----------------------------------------------------------------------------------------
// Global data structure maintaining actors.
static actors_system_t *actors_pool = NULL;
// -----------------------------------------------------------------------------------------

static void lock_mutex() {
    int error_code = pthread_mutex_lock(&actors_pool->mutex);
    assert(error_code == 0);
}

static void unlock_mutex() {
    int error_code = pthread_mutex_unlock(&actors_pool->mutex);
    assert(error_code == 0);
}

static void signal_wait_for_actor() {
    int error_code = pthread_cond_signal(&actors_pool->wait_for_actor);
    assert(error_code == 0);
}

// Adds new_message to actor's event queue.
static void queue_add_message(actor_id_t actor_id, cyclic_queue_t *queue, message_t *message) {
    if (queue->current_size == ACTOR_QUEUE_LIMIT) {
        // Current queue is full.
        assert(false); // todo remove this
    }

    queue->current_size++;

    queue->messages[queue->first_empty] = message;
    queue->first_empty = (queue->first_empty + 1) % ACTOR_QUEUE_LIMIT;

    // If threads are waiting for new tasks.
    if (queue->current_size == 1) {
        lock_mutex();
        if (actors_pool->waiting_for_actor > 0) {
            queue_add_actor(actors_pool->actors_queue, actor_id);
            signal_wait_for_actor();
        }
        else {
            unlock_mutex();
        }
    }
}


//todo
void *thread_loop(void *d);

static void init_actors_system() {
    printf("Starting init\n");

    actors_pool = (actors_system_t *) malloc(sizeof(actors_system_t));

    actors_pool->waiting_for_actor = 0;
    actors_pool->first_empty = 0;
    actors_pool->keep_working = true;

    actors_pool->actors_queue = (actor_queue_t *) malloc(sizeof(actor_queue_t));

    int error_code;

    error_code = pthread_mutex_init(&actors_pool->mutex, NULL);
    assert(error_code == 0);

    error_code = pthread_cond_init(&actors_pool->wait_for_actor, NULL);
    assert(error_code == 0);

    // Creating threads with default attr.
    for (size_t thread = 0; thread < POOL_SIZE; ++thread) {
        error_code = pthread_create(&actors_pool->threads[thread],
                                    NULL, thread_loop, actors_pool);
        assert(error_code == 0);
        printf("Creating %zu thread\n", thread);
    }

    printf("Finish init\n");
}


static void destroy_actors_system() {
    printf("Starting destroy\n");
    int error_code;

    error_code = pthread_cond_destroy(&actors_pool->wait_for_actor);
    assert(error_code == 0);

    error_code = pthread_mutex_destroy(&actors_pool->mutex);
    assert(error_code == 0);

    for (size_t thread = 0; thread < POOL_SIZE; ++thread) {
        void *thread_result;
        error_code = pthread_join(actors_pool->threads[thread], &thread_result);
        assert(error_code == 0);
        printf("Deleting %zu thread\n", thread);
    }

    // Free memory allocated for actors.
    for (size_t actor = 0; actor < actors_pool->first_empty; ++actor) {
        free(actors_pool->actors_data[actor]->messages);
        free(actors_pool->actors_data[actor]);
    }

    free(actors_pool->actors_queue);
    free(actors_pool);
    actors_pool = NULL; // todo ?
    printf("Finish destroy\n");
}

static void add_actor(actor_id_t *actor_id, role_t *const role) {
    lock_mutex();
    *actor_id = actors_pool->first_empty;
    actors_pool->first_empty++;
    actors_pool->living_actors++;

    actors_pool->actors_data[*actor_id] = (actor_t *) malloc(sizeof(actor_t));
    actors_pool->actors_data[*actor_id]->role = role;
    actors_pool->actors_data[*actor_id]->id = *actor_id;
    actors_pool->actors_data[*actor_id]->is_dead = false;
//    *actors_pool->actors_data[*actor_id]->state = NULL;
    actors_pool->actors_data[*actor_id]->messages = (cyclic_queue_t *) malloc(sizeof(cyclic_queue_t));

    unlock_mutex(); // todo musi byc tu bo mego zaczac juz z niego korzystac
}

// Performs first message of given actor.
void perform_message(actor_t *current_actor) {
    message_t *message = queue_get_message(current_actor->messages);
    int error_code;

    if (message->message_type == MSG_SPAWN) {
//        Obsługa tego komunikatu używa pola data komunikatu jako struktury typu role.
//        Tworzy ona nowego aktora z tak przekazaną rolą, a następnie do nowo utworzonego
//        aktora wysyła komunikat MSG_HELLO z polem danych zawierającym uchwyt do aktora
//        ((void*)actor_id, gdzie actor_id jest typu actor_id_t), w którym następuje stworzenie nowego aktora.
        actor_id_t new_actor;
        add_actor(&new_actor, message->data);

        message_t new_message = {
                .message_type = MSG_HELLO,
                .nbytes = sizeof(void *),
                .data = (void *) current_actor->id}; // todo czy z &?

        send_message(new_actor, new_message);
    }
    else if (message->message_type == MSG_GODIE) {
        error_code = pthread_mutex_lock(&actors_pool->mutex);
        assert(error_code == 0);
        actors_pool->living_actors--;

        if (actors_pool->living_actors == 0) {
            printf("SYSTEM SHOULD DIE!\n");
            actors_pool->keep_working = false;
            assert(false);
        }

        current_actor->is_dead = true;

        error_code = pthread_mutex_unlock(&actors_pool->mutex);
        assert(error_code == 0);
    }
    else {
//        Obsługa tego komunikatu nie jest predefiniowana. Jednak obsługa tych komunikatów
//        jest o tyle ważna, że pozwala ona nowemu aktorowi zdobyć identyfikator pewnego
//        aktora w systemie, aby móc do niego wysyłać komunikaty. Tym początkowo znanym nowemu
//        aktorowi aktorem jest aktor, który go stworzył. Identyfikator tego aktora znajduje
//        się w argumencie data funkcji obsługującej takie komunikaty. Obsługa komunikatu MSG_HELLO
//        w argumencie stateptr otrzymuje wskaźnik do miejsca w pamięci zawierającego wartość NULL.
//        Może ona tę wartość zastąpić wskaźnikiem do zainicjalizowanego przez siebie stanu wewnętrznego aktora.

        current_actor->role->prompts[message->message_type](
                current_actor->state, message->nbytes, message->data);
    }
}

// Thread work loop.
void *thread_loop(void *d) {
    bool keep_working = true;
    actors_system_t *data = (actors_system_t *) d;
    int error_code;
//    return (void *) keep_working;

    do {
        error_code = pthread_mutex_lock(&data->mutex);
        assert(error_code == 0);

        // Sleep when there are no actors.
        while (data->actors_queue->current_size == 0) {
            data->waiting_for_actor++;

            printf("THREAD SLEEP\n");
            error_code = pthread_cond_wait(&data->wait_for_actor, &data->mutex);
            assert(error_code == 0);

            data->waiting_for_actor--;
        }
        printf("THREAD ACTION\n");

        // Perform first actor's message.
        // todo dodawanie aktora jesli ma kolejne zadania
        actor_id_t current_actor_id = queue_get_actor(data->actors_queue);
        actor_t *current_actor = actors_pool->actors_data[current_actor_id];

        // release mutex
        error_code = pthread_mutex_unlock(&data->mutex);
        assert(error_code == 0);

        printf("Watek rozpoczynam prace z aktorem: %zu\n", current_actor_id);

        perform_message(current_actor);

        printf("Watek koncze prace z aktorem: %zu\n", current_actor_id);

    } while (data->keep_working);

    // todo
    return (void *) keep_working;
}

// PUBLIC -------------------------------------------------------------------------------------------------------

// Returns current actor's id.
actor_id_t actor_id_self() {
    return 0;
}

// Pulę wątków należy zaimplementować jako sposób wewnętrznej organizacji systemu aktorów.
// Pula ma być tworzona wraz z utworzeniem systemu wątków w opisanej poniżej procedurze
// actor_system_create. Powinna ona dysponować liczbą wątków zapisaną w stałej POOL_SIZE.
// Wątki powinny zostać zakończone automatycznie, gdy wszystkie aktory w systemie skończą działanie.
int actor_system_create(actor_id_t *actor, role_t *const role) {
    if (actors_pool != NULL) {
        printf("Already exists\n");
        assert(false);
    }

    init_actors_system();

    add_actor(actor, role);

    return 0;
}


// Wywołanie actor_system_join(someact) powoduje oczekiwanie na zakończenie działania systemu
// aktorów, do którego należy aktor someact. Po tym wywołaniu powinno być możliwe poprawne
// stworzenie nowego pierwszego aktora za pomocą actor_system_create. W szczególności taka
// sekwencja nie powinna prowadzić do wycieku pamięci.
void actor_system_join(actor_id_t actor) {
    destroy_actors_system();
}


// Sends message to certain actor.
// Requires actors_pool's mutex.
int send_message(actor_id_t actor, message_t message) {
    lock_mutex();

    if (actor < 0 || actor >= (actor_id_t) actors_pool->first_empty) {
        printf("NO actor with id = %ld", actor);
        unlock_mutex();
        return -2;
    }

    unlock_mutex();

    actor_t *receiving_actor = actors_pool->actors_data[actor];

    // TODO chyba dalej mutex bo co jak umrze w trakcie???
    if (receiving_actor->is_dead
        || receiving_actor->messages->current_size == ACTOR_QUEUE_LIMIT) {
        printf("DEAD or FULL actor with id = %ld", actor);
        return -1;
    }

    queue_add_message(actor, receiving_actor->messages, &message);

    return 0;
}
