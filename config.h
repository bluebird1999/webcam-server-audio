/*
 * config_audio.h
 *
 *  Created on: Sep 1, 2020
 *      Author: ning
 */

#ifndef SERVER_AUDIO_CONFIG_H_
#define SERVER_AUDIO_CONFIG_H_

/*
 * header
 */

/*
 * define
 */
#define		CONFIG_AUDIO_MODULE_NUM			2
#define		CONFIG_AUDIO_PROFILE			0
#define		CONFIG_AUDIO_CAPTURE			1

#define 	CONFIG_AUDIO_PROFILE_PATH					"config/audio_profile.config"
#define 	CONFIG_AUDIO_CAPTURE_PATH					"config/audio_capture.config"

/*
 * structure
 */
typedef struct audio_profile_t {
	int							enable;
	int							aec_enable;
	int							ns_enable;
	int							ns_level;
} audio_profile_t;

typedef struct audio_config_t {
	int							status;
	audio_profile_t				profile;
	struct rts_audio_attr 		capture;
} audio_config_t;

/*
 * function
 */
int config_audio_read(audio_config_t *aconfig);
int config_audio_set(int module, void *arg);

#endif /* SERVER_AUDIO_CONFIG_H_ */
