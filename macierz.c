#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "cacti.h"

// Overloading MSG_SUM for admin and normal actor.
#define MSG_SUM (message_type_t)0x1

// Admin actor's messages.
#define MSG_INIT (message_type_t)0x2
#define MSG_WAIT (message_type_t)0x3

// Calculating actors' messages.
#define MSG_DATA (message_type_t)0x2


// Role of admin actor managing calculating actors.
static role_t admin_role;

// Role of actor calculating values in matrix.
static role_t actor_role;

// Structure for storing calculation information.
typedef struct {
    // Calculated sum.
    long sum;

    // Row number of calculated sum.
    int row_number;
} calculating_t;

// Each calculating actor gets column and times to calculate values.
typedef struct {
    // Id of actor managing next column.
    actor_id_t next_id;

    // Number of calculated values.
    int already_calculated;

    // Number of rows.
    int row_number;

    // Corresponding column.
    int *column_values;

    // Corresponding waiting times.
    int *column_times;
} actor_state_t;

// State of admin actor.
typedef struct {
    // Stores id of most recent actor who sent MSG_WAIT to admin.
    actor_id_t previous_id;

    // Which column current actor will get.
    int current_column;

    // Number of columns.
    int column_number;

    // Number of rows.
    int row_number;

    // All columns.
    int **columns;

    // All waiting times.
    int **times;

    // Number of calculated values.
    int already_calculated;

    // Array of calculated sums.
    long *calculated_sums;
} initial_message_t;


// Hello message handler.
// Messages admin to get column.
static void message_hello(void **stateptr, size_t nbytes, void *data) {
    (void) stateptr;
    (void) nbytes;
    actor_id_t admin_id = (actor_id_t) data;

    message_t message = {
            .message_type = MSG_WAIT,
            .nbytes = sizeof(actor_id_t),
            .data = (void *) actor_id_self()
    };

    int error_code = send_message(admin_id, message);
    assert(error_code == 0);
}

// Empty message hello handler.
static void message_hello_admin(void **stateptr, size_t nbytes, void *data) {
    (void) stateptr;
    (void) nbytes;
    (void) data;
}

// Creating admin node.
static void message_init(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;

    initial_message_t *initial_data = (initial_message_t *) data;
    *stateptr = (void *) initial_data;

    // Create all necessary actors.
    for (int column = 0; column < initial_data->column_number; ++column) {
        message_t message = {
                .message_type = MSG_SPAWN,
                .nbytes = sizeof(role_t *),
                .data = (void *) &actor_role
        };

        int error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
    }
}

// Admin actor creates next calculating actor state.
// If all actors are initialized calls first actor to start calculating.
static void message_wait(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    actor_id_t actor_id = (actor_id_t) data;

    initial_message_t *initial_data = (initial_message_t *) *stateptr;

    actor_state_t *next_state = (actor_state_t *) malloc(sizeof(actor_state_t));
    *next_state = (actor_state_t) {
            .next_id = initial_data->previous_id,
            .already_calculated = 0,
            .row_number = initial_data->row_number,
            .column_values = initial_data->columns[initial_data->current_column],
            .column_times = initial_data->times[initial_data->current_column]
    };

    message_t message = {
            .message_type = MSG_DATA,
            .nbytes = sizeof(actor_state_t *),
            .data = next_state
    };

    int error_code = send_message(actor_id, message);
    assert(error_code == 0);

    initial_data->current_column--;
    initial_data->previous_id = actor_id;

    // If each column has corresponding actor, start calculating.
    if (initial_data->current_column == -1) {
        for (int row = 0; row < initial_data->row_number; ++row) {
            calculating_t *current_calculation =
                    (calculating_t *) malloc(sizeof(calculating_t));

            // 0 is neutral start value.
            *current_calculation = (calculating_t) {
                    .row_number = row,
                    .sum = 0,
            };

            message = (message_t) {
                    .message_type = MSG_SUM,
                    .nbytes = sizeof(calculating_t *),
                    .data = current_calculation
            };

            error_code = send_message(actor_id, message);
            assert(error_code == 0);
        }
    }
}

// Saves sent data as actor's state.
static void message_data(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    *stateptr = data;
}

// Signals actor to start calculating
// Passed data is calculating_t
static void message_sum(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    actor_state_t *current_state = (actor_state_t *) *stateptr;
    calculating_t *current_calculation = (calculating_t *) data;

    // Sleep for given time.
    usleep(current_state->column_times[current_calculation->row_number]);

    current_state->already_calculated++;
    current_calculation->sum +=
            (long) current_state->column_values[current_calculation->row_number];

    message_t message = {
            .message_type = MSG_SUM,
            .nbytes = sizeof(calculating_t *),
            .data = current_calculation
    };

    int error_code = send_message(current_state->next_id, message);
    assert(error_code == 0);

    // There will be no more calculations.
    if (current_state->already_calculated == current_state->row_number) {
        message = (message_t) {
                .message_type = MSG_GODIE,
                .nbytes = 0,
                .data = NULL
        };

        error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
        free(current_state);
    }
}

// Admin actor gets calculated sums, prints and destroys them.
static void message_sum_admin(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    initial_message_t *admin_data = (initial_message_t *) *stateptr;
    calculating_t *current_calculation = (calculating_t *) data;

    admin_data->already_calculated++;
    admin_data->calculated_sums[current_calculation->row_number] =
            current_calculation->sum;

    free(current_calculation);

    // All sums are calculated.
    if (admin_data->already_calculated == admin_data->row_number) {
        for (int row = 0; row < admin_data->row_number; ++row) {
            printf("%ld\n", admin_data->calculated_sums[row]);
        }

        message_t message = {
                .message_type = MSG_GODIE,
                .nbytes = 0,
                .data = NULL
        };

        int error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
    }
}

int main() {
    int k, n; // rows, columns
    scanf("%d", &k);
    scanf("%d", &n);

    // Values and times per column.
    int **values = (int **) malloc(n * sizeof(int *));
    int **times = (int **) malloc(n * sizeof(int *));

    for (int column = 0; column < n; ++column) {
        values[column] = (int *) malloc(k * sizeof(int));
        times[column] = (int *) malloc(k * sizeof(int));
    }

    for (int row = 0; row < k; ++row) {
        for (int column = 0; column < n; ++column) {
            scanf("%d %d", &values[column][row], &times[column][row]);
            // Microseconds to milliseconds conversion.
            times[column][row] *= 1000;
        }
    }

    int error_code;
    actor_id_t actor_id = -1;

    actor_role.nprompts = 3;
    actor_role.prompts = (act_t[]) {
            message_hello,
            message_sum,
            message_data
    };

    admin_role.nprompts = 4;
    admin_role.prompts = (act_t[]) {
            message_hello_admin,
            message_sum_admin,
            message_init,
            message_wait
    };

    error_code = actor_system_create(&actor_id, &admin_role);
    assert(error_code == 0);

    initial_message_t *initial_message =
            (initial_message_t *) malloc(sizeof(initial_message_t));
    *initial_message = (initial_message_t) {
            .previous_id = actor_id,
            .current_column = n - 1,
            .column_number = n,
            .row_number = k,
            .columns = values,
            .times = times,
            .already_calculated = 0,
            .calculated_sums = (long *) malloc(k * sizeof(long))
    };

    message_t message = {
            .message_type = MSG_INIT,
            .nbytes = sizeof(initial_message_t *),
            .data = (void *) initial_message
    };

    error_code = send_message(actor_id, message);
    assert(error_code == 0);

    actor_system_join(actor_id);

    // Deallocate memory.
    for (int column = 0; column < n; ++column) {
        free(values[column]);
        free(times[column]);
    }

    free(values);
    free(times);
    free(initial_message->calculated_sums);
    free(initial_message);

    return 0;
}
