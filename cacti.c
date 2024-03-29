#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include "cacti.h"


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
    void *state;
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

    // Number of living actors.
    size_t living_actors;

    // Number of messages in actors' queues.
    size_t messages_in_system;

    // If SIGINT was sent.
    bool got_sigint;

    // Number of threads which joined main thread.
    size_t thread_collected;
} actors_system_t;


// Global data structure maintaining actors.
static actors_system_t *actors_pool = NULL;

// Thread local variable of current actor being processed.
static __thread actor_id_t thread_actor_id = -1;

static void handle_sigint(int sig);

static void set_sigint_handler();

static message_t *copy_message(message_t message);

static bool thread_keep_working();

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

static void clear_actor(actor_t *actor);

void perform_message(actor_t *current_actor, message_t *message);

static void *thread_loop(void *d);


static void handle_sigint(int sig) {
    if (sig == SIGINT) {
        actors_pool->got_sigint = true;
    }
}

static void set_sigint_handler() {
    signal(SIGINT, handle_sigint);
}

static message_t *copy_message(message_t message) {
    message_t *copied = (message_t *) malloc(sizeof(message));
    *copied = message;

    return copied;
}

static bool thread_keep_working() {
    if (!actors_pool->got_sigint) {
        return actors_pool->living_actors > 0 || actors_pool->messages_in_system > 0;
    }
    else {
        return actors_pool->messages_in_system > 0;
    }
}

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

// Returns pointer to message that was first in actor's event queue.
static message_t *get_message(cyclic_queue_t *queue) {
    if (queue->current_size == 0) {
        // Current queue is empty.
        assert(false);
    }

    queue->current_size--;

    message_t *result = queue->messages[queue->first_full];
    queue->messages[queue->first_full] = NULL;
    queue->first_full = (queue->first_full + 1) % ACTOR_QUEUE_LIMIT;

    return result;
}

// Adds new_message to actor's event queue.
static void add_message(actor_id_t actor_id, cyclic_queue_t *queue,
                        message_t *message) {
    if (queue->current_size == ACTOR_QUEUE_LIMIT) {
        assert(false);
    }

    queue->current_size++;

    queue->messages[queue->first_empty] = message;
    queue->first_empty = (queue->first_empty + 1) % ACTOR_QUEUE_LIMIT;

    actors_pool->messages_in_system++;

    // Adds actor to actors queue if its not already added.
    queue_add_actor(actors_pool->actors_queue, actor_id);
}

// Tries to add actor to actor_queue.
static void queue_add_actor(actor_queue_t *queue, actor_id_t actor) {
    if (queue->current_size == CAST_LIMIT) {
        // actor_queue is full.
        assert(false);
    }

    if (actors_pool->actors_data[actor]->in_queue
        || actors_pool->actors_data[actor]->messages_queue->current_size == 0) {
        return;
    }

    actors_pool->actors_data[actor]->in_queue = true;

    queue->current_size++;
    queue->actors[queue->first_empty] = actor;
    queue->first_empty = (queue->first_empty + 1) % CAST_LIMIT;

    // Some threads are waiting for actors.
    if (actors_pool->waiting_for_actor > 0) {
        signal_wait_for_actor();
    }
}

// Return first actor's id from queue.
static actor_id_t queue_get_actor(actor_queue_t *queue) {
    if (queue->current_size == 0) {
        assert(false);
    }

    queue->current_size--;

    actor_id_t result = queue->actors[queue->first_full];
    queue->actors[queue->first_full] = -1;
    queue->first_full = (queue->first_full + 1) % CAST_LIMIT;

    assert(actors_pool->actors_data[result]->in_queue);
    return result;
}

static void init_actors_system() {
    actors_pool = (actors_system_t *) malloc(sizeof(actors_system_t));

    *actors_pool = (actors_system_t) {
            .waiting_for_actor = 0,
            .first_empty = 0,
            .living_actors = 1, // Fake actor prevents threads from dying
            .messages_in_system = 0,
            .thread_collected = 0
    };

    actors_pool->actors_queue = (actor_queue_t *) malloc(sizeof(actor_queue_t));

    *(actors_pool->actors_queue) = (actor_queue_t) {
            .first_empty = 0,
            .first_full = 0,
            .current_size = 0
    };

    int error_code;

    error_code = pthread_mutex_init(&actors_pool->mutex, NULL);
    assert(error_code == 0);

    error_code = pthread_cond_init(&actors_pool->wait_for_actor, NULL);
    assert(error_code == 0);

    // Creating threads with default attr.
    for (size_t thread = 0; thread < POOL_SIZE; ++thread) {
        error_code = pthread_create(&actors_pool->threads[thread],
                                    NULL, thread_loop, NULL);
        assert(error_code == 0);
    }
}

static void destroy_actors_system() {
    int error_code;

    // Collect threads.
    for (; actors_pool->thread_collected < POOL_SIZE;
           ++actors_pool->thread_collected) {
        void *thread_result;
        error_code = pthread_join(
                actors_pool->threads[actors_pool->thread_collected],
                &thread_result);
        assert(error_code == 0);
    }

    error_code = pthread_cond_destroy(&actors_pool->wait_for_actor);
    assert(error_code == 0);

    error_code = pthread_mutex_destroy(&actors_pool->mutex);
    assert(error_code == 0);

    // Free memory allocated for actors.
    for (size_t actor = 0; actor < actors_pool->first_empty; ++actor) {
        clear_actor(actors_pool->actors_data[actor]);
    }

    free(actors_pool->actors_queue);
    free(actors_pool);
    actors_pool = NULL;
}

static void add_actor(actor_id_t *actor_id, role_t *const role) {
    lock_mutex();

    // SIGINT was sent.
    if (actors_pool->got_sigint) {
        unlock_mutex();
        return;
    }

    *actor_id = actors_pool->first_empty;

    actors_pool->actors_data[*actor_id] = (actor_t *) malloc(sizeof(actor_t));
    (*actors_pool->actors_data[*actor_id]) = (actor_t) {
            .id = *actor_id,
            .is_dead = false,
            .in_queue = false,
            .role = role,
            .state = NULL
    };

    actors_pool->actors_data[*actor_id]->messages_queue =
            (cyclic_queue_t *) malloc(sizeof(cyclic_queue_t));

    *(actors_pool->actors_data[*actor_id]->messages_queue) = (cyclic_queue_t) {
            .first_empty = 0,
            .first_full = 0,
            .current_size = 0
    };

    actors_pool->first_empty++;
    actors_pool->living_actors++;
    unlock_mutex();
}

static void clear_actor(actor_t *actor) {
    while (actor->messages_queue->current_size > 0) {
        free(get_message(actor->messages_queue));
    }

    free(actor->messages_queue);
    free(actor);
}

// Performs first message of given actor.
void perform_message(actor_t *current_actor, message_t *message) {
    if (message->message_type == MSG_SPAWN) {
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

        if (!current_actor->is_dead) {
            actors_pool->living_actors--;
        }
        current_actor->is_dead = true;

        unlock_mutex();
    }
    else {
        current_actor->role->prompts[message->message_type](
                &current_actor->state, message->nbytes, message->data);
    }

    free(message);
    lock_mutex();
    current_actor->in_queue = false;
    actors_pool->messages_in_system--;

    // Tries to requeue actor.
    queue_add_actor(actors_pool->actors_queue, current_actor->id);
    unlock_mutex();
}

// Thread work loop.
static void *thread_loop(void *d) {
    (void) d;
    int error_code;

    lock_mutex();
    // Keep working if any actor is alive
    // or some messages had been added before all actors died.
    while (thread_keep_working()) {
        // Sleep when there are no actors.
        while (actors_pool->actors_queue->current_size == 0
               && thread_keep_working()) {
            actors_pool->waiting_for_actor++;
            error_code = pthread_cond_wait(&actors_pool->wait_for_actor,
                                           &actors_pool->mutex);
            assert(error_code == 0);

            actors_pool->waiting_for_actor--;
        }
        // Break for threads sleeping on conditional.
        if (!thread_keep_working()) {
            break;
        }

        // Perform first actor's message.
        actor_id_t current_actor_id = queue_get_actor(actors_pool->actors_queue);
        actor_t *current_actor = actors_pool->actors_data[current_actor_id];
        message_t *message = get_message(current_actor->messages_queue);
        unlock_mutex();

        thread_actor_id = current_actor_id;

        perform_message(current_actor, message);

        thread_actor_id = -1;

        lock_mutex();
    } // Thread leaves with mutex.


    // Job here is done, wake other threads.
    if (actors_pool->waiting_for_actor > 0) {
        signal_wait_for_actor();
    }
    unlock_mutex();

    return NULL;
}

// Returns current actor's id.
actor_id_t actor_id_self() {
    assert(thread_actor_id != -1);
    return thread_actor_id;
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
    if (actors_pool != NULL) {
        return -1;
    }

    init_actors_system();

    set_sigint_handler();

    add_actor(actor, role);

    lock_mutex();
    actors_pool->living_actors--; // Undo fake actor.
    unlock_mutex();

    int error_code = send_message(*actor, (message_t) {
            .message_type = MSG_HELLO,
            .nbytes = sizeof(actor_id_t),
            .data = (void *) *actor,
    });
    assert(error_code == 0);

    return 0;
}

void actor_system_join(actor_id_t actor) {
    if (actors_pool == NULL || actor < 0
        || actor >= (actor_id_t) actors_pool->first_empty) {
        return;
    }

    destroy_actors_system();
}

// Sends message to certain actor.
int send_message(actor_id_t actor, message_t message) {
    lock_mutex();

    // SIGINT was sent.
    if (actors_pool->got_sigint) {
        unlock_mutex();
        return 0;
    }

    if (actor < 0 || actor >= (actor_id_t) actors_pool->first_empty) {
        unlock_mutex();
        return -2;
    }

    actor_t *receiving_actor = actors_pool->actors_data[actor];

    if (receiving_actor->is_dead
        || receiving_actor->messages_queue->current_size == ACTOR_QUEUE_LIMIT) {
        unlock_mutex();
        return -1;
    }

    add_message(actor, receiving_actor->messages_queue, copy_message(message));

    unlock_mutex();
    return 0;
}
