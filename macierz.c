#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include "cacti.h"

#define MSG_INIT (message_type_t)0x1
#define MSG_WAIT (message_type_t)0x2
#define MSG_DATA (message_type_t)0x3
#define MSG_SUM (message_type_t)0x4

role_t admin_role;

// All actors will share same role.
role_t actor_role;


typedef struct {
    long sum;
    int row_number;
} calculating_t;

// Each actor gets corresponding column and times to calculate values.
typedef struct {
    actor_id_t parent_id;
    bool is_last;
    int already_calculated;
    int row_number;
    int *column_values;
    int *column_times;
} actor_state_t;

typedef struct {
    actor_id_t previous_id;
    int current_column;
    int column_number;
    int row_number;
    int **columns;
    int **times;

    int already_calculated;
    // Array of calculated sums
    long *calculated_sums;
} initial_message_t;

void print_column(int *column, int rows) {
    for (int i = 0; i < rows; ++i) {
        printf("%d ", column[i]);
    }

    printf("\n");
}

// Hello message handler.
// Saves parent's id and sends MSG_WAIT to parent.
void message_hello(void **stateptr, size_t nbytes, void *data) {
    (void) stateptr;
    (void) nbytes;
    actor_id_t admin_id = (actor_id_t) data;

//    actor_state_t *current_state =
//            (actor_state_t *) malloc(sizeof(actor_state_t));
//    *stateptr = (void *) current_state;

    message_t message = {
            .message_type = MSG_WAIT,
            .nbytes = sizeof(actor_id_t),
            .data = (void *) actor_id_self()
    };

    int error_code = send_message(admin_id, message);
    assert(error_code == 0);
}

// Creating admin node.
void message_init(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
//    (void) data;
//
//    actor_state_t *current_state =
//            (actor_state_t *) malloc(sizeof(actor_state_t));
//
//    current_state->parent_id = -1;
//    *stateptr = (void *) current_state;
//
//    // i tu message data ?



    initial_message_t *initial_data = (initial_message_t *) data;
    *stateptr = (void *) initial_data; // todo ??

    // Create all necessary actors.
    for (int i = 0; i < initial_data->column_number; ++i) {
        message_t message = {
                .message_type = MSG_SPAWN,
                .nbytes = sizeof(role_t *),
                .data = (void *) &actor_role
        };

        int error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
    }
}

// In admin node
void message_wait(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    actor_id_t actor_id = (actor_id_t) data;

    initial_message_t *initial_data = (initial_message_t *) *stateptr;

    bool is_last = initial_data->current_column + 1 == initial_data->column_number;

    // Last actor sends results to admin actor.
    if (is_last) {
        initial_data->previous_id = actor_id_self();
    }

    actor_state_t *next_state = (actor_state_t *) malloc(sizeof(actor_state_t));
    *next_state = (actor_state_t) {
            .parent_id = initial_data->previous_id,
            .is_last = is_last,
            .already_calculated = 0,
            .row_number = initial_data->row_number,
            .column_values = initial_data->columns[initial_data->current_column],
            .column_times = initial_data->columns[initial_data->current_column]
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

    // Each column has corresponding actor, start calculating.
    if (initial_data->current_column == -1) {
        for (int i = 0; i < initial_data->row_number; ++i) {
            calculating_t *current_calculation =
                    (calculating_t *) malloc(sizeof(calculating_t));

            // 0 is neutral value
            *current_calculation = (calculating_t) {
                    .row_number = i,
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

// Loads data
void message_data(void **stateptr, size_t nbytes, void *data) {
    *stateptr = data;
    // XD todo ???
}

// Signals actor to start calculating
// Passed data is calculating_t
void message_sum(void **stateptr, size_t nbytes, void *data) {
    actor_state_t *current_state = (actor_state_t *) *stateptr;
    calculating_t *current_calculation = (calculating_t *) data;

    printf("SLEEPING FOR %d\n", current_state->column_times[current_calculation->row_number]);
    usleep(current_state->column_times[current_calculation->row_number]);

    current_state->already_calculated++;
    current_calculation->sum +=
            current_state->column_values[current_calculation->row_number];

    if (current_state->is_last) {
        // todo jakies inne wysylanie do admina
        printf("suma wyliczona dla row %d jest %ld\n",
               current_calculation->row_number, current_calculation->sum);
    }
//    else {
    message_t message = {
            .message_type = MSG_SUM,
            .nbytes = sizeof(calculating_t *),
            .data = current_calculation
    };

    int error_code = send_message(current_state->parent_id, message);
    assert(error_code == 0);
//    }

    if (current_state->already_calculated == current_state->row_number) {
        message_t message = {
                .message_type = MSG_GODIE,
                .nbytes = 0,
                .data = NULL
        };

        free(current_state);
        error_code = send_message(actor_id_self(), message);
        assert(error_code == 0);
    }
}

void message_sum_admin(void **stateptr, size_t nbytes, void *data) {
    (void) nbytes;
    printf("--------------------------------------------------------------\n");
    initial_message_t *admin_data = (initial_message_t *) *stateptr;
    calculating_t *current_calculation = (calculating_t *) data;

    admin_data->already_calculated++;
    admin_data->calculated_sums[current_calculation->row_number] = current_calculation->sum;

    free(current_calculation);

    // Last missing sum.
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
    int k, n; // rows, columns  2, 3
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
        }
    }

    int error_code;
    actor_id_t actor_id = -1;

    actor_role.nprompts = 5;
    actor_role.prompts = (act_t[]) {
            message_hello,
            message_init,
            message_wait,
            message_data,
            message_sum
    };
    admin_role.nprompts = 5;
    admin_role.prompts = (act_t[]) {
            message_hello,
            message_init,
            message_wait,
            message_data,
            message_sum_admin
    };

    error_code = actor_system_create(&actor_id, &admin_role);
    assert(error_code == 0);

    initial_message_t *initial_message =
            (initial_message_t *) malloc(sizeof(initial_message_t));
    *initial_message = (initial_message_t) {
            .previous_id = -2137,
            .current_column = n - 1,
            .column_number = n,
            .row_number = k,
            .columns = values,
            .times = times,
            .already_calculated = 0,
            .calculated_sums = (long *) malloc(n * sizeof(long))
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
