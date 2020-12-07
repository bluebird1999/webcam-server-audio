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

#define		AUDIO_INIT_CONDITION_NUM				2
#define		AUDIO_INIT_CONDITION_CONFIG				0
#define		AUDIO_INIT_CONDITION_REALTEK			1

#define		AUDIO_EXIT_CONDITION			( (1 << SERVER_MISS) | (1 << SERVER_RECORDER) )
/*
 * structure
 */

/*
 * function
 */

#endif /* SERVER_AUDIO_AUDIO_H_ */
