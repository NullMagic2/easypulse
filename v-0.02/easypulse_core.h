/**
 * @file core.h
 * @brief EasyPulse Library Header.
 *
 * EasyPulse is a library designed to provide pseudo-object oriented programming
 * functions to simplify access to PulseAudio.
 */

#ifndef EASPYPULSE_CORE_H
#define EASPYPULSE_CORE_H
#define DEBUG_MODE 0  // Debug mode flag

#include <pulse/pulseaudio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>


// Forward declaration
typedef struct PulseAudioManager PulseAudioManager;

/**
 * @brief Represents a PulseAudio sink.
 */
typedef struct {
    uint32_t index;           ///< Index of the sink.
    char *name;               ///< Name of the sink.
    char *description;       ///< Description of the sink.
    pa_cvolume volume;       ///< Volume of the sink.
    pa_channel_map channel_map; ///< Channel map of the sink.
    int mute;                ///< Mute status of the sink (1 for muted, 0 for unmuted).
    int number_of_channels; //The number of channels of the sink.
} PulseAudioSink;

/**
 * @brief Represents the main manager for PulseAudio operations.
 */
struct PulseAudioManager {
    pa_threaded_mainloop *mainloop; ///< Mainloop for PulseAudio operations.
    pa_context *context;         ///< PulseAudio context.
    PulseAudioSink *sinks;       ///< Array of available sinks.
    int sink_count;              ///< Count of available sinks.
    int pa_ready;                ///< Indicates if PulseAudio is ready (1 for ready, 2 for error).
    int sinks_loaded;            ///< Indicates if sinks are loaded (0 for not loaded, 1 for loaded successfully, 2 for error).
    int operations_pending;      // Counter for pending operations.
    int active_sink_index;       // The active sink index, i.e, the sink being used for playback.
    char *active_sink_name;      // The name of the active sink.
    uint32_t current_sink_index; // The sink being processed right now by the program. It's not necessarily the same as the playback sink.


    bool (*initialize)(PulseAudioManager *self);      ///< Function to initialize the manager.
    bool (*cleanup)(PulseAudioManager *self);         ///< Function to cleanup the manager.
    bool (*loadSinks)(PulseAudioManager *self);       ///< Function to load available sinks.
    bool (*switchSink)(PulseAudioManager *self, uint32_t sink_index); ///< Function to switch to a specified sink.
    bool (*setVolume)(PulseAudioManager *self, uint32_t sink_index, float percentage); // Function to set the volume to a specified percentage.
    void (*getActiveSink)(PulseAudioManager *manager); // Function to set the volume to a specified percentage.
    void (*iterate)(PulseAudioManager *manager, pa_operation *op);

};

/**
 * @brief Create a new PulseAudioManager instance.
 * @return A pointer to the newly created PulseAudioManager instance.
 */
PulseAudioManager* newPulseAudioManager();

/**
 * @brief Free the memory associated with a PulseAudioManager instance.
 * @param manager The PulseAudioManager instance to delete.
 */
void deletePulseAudioManager(PulseAudioManager *manager);
void server_info_cb(pa_context *c, const pa_server_info *info, void *userdata);
void getActiveSink(PulseAudioManager *manager);
bool setVolume(PulseAudioManager *self, uint32_t sink_index, float percentage);
void iterate(PulseAudioManager *manager, pa_operation *op);

#endif // CORE_H
