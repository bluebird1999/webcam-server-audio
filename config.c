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
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
//server header
#include "config.h"

/*
 * static
 */
//variable
static int							dirty;
static audio_config_t				audio_config;

static config_map_t audio_config_profile_map[] = {
		{"enable", 				&(audio_config.profile.enable), 		cfg_u32, 1,0,0,1,},
		{"aec_enable",			&(audio_config.profile.aec_enable),		cfg_u32, 1,0,0,1,},
		{"ns_enable",			&(audio_config.profile.ns_enable),		cfg_u32, 1,0,0,1,},
		{"ns_level",			&(audio_config.profile.ns_level),		cfg_u32, 1,0,0,100,},
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
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	if( misc_get_bit(dirty, CONFIG_AUDIO_PROFILE) ) {
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_AUDIO_PROFILE_PATH);
		ret = write_config_file(&audio_config_profile_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_AUDIO_PROFILE, 0);
	}
	else if( misc_get_bit(dirty, CONFIG_AUDIO_CAPTURE) )
	{
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_AUDIO_CAPTURE_PATH);
		ret = write_config_file(&audio_config_caputure_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_AUDIO_CAPTURE, 0);
	}
	if( !dirty ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_REMOVE;
		msg.arg_in.handler = audio_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	return ret;
}

int config_audio_read(audio_config_t *aconfig)
{
	int ret,ret1=0;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_AUDIO_PROFILE_PATH);
	ret = read_config_file(&audio_config_profile_map, fname);
	if(!ret)
		misc_set_bit(&audio_config.status, CONFIG_AUDIO_PROFILE,1);
	else
		misc_set_bit(&audio_config.status, CONFIG_AUDIO_PROFILE,0);
	ret1 |= ret;
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_AUDIO_CAPTURE_PATH);
	ret = read_config_file(&audio_config_caputure_map, fname);
	if(!ret)
		misc_set_bit(&audio_config.status, CONFIG_AUDIO_CAPTURE,1);
	else
		misc_set_bit(&audio_config.status, CONFIG_AUDIO_CAPTURE,0);
	ret1 |= ret;
	memcpy(aconfig,&audio_config,sizeof(audio_config_t));
	return ret1;
}

int config_audio_set(int module, void *arg)
{
	int ret = 0;
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
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	misc_set_bit(&dirty, module, 1);
	if( module == CONFIG_AUDIO_PROFILE) {
		memcpy( (audio_profile_t*)(&audio_config.profile), arg, sizeof(audio_profile_t));
	}
	else if ( module == CONFIG_AUDIO_CAPTURE ) {
		memcpy( (struct rts_audio_attr*)(&audio_config.capture), arg, sizeof(struct rts_audio_attr));
	}
	return ret;
}
