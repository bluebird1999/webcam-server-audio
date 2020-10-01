/*
 * audio.c
 *
 *  Created on: Aug 13, 2020
 *      Author: ning
 */

/*
 * header
 */
//system header
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#include <rtscamkit.h>
#include <rtsavapi.h>
#include <rtsaudio.h>
//program header
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/config/config_audio_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/config/config_interface.h"
//server header
#include "audio.h"

#include "../../manager/global_interface.h"
#include "../../manager/manager_interface.h"
#include "audio_interface.h"

/*
 * static
 */
//variable
static 	message_buffer_t	message;
static 	server_info_t 		info;
static	audio_stream_t		stream;
static	audio_config_t		config;

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static int server_none(void);
static int server_wait(void);
static int server_setup(void);
static int server_idle(void);
static int server_start(void);
static int server_run(void);
static int server_stop(void);
static int server_restart(void);
static int server_error(void);
static int server_release(void);
static int server_get_status(int type);
static int server_set_status(int type, int st);
static int server_check_msg_lock(void);
static void server_thread_termination(void);
//specific
static int stream_init(void);
static int stream_destroy(void);
static int stream_start(void);
static int stream_stop(void);
static int audio_init(void);
static int audio_main(void);
static int write_miss_avbuffer(struct rts_av_buffer *data);
static int audio_check_mode(int mode);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int audio_check_mode(int mode)
{
	int ret=-1,ret1=-1;
	ret1 = pthread_rwlock_wrlock(&info.lock);
	if(ret1) {
		log_err("add fifo lock fail, ret = %d", ret);
		return ret1;
	}
	ret = ((config.profile.run_mode) >> mode) & 0x01;
	ret1 = pthread_rwlock_unlock(&info.lock);
	if (ret1) {
		log_err("add fifo unlock fail, ret = %d", ret1);
	}
	return ret;
}

static int stream_init(void)
{
	stream.capture = -1;
	stream.encoder = -1;
	stream.frame = 0;
}

static int stream_destroy(void)
{
	int ret = 0;
	if (stream.capture >= 0) {
		RTS_SAFE_CLOSE(stream.capture, rts_av_destroy_chn);
		stream.capture = -1;
	}
	if (stream.encoder >= 0) {
		RTS_SAFE_CLOSE(stream.encoder, rts_av_destroy_chn);
		stream.encoder = -1;
	}
	return ret;
}

static int stream_start(void)
{
	int ret=0;

	if( stream.capture != -1 ) {
		ret = rts_av_enable_chn(stream.capture);
		if (ret) {
			log_err("enable capture fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	if( stream.encoder != -1 ) {
		ret = rts_av_enable_chn(stream.encoder);
		if (ret) {
			log_err("enable encoder fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	stream.frame = 0;
    ret = rts_av_start_recv(stream.encoder);
    if (ret) {
    	log_err("start recv audio fail, ret = %d", ret);
    	return -1;
    }
    return 0;
}

static int stream_stop(void)
{
	int ret=0;
	if(stream.encoder!=-1)
		ret = rts_av_stop_recv(stream.encoder);
	if(stream.capture!=-1)
		ret = rts_av_disable_chn(stream.capture);
	if(stream.encoder!=-1)
		ret = rts_av_disable_chn(stream.encoder);
	return ret;
}

static int audio_init(void)
{
	int ret;

	stream_init();
	stream.capture = rts_av_create_audio_capture_chn(&config.capture);
	if (stream.capture < 0) {
		log_err("fail to create audio capture chn, ret = %d", stream.capture);
		return -1;
	}
	log_info("capture chnno:%d", stream.capture);
	stream.encoder = rts_av_create_audio_encode_chn(RTS_AUDIO_TYPE_ID_ALAW, 0);
	if (stream.encoder < 0) {
		log_err("fail to create audio encoder chn, ret = %d", stream.encoder);
		return -1;
	}
	log_info("encoder chnno:%d", stream.encoder);
    ret = rts_av_bind(stream.capture, stream.encoder);
    if (ret) {
    	log_err("fail to bind capture and encode, ret = %d\n", ret);
    	return -1;
    }
	return 0;
}

static int audio_main(void)
{
	int ret = 0;
	struct rts_av_buffer *buffer = NULL;
	usleep(1000);

	if (rts_av_poll(stream.encoder))
		return 0;
	if (rts_av_recv(stream.encoder, &buffer))
		return 0;
	if (buffer) {
		if( audio_check_mode(RUN_MODE_SEND_MISS) ) {
			if( write_miss_avbuffer(buffer)!=0 )
				log_err("Miss ring buffer push failed!");
		}
		if( audio_check_mode(RUN_MODE_SAVE) ) {
//			if( write_avbuffer(&recorder_buffer, buffer)!=0 )
//				log_err("Recorder ring buffer push failed!");
		}
		stream.frame++;
		rts_av_put_buffer(buffer);
	}
    return ret;
}

static int write_miss_avbuffer(struct rts_av_buffer *data)
{
	int ret=0,ret1=-1;
	message_t msg;
	av_data_info_t	info;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_AUDIO_BUFFER_MISS;
	msg.extra = data->vm_addr;
	msg.extra_size = data->bytesused;
	info.flag = data->flags;
	info.frame_index = data->frame_idx;
	info.index = data->index;
	info.timestamp = data->timestamp;
	msg.arg = &info;
	msg.arg_size = sizeof(av_data_info_t);
	/****************************/
	server_miss_audio_message(&msg);
	return ret;
}

static void server_thread_termination(void)
{
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_AUDIO_SIGINT;
	msg.sender = msg.receiver = SERVER_AUDIO;
	/****************************/
	manager_message(&msg);
}

static int server_release(void)
{
	int ret = 0;
	stream_stop();
	stream_destroy();
	msg_buffer_release(&message);
	return ret;
}

static int server_set_msg_lock(int type, int st)
{
	int ret=-1, ret1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if( !info.msg_lock ) {
		if( type == MSG_TYPE_SET_CARE) {
			info.msg_lock = 1;
			info.msg_status = st;
			ret = 0;
		}
	}
	ret1 = pthread_rwlock_unlock(&info.lock);
	if (ret1)
		log_err("add unlock fail, ret = %d", ret1);
	return ret;
}

static int server_check_msg_lock(void)
{
	int ret=1, ret1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if( info.msg_lock ) {
		if( info.status == info.msg_status ) {
			info.msg_lock = 0;
			info.msg_status = 0;
			ret = 0;
		}
	}
	ret1 = pthread_rwlock_unlock(&info.lock);
	if (ret1)
		log_err("add unlock fail, ret = %d", ret1);
	return ret;
}

static int server_set_status(int type, int st)
{
	int ret=-1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		info.status = st;
	else if(type==STATUS_TYPE_EXIT)
		info.exit = st;
	else if(type==STATUS_TYPE_CONFIG)
		config.status = st;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return ret;
}

static int server_get_status(int type)
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		st = info.status;
	else if(type== STATUS_TYPE_EXIT)
		st = info.exit;
	else if(type==STATUS_TYPE_CONFIG)
		st = config.status;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return st;
}

static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg;
	message_t send_msg;
	message_arg_t *rd;

	msg_init(&msg);
	msg_init(&send_msg);
	int st;
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	if( server_check_msg_lock() )
		return 0;
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_err("add message unlock fail, ret = %d\n", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1) {
		return 0;
	}
	server_set_msg_lock(msg.type, msg.target_status);
	switch(msg.message){
	case MSG_MISS_SERVER_AUDIO_START:
		st = server_get_status(STATUS_TYPE_STATUS);
		log_info("audio server status = %d", st);
		if( st == STATUS_IDLE ) {
			server_set_status(STATUS_TYPE_STATUS, STATUS_START);
			ret = 0;
		}
		else if( st == STATUS_RUN )
			ret = 0;
		else
			ret = -1;
		break;
	case MSG_MISS_SERVER_AUDIO_STOP:
		if( server_get_status(STATUS_TYPE_STATUS) == STATUS_RUN) {
			server_set_status(STATUS_TYPE_STATUS, STATUS_STOP );
			ret = 0;
		}
		else
			ret = -1;
		break;
	case MSG_MANAGER_EXIT:
		server_set_status(STATUS_TYPE_EXIT,1);
		break;
	case MSG_CONFIG_READ_ACK:
		if( msg.result==0 ) {
			memcpy( (audio_config_t*)(&config), (audio_config_t*)msg.arg, msg.arg_size);
			if( server_get_status(STATUS_TYPE_CONFIG) == ( (1<<CONFIG_AUDIO_MODULE_NUM) -1 ) )
				server_set_status(STATUS_TYPE_STATUS, STATUS_SETUP);
		}
		break;
	case MSG_MANAGER_TIMER_ACK:
		((HANDLER)msg.arg_in.handler)();
		break;
	}
	msg_free(&msg);
	return ret;
}


/*
 * State Machine
 */
static int server_none(void)
{
	int ret = 0;
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_CONFIG_READ;
	msg.sender = msg.receiver = SERVER_AUDIO;
	/***************************/
	ret = server_config_message(&msg);
	if( ret == 0 )
		server_set_status(STATUS_TYPE_STATUS, STATUS_WAIT);
	else
		sleep(1);
	return ret;
}

static int server_wait(void)
{
	int ret = 0;
	usleep(50000);
	return ret;
}

static int server_setup(void)
{
	int ret = 0;
	if( audio_init() == 0)
		server_set_status(STATUS_TYPE_STATUS, STATUS_IDLE);
	else
		server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR);
	return ret;
}

static int server_idle(void)
{
	int ret = 0;
	usleep(50000);
	return ret;
}

static int server_start(void)
{
	int ret = 0;
	if( stream_start()==0 )
		server_set_status(STATUS_TYPE_STATUS, STATUS_RUN);
	else
		server_set_status(STATUS_TYPE_STATUS, STATUS_ERROR);
	return ret;
}

static int server_run(void)
{
	int ret = 0;
	if(audio_main()!=0)
		server_set_status(STATUS_TYPE_STATUS, STATUS_STOP);
	return ret;
}

static int server_stop(void)
{
	int ret = 0;
	if( stream_stop()==0 )
		server_set_status(STATUS_TYPE_STATUS,STATUS_IDLE);
	else
		server_set_status(STATUS_TYPE_STATUS,STATUS_ERROR);
	return ret;
}

static int server_restart(void)
{
	int ret = 0;
	server_release();
	server_set_status(STATUS_TYPE_STATUS,STATUS_WAIT);
	return ret;
}

static int server_error(void)
{
	int ret = 0;
	server_release();
	log_err("!!!!!!!!error in audio!!!!!!!!");
	return ret;
}

static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_audio");
	pthread_detach(pthread_self());
	while( !server_get_status(STATUS_TYPE_EXIT) ) {
	switch( server_get_status(STATUS_TYPE_STATUS) ){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			server_wait();
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			server_idle();
			break;
		case STATUS_START:
			server_start();
			break;
		case STATUS_RUN:
			server_run();
			break;
		case STATUS_STOP:
			server_stop();
			break;
		case STATUS_RESTART:
			server_restart();
			break;
		case STATUS_ERROR:
			server_error();
			break;
		}
		server_message_proc();
	}
	server_release();
	message_t msg;
	/********message body********/
	msg_init(&msg);
	msg.message = MSG_MANAGER_EXIT_ACK;
	msg.sender = SERVER_AUDIO;
	/***************************/
	manager_message(&msg);
	log_info("-----------thread exit: server_audio-----------");
	pthread_exit(0);
}


/*
 * external interface
 */
int server_audio_start(void)
{
	int ret=-1;
	msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	pthread_rwlock_init(&info.lock, NULL);
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_err("audio server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_err("audio server create successful!");
		return 0;
	}
}

int server_audio_message(message_t *msg)
{
	int ret=0,ret1;
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	log_info("push into the audio message queue: sender=%d, message=%d, ret=%d", msg->sender, msg->message, ret);
	if( ret!=0 )
		log_err("message push in audio error =%d", ret);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_err("add message unlock fail, ret = %d\n", ret1);
	return ret;
}
