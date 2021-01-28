#include <assert.h>
#include "cacti.h"
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h> // todo

// STRUCTS ====================================================================

// Cyclic queue of actors' events.
typedef struct cyclic_queue {
    size_t first_empty;
    size_t first_full;
    size_t current_size;

    message_t *messages[ACTOR_QUEUE_LIMIT];
} cyclic_queue_t;

// Actor's necessary data.
typedef struct actor {
    actor_id_t id;
    bool is_dead;
    bool in_queue;
    cyclic_queue_t *messages_queue;
    role_t *role;
    void *state; // TODO cos takiego?

} actor_t;

// Queue of actors waiting for free thread.
typedef struct actor_queue {
    size_t first_empty;
    size_t first_full;
    size_t current_size;

    actor_id_t actors[CAST_LIMIT];
} actor_queue_t;

// Data structure containing all information about actors.
typedef struct actors_system {
    // Mutex for working with actors_system.
    pthread_mutex_t mutex;

    // Conditional for waiting for actors.
    pthread_cond_t wait_for_actor;

    // Number of threads waiting for actor.
    size_t waiting_for_actor;

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

// DATA =======================================================================

// Global data structure maintaining actors.
static actors_system_t *actors_pool = NULL;

// Thread local variable of current actor being processed.
static __thread actor_id_t thread_actor_id = -1;

// DECLARATIONS ===============================================================
static message_t *copy_message(message_t message);

static void lock_mutex();

static void unlock_mutex();

static void signal_wait_for_actor();

static message_t *get_message(cyclic_queue_t *queue);

static void add_message(actor_id_t actor_id, cyclic_queue_t *queue,
                        message_t *message);

static void queue_add_actor(actor_queue_t *queue, actor_id_t actor);

static actor_id_t queue_get_actor(actor_queue_t *queue);

static void init_actors_system();

static void destroy_actors_system();

static void add_actor(actor_id_t *actor_id, role_t *const role);

void perform_message(actor_t *current_actor, message_t *message);

static void *thread_loop(void *d);


// IMPLEMENTATION =============================================================


static message_t *copy_message(message_t message) {
    message_t *copied = (message_t *) malloc(sizeof(message));
    *copied = message;

    return copied;
}

static void lock_mutex() {
    int error_code = pthread_mutex_lock(&actors_pool->mutex);
    assert(error_code == 0);
//    printf("LOCKED MUTEX\n");
}

static void unlock_mutex() {
//    printf("UNLOCKING MUTEX\n");
    int error_code = pthread_mutex_unlock(&actors_pool->mutex);
    assert(error_code == 0);
}

static void signal_wait_for_actor() {
    printf("WAKING THREAD\n");
    int error_code = pthread_cond_signal(&actors_pool->wait_for_actor);
    assert(error_code == 0);
}

// Returns pointer to message that was first in actor's event queue.
static message_t *get_message(cyclic_queue_t *queue) {
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

// Adds new_message to actor's event queue.
// Unlocks mutex.
static void add_message(actor_id_t actor_id, cyclic_queue_t *queue,
                        message_t *message) {
    if (queue->current_size == ACTOR_QUEUE_LIMIT) {
        // Current queue is full.
        assert(false); // todo remove this
    }

    queue->current_size++;

    queue->messages[queue->first_empty] = message;
    queue->first_empty = (queue->first_empty + 1) % ACTOR_QUEUE_LIMIT;


    // If this is my first message.
    if (queue->current_size > 0) {
        // First message -> adding to actors_queue
        queue_add_actor(actors_pool->actors_queue, actor_id);
    }
    else {
        unlock_mutex();
    }
}

// Adds actor to actor_queue.
// Unlocks mutex.
static void queue_add_actor(actor_queue_t *queue, actor_id_t actor) {
    if (queue->current_size == CAST_LIMIT) {
        // actor_queue is full.
        assert(false); //todo
    }

    if (actors_pool->actors_data[actor]->in_queue) {
        printf("ACTOR ALREADY IN QUEUE!!!!! MUST HAVE BEEN ADDED EARLIER\n");
        unlock_mutex();
        return;
    }
    else if (actors_pool->actors_data[actor]->messages_queue->current_size == 0) {
        printf("ACTOR DOESNT HAVE ANY MESSAGE - NOT ADDED\n");
        unlock_mutex();
        return;
    }

    actors_pool->actors_data[actor]->in_queue = true;

    queue->current_size++;
    queue->actors[queue->first_empty] = actor;
    queue->first_empty = (queue->first_empty + 1) % CAST_LIMIT;

    // Some threads are waiting for actors.
    if (actors_pool->waiting_for_actor > 0) {
        printf("thread should wake up! %lu threads are waiting\n",
               actors_pool->waiting_for_actor);
        signal_wait_for_actor();
    }
    else {
        unlock_mutex();
    }
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

    assert(actors_pool->actors_data[result]->in_queue);
    actors_pool->actors_data[result]->in_queue = false;

    return result;
}

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

    lock_mutex();
    // Creating threads with default attr.
    for (size_t thread = 0; thread < POOL_SIZE; ++thread) {
        error_code = pthread_create(&actors_pool->threads[thread],
                                    NULL, thread_loop, NULL);
        assert(error_code == 0);
        printf("Creating %zu thread\n", thread);
    }

    for (size_t i = 0; i < CAST_LIMIT; ++i) {
        actors_pool->actors_queue->actors[i] = -1;
    }
    unlock_mutex();

    printf("Finish init\n");
}

static void destroy_actors_system() {
    printf("Starting destroy\n");
    int error_code;

    for (size_t thread = 0; thread < POOL_SIZE; ++thread) {
        void *thread_result;
        error_code = pthread_join(actors_pool->threads[thread], &thread_result);
        assert(error_code == 0);
        printf("Deleting %zu thread\n", thread);
    }

    error_code = pthread_cond_destroy(&actors_pool->wait_for_actor);
    assert(error_code == 0);

    error_code = pthread_mutex_destroy(&actors_pool->mutex);
    assert(error_code == 0);


    // Free memory allocated for actors.
    for (size_t actor = 0; actor < actors_pool->first_empty; ++actor) {
        free(actors_pool->actors_data[actor]->messages_queue);
        free(actors_pool->actors_data[actor]->state);
        free(actors_pool->actors_data[actor]);
    }

    free(actors_pool->actors_queue);
    free(actors_pool);
    actors_pool = NULL;

    printf("Finish destroy\n");
}

static void add_actor(actor_id_t *actor_id, role_t *const role) {
    lock_mutex();
    *actor_id = actors_pool->first_empty;

    actors_pool->actors_data[*actor_id] = (actor_t *) malloc(sizeof(actor_t));
    actors_pool->actors_data[*actor_id]->role = role;
    actors_pool->actors_data[*actor_id]->id = *actor_id;
    actors_pool->actors_data[*actor_id]->is_dead = false;
    actors_pool->actors_data[*actor_id]->in_queue = false;
    actors_pool->actors_data[*actor_id]->messages_queue =
            (cyclic_queue_t *) malloc(sizeof(cyclic_queue_t));

    actors_pool->first_empty++;
    actors_pool->living_actors++;
    unlock_mutex();
}

// Performs first message of given actor.
void perform_message(actor_t *current_actor, message_t *message) {
    printf("IN PERFORMING MESSAGE!!!! Got message = %ld\n", message->message_type);

    if (message->message_type == MSG_SPAWN) {
        printf("MSG_SPAWN!!\n");

        // Data field is the new role.
        actor_id_t new_actor;
        add_actor(&new_actor, message->data);

        message_t new_message = {
                .message_type = MSG_HELLO,
                .nbytes = sizeof(actor_id_t),
                .data = (void *) current_actor->id
        };

        // Sends hello message to new actor.
        send_message(new_actor, new_message);
    }
    else if (message->message_type == MSG_GODIE) {
        lock_mutex();
        actors_pool->living_actors--;

        if (actors_pool->living_actors == 0) {
            printf("SYSTEM SHOULD DIE!\n");
            actors_pool->keep_working = false;
        }

        current_actor->is_dead = true;

        // todo clear messages
        unlock_mutex();
        return;
    }
    else {
        current_actor->role->prompts[message->message_type](
                &current_actor->state, message->nbytes, message->data);
    }

    lock_mutex();
    // Tries to requeue actor, this unlocks mutex.
    queue_add_actor(actors_pool->actors_queue, current_actor->id);
}

// Thread work loop.
static void *thread_loop(void *d) {
    (void) d; // not unused
    int error_code;

    while (actors_pool->keep_working) {
        lock_mutex();

        // Sleep when there are no actors.
        while (actors_pool->actors_queue->current_size == 0) {
            actors_pool->waiting_for_actor++;

            printf("THREAD SLEEP releasing mutex\n");
            error_code = pthread_cond_wait(&actors_pool->wait_for_actor, &actors_pool->mutex);
            assert(error_code == 0);
            printf("THREAD WOKE UP\n");

            actors_pool->waiting_for_actor--;
        }
        printf("THREAD ACTION\n");

        // Perform first actor's message.
        actor_id_t current_actor_id = queue_get_actor(actors_pool->actors_queue);
        actor_t *current_actor = actors_pool->actors_data[current_actor_id];

        unlock_mutex();

        printf("Watek rozpoczynam prace z aktorem: %zu\n", current_actor_id);
        thread_actor_id = current_actor_id;

        perform_message(current_actor, get_message(current_actor->messages_queue));

        printf("Watek koncze prace z aktorem: %zu\n", current_actor_id);
        thread_actor_id = -1;

    }

    printf("----------------THREAD LEAVING LOOP----------------\n");

    return NULL;
}

// PUBLIC =====================================================================

// Returns current actor's id.
actor_id_t actor_id_self() {
    return thread_actor_id;
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
    if (actors_pool != NULL) {
        printf("Already exists\n");
        assert(false);
    }

    init_actors_system();

    add_actor(actor, role);

    return 0;
}

void actor_system_join(actor_id_t actor) {
    (void) actor; // not unused
    destroy_actors_system();
}

// Sends message to certain actor.
int send_message(actor_id_t actor, message_t message) {
    lock_mutex();

    if (actor < 0 || actor >= (actor_id_t) actors_pool->first_empty) {
        printf("NO actor with id = %ld", actor);
        unlock_mutex();
        return -2;
    }

    actor_t *receiving_actor = actors_pool->actors_data[actor];

    if (receiving_actor->is_dead
        || receiving_actor->messages_queue->current_size == ACTOR_QUEUE_LIMIT) {
        printf("DEAD or FULL actor with id = %ld", actor);
        unlock_mutex();
        return -1;
    }

    // add_message unlocks mutex.
    add_message(actor, receiving_actor->messages_queue, copy_message(message));

    return 0;
}
