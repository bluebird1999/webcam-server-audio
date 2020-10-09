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
#include "../../server/miio/miio_interface.h"
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
static int server_release(void);
static int server_get_status(int type);
static int server_set_status(int type, int st);
static void server_thread_termination(void);
static void task_default(void);
static void task_start(void);
static void task_stop(void);
static void task_error(void);
//specific
static int stream_init(void);
static int stream_destroy(void);
static int stream_start(void);
static int stream_stop(void);
static int audio_init(void);
static int audio_main(void);
static int write_audio_buffer(struct rts_av_buffer *data, int id, int target);
static int send_message(int receiver, message_t *msg);
static int send_iot_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg, int size);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */

static int send_iot_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg, int size)
{
	int ret = 0;
    /********message body********/
	msg_init(msg);
	memcpy(&(msg->arg_pass), &(org_msg->arg_pass),sizeof(message_arg_t));
	msg->message = id | 0x1000;
	msg->sender = msg->receiver = SERVER_AUDIO;
	msg->result = result;
	msg->arg = arg;
	msg->arg_size = size;
	ret = send_message(receiver, msg);
	/***************************/
	return ret;
}

static int send_message(int receiver, message_t *msg)
{
	int st;
	switch(receiver) {
	case SERVER_CONFIG:
		st = server_config_message(msg);
		break;
	case SERVER_DEVICE:
		break;
	case SERVER_KERNEL:
		break;
	case SERVER_REALTEK:
		break;
	case SERVER_MIIO:
		st = server_miio_message(msg);
		break;
	case SERVER_MISS:
		st = server_miss_message(msg);
		break;
	case SERVER_MICLOUD:
		break;
	case SERVER_AUDIO:
		st = server_audio_message(msg);
		break;
	case SERVER_RECORDER:
		break;
	case SERVER_PLAYER:
		break;
	case SERVER_MANAGER:
		st = manager_message(msg);
		break;
	}
	return st;
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
		if( misc_get_bit(config.profile.run_mode, RUN_MODE_SEND_MISS)
				&& misc_get_bit(info.status2, RUN_MODE_SEND_MISS) ) {
			if( write_audio_buffer(buffer, MSG_MISS_AUDIO_DATA, SERVER_AUDIO) != 0 )
				log_err("Miss ring buffer push failed!");
		}
		if( misc_get_bit(config.profile.run_mode, RUN_MODE_SAVE)
				&& misc_get_bit(info.status2, RUN_MODE_SAVE) ) {
			if( write_audio_buffer(&buffer, MSG_RECORDER_AUDIO_DATA, SERVER_RECORDER) != 0 )
				log_err("Recorder ring buffer push failed!");
		}
		if( misc_get_bit(config.profile.run_mode, RUN_MODE_SEND_MICLOUD)
				&& misc_get_bit(info.status2, RUN_MODE_SEND_MICLOUD) ) {
			if( write_audio_buffer(&buffer, MSG_MICLOUD_AUDIO_DATA, SERVER_MICLOUD) != 0 )
				log_err("Micloud ring buffer push failed!");
		}
		stream.frame++;
		rts_av_put_buffer(buffer);
	}
    return ret;
}

static int write_audio_buffer(struct rts_av_buffer *data, int id, int target)
{
	int ret=0;
	message_t msg;
	av_data_info_t	info;
    /********message body********/
	msg_init(&msg);
	msg.message = id;
	msg.extra = data->vm_addr;
	msg.extra_size = data->bytesused;
	info.flag = data->flags;
	info.frame_index = data->frame_idx;
	info.index = data->index;
	info.timestamp = data->timestamp;
	msg.arg = &info;
	msg.arg_size = sizeof(av_data_info_t);
	if( target == SERVER_AUDIO )
		ret = server_miss_audio_message(&msg);
/*	else if( target == MSG_MICLOUD_AUDIO_DATA )
		ret = server_micloud_audio_message(&msg);
	else if( target == MSG_RECORDER_AUDIO_DATA )
		ret = server_recorder_audio_message(&msg);
*/
	/****************************/
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


static int server_set_status(int type, int st)
{
	int ret=-1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS) info.status = st;
	else if(type==STATUS_TYPE_EXIT) info.exit = st;
	else if(type==STATUS_TYPE_CONFIG) config.status = st;
	else if(type==STATUS_TYPE_THREAD_START) info.thread_start = st;
	else if(type==STATUS_TYPE_THREAD_EXIT) info.thread_exit = st;
	else if(type==STATUS_TYPE_MESSAGE_LOCK) info.msg_lock = st;
	else if(type==STATUS_TYPE_STATUS2) info.status2 = st;
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
	if(type == STATUS_TYPE_STATUS) st = info.status;
	else if(type== STATUS_TYPE_EXIT) st = info.exit;
	else if(type==STATUS_TYPE_CONFIG) st = config.status;
	else if(type==STATUS_TYPE_THREAD_START) st = info.thread_start;
	else if(type==STATUS_TYPE_THREAD_EXIT) st = info.thread_exit;
	else if(type==STATUS_TYPE_MESSAGE_LOCK) st = info.msg_lock;
	else if(type==STATUS_TYPE_STATUS2) st = info.status2;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return st;
}


/*
 * State Machine
 */
static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg,send_msg;
	msg_init(&msg);
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	if( info.msg_lock ) {
		ret1 = pthread_rwlock_unlock(&message.lock);
		return 0;
	}
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_err("add message unlock fail, ret = %d\n", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1)
		return 0;
	switch(msg.message) {
		case MSG_AUDIO_START:
			if( msg.sender == SERVER_MISS) misc_set_bit(&info.status2, RUN_MODE_SEND_MISS, 1);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_SEND_MICLOUD, 1);
			if( msg.sender == SERVER_RECORDER) misc_set_bit(&info.status2, RUN_MODE_SAVE, 1);
			if( info.status == STATUS_RUN ) {
				ret = send_iot_ack(&msg, &send_msg, MSG_AUDIO_START, msg.receiver, 0, 0, 0);
				break;
			}
			info.task.func = task_start;
			info.task.start = info.status;
			memcpy(&info.task.msg, &msg,sizeof(message_t));
			info.msg_lock = 1;
			break;
		case MSG_AUDIO_STOP:
			if( msg.sender == SERVER_MISS) misc_set_bit(&info.status2, RUN_MODE_SEND_MISS, 0);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_SEND_MICLOUD, 0);
			if( msg.sender == SERVER_RECORDER) misc_set_bit(&info.status2, RUN_MODE_SAVE, 0);
			if( info.status != STATUS_RUN ) {
				ret = send_iot_ack(&msg, &send_msg, MSG_AUDIO_START, msg.receiver, 0, 0, 0);
				break;
			}
			if( info.status2 > 0 ) {
				ret = send_iot_ack(&msg, &send_msg, MSG_AUDIO_START, msg.receiver, 0, 0, 0);
				break;
			}
			info.task.func = task_stop;
			info.task.start = info.status;
			msg_deep_copy(&info.task.msg, &msg);
			info.msg_lock = 1;
			break;
		case MSG_MANAGER_EXIT:
			info.exit = 1;
			break;
		case MSG_CONFIG_READ_ACK:
			if( msg.result==0 )
				memcpy( (audio_config_t*)(&config), (audio_config_t*)msg.arg, msg.arg_size);
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
	}
	msg_free(&msg);
	return ret;
}

/*
 * task
 */
/*
 * task error: error->5 seconds->shut down server->msg manager
 */
static void task_error(void)
{
	unsigned int tick=0;
	switch( info.status ) {
		case STATUS_ERROR:
			log_err("!!!!!!!!error in audio, restart in 5 s!");
			info.tick = time_get_now_ms();
			info.status = STATUS_NONE;
			break;
		case STATUS_NONE:
			tick = time_get_now_ms();
			if( (tick - info.tick) > 5000 ) {
				info.exit = 1;
				info.tick = tick;
			}
			break;
	}
	usleep(1000);
	return;
}

/*
 * task start: idle->start
 */
static void task_start(void)
{
	message_t msg;
	int ret;
	switch(info.status){
		case STATUS_RUN:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_AUDIO_START, info.task.msg.receiver, 0, 0, 0);
			goto exit;
			break;
		case STATUS_IDLE:
			info.status = STATUS_START;
			break;
		case STATUS_START:
			if( stream_start()==0 ) info.status = STATUS_RUN;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_AUDIO_START, info.task.msg.receiver, -1 ,0 ,0);
			goto exit;
			break;
	}
	usleep(1000);
	return;
exit:
	info.task.func = &task_default;
	info.msg_lock = 0;
	msg_free(&info.task.msg);
	return;
}
/*
 * task start: run->stop->idle
 */
static void task_stop(void)
{
	message_t msg;
	int ret;
	switch(info.status){
		case STATUS_IDLE:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_AUDIO_STOP, info.task.msg.receiver, 0, 0, 0);
			goto exit;
			break;
		case STATUS_RUN:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_AUDIO_STOP, info.task.msg.receiver, -1,0 ,0);
			break;
	}
	usleep(1000);
	return;
exit:
	info.task.func = &task_default;
	info.msg_lock = 0;
	msg_free(&info.task.msg);
	return;
}
/*
 * default task: none->run
 */
static void task_default(void)
{
	message_t msg;
	int ret = 0;
	switch( info.status){
		case STATUS_NONE:
		    /********message body********/
			msg_init(&msg);
			msg.message = MSG_CONFIG_READ;
			msg.sender = msg.receiver = SERVER_AUDIO;
			ret = server_config_message(&msg);
			/***************************/
			if( !ret ) info.status = STATUS_WAIT;
			else sleep(1);
			break;
		case STATUS_WAIT:
			if( config.status == ( (1<<CONFIG_AUDIO_MODULE_NUM) -1 ) )
				info.status = STATUS_SETUP;
			else usleep(1000);
			break;
		case STATUS_SETUP:
			if( audio_init() == 0) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_RUN:
			if(audio_main()!=0) info.status = STATUS_STOP;
			break;
		case STATUS_STOP:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			info.task.func = task_error;
			break;
		}
	usleep(1000);
	return;
}

static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_audio");
	pthread_detach(pthread_self());
	//default task
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while( !info.exit ) {
		info.task.func();
		server_message_proc();
	}
	if( info.exit ) {
		while( info.thread_exit != info.thread_start ) {
		}
	    /********message body********/
		message_t msg;
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_AUDIO;
		manager_message(&msg);
		/***************************/
	}
	server_release();
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
