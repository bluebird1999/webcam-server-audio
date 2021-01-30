/*
 * vedio_interface.h
 *
 *  Created on: Aug 27, 2020
 *      Author: ning
 */
#ifndef SERVER_AUDIO_AUDIO_INTERFACE_H_
#define SERVER_AUDIO_AUDIO_INTERFACE_H_

/*
 * header
 */
#include "../../manager/global_interface.h"
#include "../../manager/manager_interface.h"

/*
 * define
 */
#define		SERVER_AUDIO_VERSION_STRING			"alpha-8.2"

#define		MSG_AUDIO_BASE						(SERVER_AUDIO<<16)
#define		MSG_AUDIO_SIGINT					(MSG_AUDIO_BASE | 0x0000)
#define		MSG_AUDIO_SIGINT_ACK				(MSG_AUDIO_BASE | 0x1000)
//AUDIO control message
#define		MSG_AUDIO_START						(MSG_AUDIO_BASE | 0x0010)
#define		MSG_AUDIO_START_ACK					(MSG_AUDIO_BASE | 0x1010)
#define		MSG_AUDIO_STOP						(MSG_AUDIO_BASE | 0x0011)
#define		MSG_AUDIO_STOP_ACK					(MSG_AUDIO_BASE | 0x1011)
#define		MSG_AUDIO_PROPERTY_GET				(MSG_AUDIO_BASE | 0x0012)
#define		MSG_AUDIO_PROPERTY_GET_ACK			(MSG_AUDIO_BASE | 0x1012)
#define 	MSG_AUDIO_SPEAKER_CTL_PLAY			(MSG_AUDIO_BASE | 0x0013)
#define 	MSG_AUDIO_SPEAKER_CTL_PLAY_ACK		(MSG_AUDIO_BASE | 0x1013)
#define 	MSG_AUDIO_SPEAKER_CTL_DATA			(MSG_AUDIO_BASE | 0x0014)
#define 	MSG_AUDIO_SPEAKER_CTL_DATA_ACK		(MSG_AUDIO_BASE | 0x1014)
#define 	MSG_AUDIO_CTL						(MSG_AUDIO_BASE | 0x0015)

#define		AUDIO_PROPERTY_SERVER_STATUS		(0x000 | PROPERTY_TYPE_GET)
#define		AUDIO_PROPERTY_FORMAT				(0x001 | PROPERTY_TYPE_GET)

#define     SPEAKER_CTL_INTERCOM_START      	0x0030
#define     SPEAKER_CTL_INTERCOM_STOP       	0x0040
#define     SPEAKER_CTL_INTERCOM_DATA        	0x0050

#define     SPEAKER_CTL_DEV_START_FINISH        0x0060
#define     SPEAKER_CTL_ZBAR_SCAN_SUCCEED       0x0070
#define     SPEAKER_CTL_WIFI_CONNECT            0x0080
#define     SPEAKER_CTL_INTERNET_CONNECT_DEFEAT	0x0081
#define     SPEAKER_CTL_ZBAR_SCAN	            0x0090
#define     SPEAKER_CTL_INSTALLING		        0x0091
#define     SPEAKER_CTL_INSTALLEND				0x0092
#define     SPEAKER_CTL_INSTALLFAILED		    0x0093
#define     SPEAKER_CTL_RESET				    0x0094
#define     SPEAKER_CTL_SD_EJECTED			    0x0095
#define     SPEAKER_CTL_SD_PLUG_SUCCESS		    0x0096
#define 	SPEAKER_PLAYBACK_CHN_NUM			0x0010

#define 	AUDIO_CTL_MOTOR						0x0020
/*
 * structure
 */

/*
 * function
 */
int server_audio_start(void);
int server_audio_message(message_t *msg);

#endif
