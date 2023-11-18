/**
 * @file change_global_sample_rate.c
 * @brief Adjusts the global PulseAudio sample rate.
 *
 * This program retrieves and displays the current global sample rate for PulseAudio.
 * It then prompts the user to enter a new sample rate. Upon receiving a valid input,
 * it attempts to set this new sample rate as the global sample rate in PulseAudio's
 * configuration files. The program first tries to update the system-wide configuration
 * and then falls back to the user's local configuration if necessary.
 *
 * @return Returns 0 on successful execution, 1 on failure or invalid input.
 */

#include <stdint.h>
#include <stdio.h>
#include "../easypulse_core.h"


int main() {
    // Fetch the global playback sample rate from the default PulseAudio configuration file
    int sample_rate = get_pulseaudio_global_playback_rate(NULL);
    printf("[DEBUG, main] sample rate is: %i\n", sample_rate);

    if (sample_rate > 0) {
        printf("Current global playback sample rate: %d Hz\n", sample_rate);
    } else {
        printf("Failed to retrieve the current global playback sample rate.\n");
        return 1;
    }

    // Prompt the user for a new sample rate
    printf("Enter the new sample rate to set: ");
    int new_sample_rate;
    if (scanf("%d", &new_sample_rate) != 1) {
        printf("Invalid input.\n");
        return 1;
    }

    // Set the new global sample rate
    if (manager_set_pulseaudio_global_rate(new_sample_rate) == 0) {
        printf("Sample rate successfully set to %d Hz.\n", new_sample_rate);
    } else {
        printf("Failed to set the new sample rate.\n");
    }

    return 0;
}
