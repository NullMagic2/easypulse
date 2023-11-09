/**
 * @file volume-change-pulseaudio.c
 * @brief Change the volume of the default PulseAudio sink.
 *
 * This program prompts the user for a desired volume level, displays the current volume
 * of the default sink, sets the volume of the default sink to the user's input, and then
 * displays the volume after the change.
 */

#include <stdio.h>
#include <pulse/pulseaudio.h>
#include <stdlib.h>

static pa_threaded_mainloop *mainloop;
static pa_context *context;

/**
 * @brief Callback function to set the volume of the default sink.
 *
 * This function fetches the current volume of the default sink, displays it,
 * then prompts the user for a desired volume level, sets the volume, and finally
 * displays the volume after the change.
 *
 * @param c     The PulseAudio context.
 * @param info  Information about the current sink.
 * @param eol   End-of-list flag.
 * @param userdata  User data (unused).
 */
void set_volume_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    (void) c;
    (void) userdata;

    if (eol > 0) {
        pa_threaded_mainloop_signal(mainloop, 0);
        return;
    }

    // Prompt the user for the desired volume level first
    int volume_percentage;
    printf("Enter the desired volume (0-100): ");
    scanf("%d", &volume_percentage);

    // Display the current volume percentage
    int volume_percentage_before = (int)(100.0f * pa_cvolume_avg(&info->volume) / PA_VOLUME_NORM + 0.5);
    printf("Volume before change: %d%%\n", volume_percentage_before);

    // Set the volume based on user input
    pa_cvolume volume;
    pa_cvolume_set(&volume, info->channel_map.channels, (volume_percentage * PA_VOLUME_NORM) / 100);
    pa_context_set_sink_volume_by_index(c, info->index, &volume, NULL, NULL);

    // Display the volume after the change
    int volume_percentage_after = (int)(100.0f * pa_cvolume_avg(&volume) / PA_VOLUME_NORM + 0.5);
    printf("Volume after change: %d%%\n", volume_percentage_after);

    pa_threaded_mainloop_signal(mainloop, 0);
}

/**
 * @brief Callback function for context state changes.
 *
 * This function checks if the context is ready and then triggers the retrieval
 * of the default sink's information.
 *
 * @param c     The PulseAudio context.
 * @param userdata  User data (unused).
 */
void context_state_cb(pa_context *c, void *userdata) {
    (void) c;
    (void) userdata;

    if (pa_context_get_state(c) == PA_CONTEXT_READY) {
        pa_operation_unref(pa_context_get_sink_info_by_name(c, "@DEFAULT_SINK@", set_volume_cb, NULL));
    }
}

int main(void) {
    // Initialize PulseAudio threaded mainloop and context
    mainloop = pa_threaded_mainloop_new();
    pa_mainloop_api *mainloop_api = pa_threaded_mainloop_get_api(mainloop);
    context = pa_context_new(mainloop_api, "volume_changer_threaded");

    // Set context state callback and connect the context
    pa_context_set_state_callback(context, context_state_cb, NULL);
    pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);

    // Start the threaded mainloop
    pa_threaded_mainloop_start(mainloop);

    // Lock the mainloop and wait for operations to complete
    pa_threaded_mainloop_lock(mainloop);
    pa_threaded_mainloop_wait(mainloop);

    // Cleanup and free resources
    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_threaded_mainloop_unlock(mainloop);
    pa_threaded_mainloop_stop(mainloop);
    pa_threaded_mainloop_free(mainloop);
    return 0;
}
