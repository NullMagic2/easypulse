/**
 * @file get-card-by-name-pulseaudio.c
 * @brief PulseAudio Card Information Demo
 *
 * This program demonstrates how to interact with the PulseAudio (PA) sound server
 * to retrieve information about a specific sound card by its name. It utilizes the
 * PulseAudio API to establish a connection with the PA server, queries for a card
 * by name, and then prints out the name and active profile of the card.
 *
 * The program employs the asynchronous PA API with a threaded main loop to handle
 * the communication with the PA server.
 *
 * The card name should be the technical name as recognized by PulseAudio, which
 * can be obtained using `pactl list cards` or `pacmd list-cards`.
 *
 *
 * @author Mbyte2
 * @date November 18, 2023
 */
#include <stdio.h>
#include <pulse/pulseaudio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pa_threaded_mainloop *mainloop;
    pa_context *context;
    char **card_names;
    size_t num_cards;
} pa_userdata;

static void context_state_cb(pa_context *context, void *userdata) {
    pa_userdata *ud = (pa_userdata *) userdata;
    switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY:
            pa_threaded_mainloop_signal(ud->mainloop, 0);
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            pa_threaded_mainloop_signal(ud->mainloop, 0);
            break;
        default:
            break;
    }
}

static void card_list_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata) {
    (void) c;
    pa_userdata *ud = (pa_userdata *) userdata;

    if (eol > 0) {
        pa_threaded_mainloop_signal(ud->mainloop, 0);
        return;
    }

    if (i) {
        ud->card_names = realloc(ud->card_names, sizeof(char *) * (ud->num_cards + 1));
        ud->card_names[ud->num_cards] = strdup(i->name);
        ud->num_cards++;
    }
}

static void card_info_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata) {
    (void) c;
    pa_userdata *ud = (pa_userdata *) userdata;

    if (eol > 0) {
        pa_threaded_mainloop_signal(ud->mainloop, 0);
        return;
    }

    if (i) {
        printf("Card Name: %s\n", i->name);
        if (i->active_profile) {
            printf("Active Profile: %s\n", i->active_profile->name);
        }
        printf("\n");
    }
}

int main() {
    pa_userdata userdata = {0};

    userdata.mainloop = pa_threaded_mainloop_new();
    userdata.context = pa_context_new(pa_threaded_mainloop_get_api(userdata.mainloop), "PA Demo");

    pa_context_set_state_callback(userdata.context, context_state_cb, &userdata);

    if (pa_context_connect(userdata.context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        fprintf(stderr, "PulseAudio connection failed\n");
        pa_threaded_mainloop_free(userdata.mainloop);
        return 1;
    }

    pa_threaded_mainloop_start(userdata.mainloop);
    pa_threaded_mainloop_lock(userdata.mainloop);

    while (pa_context_get_state(userdata.context) != PA_CONTEXT_READY) {
        pa_threaded_mainloop_wait(userdata.mainloop);
    }

    // List all cards
    pa_operation *op = pa_context_get_card_info_list(userdata.context, card_list_cb, &userdata);
    if (op) {
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
            pa_threaded_mainloop_wait(userdata.mainloop);
        }
        pa_operation_unref(op);
    }

    // Get detailed info for each card
    for (size_t i = 0; i < userdata.num_cards; i++) {
        op = pa_context_get_card_info_by_name(userdata.context, userdata.card_names[i], card_info_cb, &userdata);
        if (op) {
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                pa_threaded_mainloop_wait(userdata.mainloop);
            }
            pa_operation_unref(op);
        }
    }

    pa_threaded_mainloop_unlock(userdata.mainloop);
    pa_threaded_mainloop_stop(userdata.mainloop);

    pa_context_disconnect(userdata.context);
    pa_context_unref(userdata.context);
    pa_threaded_mainloop_free(userdata.mainloop);

    for (size_t i = 0; i < userdata.num_cards; i++) {
        free(userdata.card_names[i]);
    }
    free(userdata.card_names);

    return 0;
}

