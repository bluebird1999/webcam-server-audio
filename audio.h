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
#define	RUN_MODE_SAVE			0
#define RUN_MODE_SEND_MISS		1
#define	RUN_MODE_SEND_MICLOUD	2

#define		AUDIO_INIT_CONDITION_NUM				1
#define		AUDIO_INIT_CONDITION_CONFIG				0

/*
 * structure
 */
typedef struct audio_stream_t {
	//channel
	int capture;
	int encoder;
	//data
	int	frame;
} audio_stream_t;

/*
 * function
 */

#endif /* SERVER_AUDIO_AUDIO_H_ */
