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
#define		SERVER_AUDIO_VERSION_STRING			"alpha-3.1"

#define		MSG_AUDIO_BASE						(SERVER_AUDIO<<16)
#define		MSG_AUDIO_SIGINT					MSG_AUDIO_BASE | 0x0000
#define		MSG_AUDIO_SIGINT_ACK				MSG_AUDIO_BASE | 0x1000
//AUDIO control message
#define		MSG_AUDIO_START						MSG_AUDIO_BASE | 0x0010
#define		MSG_AUDIO_START_ACK					MSG_AUDIO_BASE | 0x1010
#define		MSG_AUDIO_STOP						MSG_AUDIO_BASE | 0x0011
#define		MSG_AUDIO_STOP_ACK					MSG_AUDIO_BASE | 0x1011
#define		MSG_AUDIO_CTRL						MSG_AUDIO_BASE | 0x0012
#define		MSG_AUDIO_CTRL_ACK					MSG_AUDIO_BASE | 0x1012
#define		MSG_AUDIO_CTRL_EXT					MSG_AUDIO_BASE | 0x0013
#define		MSG_AUDIO_CTRL_EXT_ACK				MSG_AUDIO_BASE | 0x1013
#define		MSG_AUDIO_CTRL_DIRECT				MSG_AUDIO_BASE | 0x0014
#define		MSG_AUDIO_CTRL_DIRECT_ACK			MSG_AUDIO_BASE | 0x1014
#define		MSG_AUDIO_GET_PARA					MSG_AUDIO_BASE | 0x0015
#define		MSG_AUDIO_GET_PARA_ACK				MSG_AUDIO_BASE | 0x1015

/*
 * structure
 */

/*
 * function
 */
int server_audio_start(void);
int server_audio_message(message_t *msg);

#endif
