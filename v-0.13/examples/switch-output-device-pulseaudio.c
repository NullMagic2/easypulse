#include <stdio.h>
#include <pulse/pulseaudio.h>
#include <stdlib.h>
#include <string.h>

static pa_threaded_mainloop *mainloop;
static pa_context *context;
static int sink_count = 0;
static char **sink_names = NULL;

void sink_list_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    (void) c;
    (void) userdata;
    if (eol > 0) {
        int chosen;
        printf("Enter the number of the sink to set as default: ");
        scanf("%d", &chosen);

        if (chosen >= 0 && chosen < sink_count) {
            pa_context_set_default_sink(c, sink_names[chosen], NULL, NULL);
        }
        pa_threaded_mainloop_signal(mainloop, 0);
        return;
    }

    sink_names = realloc(sink_names, sizeof(char*) * (sink_count + 1));
    sink_names[sink_count] = strdup(info->name);
    printf("%d: %s (%s)\n", sink_count++, info->name, info->description);
}

void context_state_cb(pa_context *c, void *userdata) {
    (void) userdata;

    if (pa_context_get_state(c) == PA_CONTEXT_READY) {
        pa_operation_unref(pa_context_get_sink_info_list(c, sink_list_cb, NULL));
    }
}

int main(void) {
    mainloop = pa_threaded_mainloop_new();
    pa_mainloop_api *mainloop_api = pa_threaded_mainloop_get_api(mainloop);
    context = pa_context_new(mainloop_api, "sink_switcher_threaded");

    pa_context_set_state_callback(context, context_state_cb, NULL);
    pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);
    pa_threaded_mainloop_start(mainloop);

    pa_threaded_mainloop_lock(mainloop);
    pa_threaded_mainloop_wait(mainloop);  // Wait until sink_list_cb signals completion

    for (int i = 0; i < sink_count; i++) {
        free(sink_names[i]);
    }
    free(sink_names);

    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_threaded_mainloop_unlock(mainloop);
    pa_threaded_mainloop_stop(mainloop);
    pa_threaded_mainloop_free(mainloop);
    return 0;
}
