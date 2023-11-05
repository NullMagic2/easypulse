//Header definition files to query about sound card properties (number of sinks, profiles...)
#ifndef SYSTEM_QUERY_H
#define SYSTEM_QUERY_H

#include <pulse/pulseaudio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

//Structures to get source port information (e.g, line in, microphone...)
//Used by get_source_port_info, get_active_port, and get_source_ports.
typedef struct pa_port_info {
    char *name;           // Port name
    char *description;    // Port description
    bool is_active;       // Is this the active port
} pa_port_info;

typedef struct pa_source_info_list {
    pa_port_info *ports;  // Array of ports
    int num_ports;        // Number of ports
    bool done;            // Indicates if the callback has been called
} pa_source_info_list;

void print_proplist(const pa_proplist *p);                                 // Utility function to print all properties in the proplist
uint32_t get_output_device_count(void);                                    //Gets the number of output devices in the system.
uint32_t get_input_device_count(void);                                     //Gets the number of input devices in the system.
uint32_t get_profile_count(uint32_t card_index);                           //Gets the number of profiles for a given soundcard.
pa_sink_info **get_available_output_devices();                             //Gets the total available sinks (output devices) for this system.
pa_source_info **get_available_input_devices();                            //Gets the total available sources (input devices) for this system.
const char* get_alsa_output_name(const char *sink_name);                   //Gets the corresponding alsa name of a pulseaudio sink (output device).
pa_source_info *get_input_device_by_name(const char *source_name);         //Gets the corresponding alsa name of a pulseaudio source (input device).
pa_source_info_list* get_source_port_info();                               //Returns which ports in the source are available (mic, line in...).
const char* get_alsa_input_name(const char *source_name);                  //Gets the corresponding alsa name of a pulseaudio source (input device).
char** get_input_channel_names(const char *alsa_id, int num_channels);     //Returns the channel names of an input device.
char** get_output_channel_names(const char *sink_name, int num_channels);  //Returns the channel names of an output device.

int get_min_input_channels(const char *alsa_id,
const pa_source_info *source_info);                                        //Gets the maximum output channels an ALSA card supports.

int get_max_input_channels(const char *alsa_id,
const pa_source_info *source_info);                                        //Gets the maximum output channels an ALSA card supports.

int get_max_output_channels(const char *alsa_id,
const pa_sink_info *sink_info);                                            //Gets the maximum output channels an ALSA card supports.

int get_min_output_channels(const char *alsa_id,
const pa_sink_info *sink_info);                                            //Gets the maximum output channels an ALSA card supports.

const char* get_alsa_input_id(const char *source_name);                    //Gets the alsa input id based on the pulseaudio channel name.

const char* get_alsa_output_id(const char *sink_name);                     //Gets the alsa output id based on the pulseaudio channel name.

int get_output_sample_rate(const char *alsa_id,
const pa_sink_info *sink_info);                                            //Gets the sample rate of a pulseaudio sink (output device).

uint32_t get_input_sample_rate(const char *source_name);                   //Gets the sample rate of a pulseaudio source (input device).
pa_sink_info *get_output_device_by_name(const char *sink_name);            //Gets output device by name.

void delete_output_devices(pa_sink_info **sinks);                          //Releases memory for allocated output devices.
void delete_input_devices(pa_source_info **sources);                       //Releases memory for allocated input devices.
pa_volume_t get_channel_volume(const pa_sink_info *sink_info,
unsigned int channel_index);                                               //Retrieves the volume of a given channel.


#endif
