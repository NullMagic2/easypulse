/**
 * @file alsa-mapper_pulseaudio-api.c
 * @brief Fetches and displays UDEV descriptions and ALSA names for PulseAudio sinks.
 *
 * This program interfaces with both the ALSA and PulseAudio APIs to retrieve information about available audio sinks.
 * It lists each sink's UDEV description, as well as its corresponding ALSA hardware (hw) name and a more user-friendly
 * ALSA name. This is useful for applications that need to display detailed information about the audio devices in the system,
 * especially when working with systems where multiple audio devices are present.
 *
 * The program utilizes the PulseAudio asynchronous API to fetch sink information and then queries ALSA to get a friendly
 * name for each sink. The ALSA friendly name is typically more readable and user-friendly compared to the default hardware
 * name provided by ALSA.
 *
 * Functions:
 * - get_alsa_friendly_name: Retrieves a user-friendly name for an ALSA card.
 * - sink_info_cb: Callback function for processing and displaying each sink's information.
 * - context_state_cb: Callback function to handle the state changes of the PulseAudio context.
 * - main: Sets up the PulseAudio main loop and context, and runs the main loop.
 *
 * Usage:
 * - The program does not require any command-line arguments.
 * - On execution, it lists all available PulseAudio sinks with their UDEV descriptions and ALSA names.
 *
 * @author Mbyte2
 * @date November 13, 2023
 */

#include <stdio.h>
#include <pulse/pulseaudio.h>
#include <alsa/asoundlib.h>
#include <string.h>

static const char* get_alsa_friendly_name(int card_num) {
    snd_ctl_t *ctl;
    char name[128];
    snprintf(name, sizeof(name), "hw:%d", card_num);

    if (snd_ctl_open(&ctl, name, 0) < 0) {
        return NULL;
    }

    snd_ctl_card_info_t *info;
    snd_ctl_card_info_alloca(&info);
    if (snd_ctl_card_info(ctl, info) < 0) {
        snd_ctl_close(ctl);
        return NULL;
    }

    const char *friendly_name = strdup(snd_ctl_card_info_get_name(info));
    snd_ctl_close(ctl);
    return friendly_name;
}

static void sink_info_cb(pa_context *c, const pa_sink_info *info, int eol, void *userdata) {
    (void) userdata;

    if (eol > 0) {
        pa_context_disconnect(c);
        return;
    }

    const char *udev_description = pa_proplist_gets(info->proplist, "device.description");
    const char *card_str = pa_proplist_gets(info->proplist, "alsa.card");
    const char *device_str = pa_proplist_gets(info->proplist, "alsa.device");

    int card_num = card_str ? atoi(card_str) : -1;
    const char *friendly_name = card_num != -1 ? get_alsa_friendly_name(card_num) : NULL;

    if (udev_description && card_str && device_str) {
        printf("Sink: %s, UDEV description: %s, ALSA name: hw:%s,%s", info->name, udev_description, card_str, device_str);
        if (friendly_name) {
            printf(", Friendly ALSA name: %s", friendly_name);
            free((void*)friendly_name);
        }
        printf("\n");
    } else if (udev_description) {
        printf("Sink: %s, UDEV description: %s, Incomplete ALSA name information.\n", info->name, udev_description);
    }
}

static void context_state_cb(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            pa_context_get_sink_info_list(c, sink_info_cb, NULL);
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            pa_mainloop_quit((pa_mainloop*)userdata, 0);
            break;
        default:
            break;
    }
}

int main(void) {
    pa_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;

    mainloop = pa_mainloop_new();
    mainloop_api = pa_mainloop_get_api(mainloop);
    context = pa_context_new(mainloop_api, "udev_description_fetcher");

    pa_context_set_state_callback(context, context_state_cb, mainloop);
    pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);

    pa_mainloop_run(mainloop, NULL);

    pa_context_unref(context);
    pa_mainloop_free(mainloop);

    return 0;
}
