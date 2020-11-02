/*
 * config_audio.c
 *
 *  Created on: Sep 1, 2020
 *      Author: ning
 */


/*
 * header
 */
//system header
#include <pthread.h>
#include <stdio.h>
#include <rtsvideo.h>
#include <malloc.h>
#include <dmalloc.h>
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
//server header
#include "config.h"

/*
 * static
 */
//variable
static pthread_rwlock_t				lock;
static int							dirty;
static audio_config_t				audio_config;

static config_map_t audio_config_profile_map[] = {
    {"enable", 			&(audio_config.profile.enable), 		cfg_u32, 0,0,0,64,},
    {NULL,},
};

static config_map_t audio_config_caputure_map[] = {
    {"device", 				&(audio_config.capture.dev_node), 		cfg_string, 'hw:0,1',0,0,64,},
    {"format",				&(audio_config.capture.format),			cfg_u32, 16,0,0,100,},
	{"rate",				&(audio_config.capture.rate),			cfg_u32, 8000,0,0,10000000,},
	{"channels",			&(audio_config.capture.channels),		cfg_u32, 1,0,0,10,},
    {NULL,},
};
//function
static int audio_config_save(void);


/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */


/*
 * interface
 */
static int audio_config_save(void)
{
	int ret = 0;
	message_t msg;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	if( misc_get_bit(dirty, CONFIG_AUDIO_PROFILE) ) {
		ret = write_config_file(&audio_config_profile_map, CONFIG_AUDIO_PROFILE_PATH);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_AUDIO_PROFILE, 0);
	}
	else if( misc_get_bit(dirty, CONFIG_AUDIO_CAPTURE) )
	{
		ret = write_config_file(&audio_config_caputure_map, CONFIG_AUDIO_CAPTURE_PATH);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_AUDIO_CAPTURE, 0);
	}
	if( !dirty ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_REMOVE;
		msg.arg_in.handler = audio_config_save;
		/****************************/
		manager_message(&msg);
	}
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);

	return ret;
}

int config_audio_read(audio_config_t *aconfig)
{
	int ret,ret1=0;
	pthread_rwlock_init(&lock, NULL);
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = read_config_file(&audio_config_profile_map, CONFIG_AUDIO_PROFILE_PATH);
	if(!ret)
		misc_set_bit(&audio_config.status, CONFIG_AUDIO_PROFILE,1);
	else
		misc_set_bit(&audio_config.status, CONFIG_AUDIO_PROFILE,0);
	ret1 |= ret;
	ret = read_config_file(&audio_config_caputure_map, CONFIG_AUDIO_CAPTURE_PATH);
	if(!ret)
		misc_set_bit(&audio_config.status, CONFIG_AUDIO_CAPTURE,1);
	else
		misc_set_bit(&audio_config.status, CONFIG_AUDIO_CAPTURE,0);
	ret1 |= ret;
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);
	ret1 |= ret;
	memcpy(aconfig,&audio_config,sizeof(audio_config_t));
	return ret1;
}

int config_audio_set(int module, void *arg)
{
	int ret = 0;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	if(dirty==0) {
		message_t msg;
		message_arg_t arg;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_ADD;
		msg.sender = SERVER_AUDIO;
		msg.arg_in.cat = FILE_FLUSH_TIME;	//1min
		msg.arg_in.dog = 0;
		msg.arg_in.duck = 0;
		msg.arg_in.handler = &audio_config_save;
		/****************************/
		manager_message(&msg);
	}
	misc_set_bit(&dirty, module, 1);
	if( module == CONFIG_AUDIO_PROFILE) {
		memcpy( (audio_profile_t*)(&audio_config.profile), arg, sizeof(audio_profile_t));
	}
	else if ( module == CONFIG_AUDIO_CAPTURE ) {
		memcpy( (struct rts_audio_attr*)(&audio_config.capture), arg, sizeof(struct rts_audio_attr));
	}
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);
	return ret;
}

int config_audio_get_config_status(int module)
{
	int st,ret=0;
	ret = pthread_rwlock_wrlock(&lock);
	if(ret)	{
		log_err("add lock fail, ret = %d\n", ret);
		return ret;
	}
	if(module==-1)
		st = audio_config.status;
	else
		st = misc_get_bit(audio_config.status, module);
	ret = pthread_rwlock_unlock(&lock);
	if (ret)
		log_err("add unlock fail, ret = %d\n", ret);
	return st;
}

