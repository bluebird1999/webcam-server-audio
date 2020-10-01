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
#define		MSG_AUDIO_BASE						(SERVER_AUDIO<<16)
#define		MSG_AUDIO_SIGINT					MSG_AUDIO_BASE | 0x0000
#define		MSG_AUDIO_SIGTERM					MSG_AUDIO_BASE | 0x0001
#define		MSG_AUDIO_EXIT						MSG_AUDIO_BASE | 0X0002
#define		MSG_AUDIO_BUFFER_MISS				MSG_AUDIO_BASE | 0X0010

/*
 * structure
 */

/*
 * function
 */
int server_audio_start(void);
int server_audio_message(message_t *msg);

#endif
