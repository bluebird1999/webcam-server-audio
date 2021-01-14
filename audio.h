/*
 * audio.h
 *
 *  Created on: Aug 27, 2020
 *      Author: ning
 */

#ifndef SERVER_AUDIO_AUDIO_H_
#define SERVER_AUDIO_AUDIO_H_

/*
 * header
 */

/*
 * define
 */
#define	THREAD_AUDIO			0

#define	RUN_MODE_SAVE		0
#define	RUN_MODE_MICLOUD	4
#define	RUN_MODE_MOTION		8
#define RUN_MODE_MISS		16
#define	RUN_MODE_SPEAKER	24

#define		AUDIO_INIT_CONDITION_NUM				2
#define		AUDIO_INIT_CONDITION_CONFIG				0
#define		AUDIO_INIT_CONDITION_REALTEK			1

#define		AUDIO_EXIT_CONDITION			( (1 << SERVER_MISS) | (1 << SERVER_RECORDER) )

#define DEV_START_FINISH 			"/opt/qcy/audio_resource/dev_start_finish.alaw"
#define DEV_START_ING	 			"/opt/qcy/audio_resource/dev_starting.alaw"
#define WIFI_CONNECT_SUCCEED 		"/opt/qcy/audio_resource/wifi_connect_success.alaw"
#define INTERNET_CONNECT_DEFEAT		"/opt/qcy/audio_resource/wifi_connect_failed.alaw"
#define ZBAR_SCAN_SUCCEED 			"/opt/qcy/audio_resource/scan_zbar_success.alaw"
#define ZBAR_SCAN					"/opt/qcy/audio_resource/wait_connect.alaw"
#define INSTALLING					"/opt/qcy/audio_resource/begin_update.alaw"
#define INSTALLEND					"/opt/qcy/audio_resource/success_upgrade.alaw"
#define INSTALLFAILED				"/opt/qcy/audio_resource/upgrade_failed.alaw"
#define RESET_SUCCESS				"/opt/qcy/audio_resource/reset_success.alaw"
/*
 * structure
 */

/*
 * function
 */
int audio_speaker(char *buffer, unsigned int buffer_size);

#endif /* SERVER_AUDIO_AUDIO_H_ */
