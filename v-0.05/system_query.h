//Header definition files to query about sound card properties (number of sinks, profiles...)
#ifndef SYSTEM_QUERY_H
#define SYSTEM_QUERY_H

#include <pulse/pulseaudio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

uint32_t get_device_count(void);                                           //Gets the number of devices in the system.
uint32_t get_profile_count(uint32_t card_index);                           //Gets the number of profiles for a given soundcard.
pa_sink_info **get_available_sinks();                                      //Gets the total available sinks for this system.
const char* get_alsa_name(const char *sink_name);                          //Gets the corresponding alsa name of a pulseaudio card.
int get_max_channels(const char *alsa_id, const pa_sink_info *sink_info);  //Gets the maximum channels an ALSA card supports.
int get_min_channels(const char *alsa_id, const pa_sink_info *sink_info);  //Gets the maximum channels an ALSA card supports.
const char* get_alsa_id(const char *sink_name);                            //Gets the alsa id based on the pulseaudio channel name.
int get_sample_rate(const char *alsa_id, const pa_sink_info *sink_info);   //Gets the sample rate of a pulseaudio sink.
void delete_devices(pa_sink_info **sinks);                                 //Releases memory for allocated devices.

#endif
