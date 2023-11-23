/**
 * @file pa_sink_list.c
 * @brief Demonstrates listing all available PulseAudio sink inputs and their active profiles.
 *
 * This program connects to the PulseAudio server and enumerates all available sink inputs.
 * For each sink input, it retrieves and displays its name and active profile. The program
 * showcases the basic use of the PulseAudio API in a C program for audio device management.
 *
 * Author: Mbyte2
 * Date: November 21, 2023
 *
 */

#include <stdio.h>
#include <pulse/pulseaudio.h>

// Callback function for sink input info
void sink_input_info_cb(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {
    (void) c;
    (void) userdata;

    if (eol > 0) {
        pa_mainloop_quit((pa_mainloop*)userdata, 0);
        return;
    }

    printf("Sink Input #%u\n", i->index);
    printf("Name: %s\n", i->name);
}

// Callback function for server info
void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata) {
    (void) i;
    (void) userdata;

    pa_operation *o;

    if (!(o = pa_context_get_sink_input_info_list(c, sink_input_info_cb, userdata))) {
        fprintf(stderr, "pa_context_get_sink_input_info_list() failed\n");
        return;
    }

    pa_operation_unref(o);
}

// State callback for connection
void state_cb(pa_context *c, void *userdata) {
    (void) c;
    (void) userdata;

    pa_context_state_t state;
    state = pa_context_get_state(c);

    switch (state) {
        case PA_CONTEXT_READY:
            server_info_cb(c, NULL, userdata);
            break;

        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            pa_mainloop_quit((pa_mainloop*)userdata, 0);
            break;

        default:
            break;
    }
}

int main() {
    pa_mainloop *m = NULL;
    pa_mainloop_api *api = NULL;
    pa_context *context = NULL;
    int ret = 1;

    // Create a mainloop API and connection to the default server
    m = pa_mainloop_new();
    api = pa_mainloop_get_api(m);
    context = pa_context_new(api, "Sink Input List");

    // Connect to the PulseAudio server
    pa_context_connect(context, NULL, 0, NULL);

    // Set the callback so we can wait for the server to be ready
    pa_context_set_state_callback(context, state_cb, m);

    if (pa_mainloop_run(m, &ret) < 0) {
        fprintf(stderr, "Failed to run mainloop\n");
        goto quit;
    }

quit:
    if (context) {
        pa_context_unref(context);
    }

    if (m) {
        pa_mainloop_free(m);
    }

    return ret;
}
