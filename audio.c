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
#include <malloc.h>
#include <miss.h>
//program header
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../manager/global_interface.h"
#include "../../manager/manager_interface.h"
#include "../../server/recorder/recorder_interface.h"
//server header
#include "audio.h"
#include "config.h"
#include "audio_interface.h"

/*
 * static
 */
//variable
static 	message_buffer_t	message;
static 	server_info_t 		info;
static	audio_stream_t		stream;
static	audio_config_t		config;
static 	unsigned long long int		tick = 0;

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static int server_release(void);
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
static int write_audio_buffer(struct rts_av_buffer *data, int id, int target, int type);
static int send_message(int receiver, message_t *msg);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int send_message(int receiver, message_t *msg)
{
	int st = 0;
	switch(receiver) {
		case SERVER_DEVICE:
			st = server_device_message(msg);
			break;
		case SERVER_KERNEL:
	//		st = server_kernel_message(msg);
			break;
		case SERVER_REALTEK:
			st = server_realtek_message(msg);
			break;
		case SERVER_MIIO:
			st = server_miio_message(msg);
			break;
		case SERVER_MISS:
			st = server_miss_message(msg);
			break;
		case SERVER_MICLOUD:
	//		st = server_micloud_message(msg);
			break;
		case SERVER_VIDEO:
			st = server_video_message(msg);
			break;
		case SERVER_AUDIO:
			st = server_audio_message(msg);
			break;
		case SERVER_RECORDER:
			st = server_recorder_message(msg);
			break;
		case SERVER_PLAYER:
			st = server_player_message(msg);
			break;
		case SERVER_SPEAKER:
			st = server_speaker_message(msg);
			break;
		case SERVER_VIDEO2:
			st = server_video2_message(msg);
			break;
		case SERVER_SCANNER:
//			st = server_scanner_message(msg);
			break;
		case SERVER_MANAGER:
			st = manager_message(msg);
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "unknown message target! %d", receiver);
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
			log_qcy(DEBUG_SERIOUS, "enable capture fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	if( stream.encoder != -1 ) {
		ret = rts_av_enable_chn(stream.encoder);
		if (ret) {
			log_qcy(DEBUG_SERIOUS, "enable encoder fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	stream.frame = 0;
    ret = rts_av_start_recv(stream.encoder);
    if (ret) {
    	log_qcy(DEBUG_SERIOUS, "start recv audio fail, ret = %d", ret);
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
		log_qcy(DEBUG_SERIOUS, "fail to create audio capture chn, ret = %d", stream.capture);
		return -1;
	}
	log_qcy(DEBUG_INFO, "capture chnno:%d", stream.capture);
	stream.encoder = rts_av_create_audio_encode_chn(RTS_AUDIO_TYPE_ID_ALAW, 0);
	if (stream.encoder < 0) {
		log_qcy(DEBUG_SERIOUS, "fail to create audio encoder chn, ret = %d", stream.encoder);
		return -1;
	}
	log_qcy(DEBUG_INFO, "encoder chnno:%d", stream.encoder);
    ret = rts_av_bind(stream.capture, stream.encoder);
    if (ret) {
    	log_qcy(DEBUG_SERIOUS, "fail to bind capture and encode, ret = %d", ret);
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
		if( buffer->bytesused <= 1024*100 ) {
			if( misc_get_bit(info.status2, RUN_MODE_SEND_MISS) ) {
				if( write_audio_buffer(buffer, MSG_MISS_AUDIO_DATA, SERVER_MISS, 0) != 0 )
					log_qcy(DEBUG_WARNING, "Miss ring buffer push failed!");
			}
			if( misc_get_bit(info.status2, RUN_MODE_SAVE) ) {
				if( write_audio_buffer(buffer, MSG_RECORDER_AUDIO_DATA, SERVER_RECORDER, RECORDER_TYPE_NORMAL) != 0 )
					log_qcy(DEBUG_WARNING, "Recorder ring buffer push failed!");
			}
			if( misc_get_bit(info.status2, RUN_MODE_MOTION_DETECT) ) {
				if( write_audio_buffer(buffer, MSG_RECORDER_AUDIO_DATA, SERVER_RECORDER, RECORDER_TYPE_MOTION_DETECTION) != 0 )
					log_qcy(DEBUG_WARNING, "Recorder ring buffer push failed!");
			}
/* wait for other server
			if( misc_get_bit(info.status2, RUN_MODE_SEND_MICLOUD) ) {
				if( write_audio_buffer(buffer, MSG_MICLOUD_AUDIO_DATA, SERVER_MICLOUD) != 0 )
					log_qcy(DEBUG_SERIOUS, "Micloud ring buffer push failed!");
			}
*/
			stream.frame++;
		}
		rts_av_put_buffer(buffer);
	}
    return ret;
}

static int write_audio_buffer(struct rts_av_buffer *data, int id, int target, int type)
{
	int ret=0;
	message_t msg;
	av_data_info_t	info;
    /********message body********/
	msg_init(&msg);
	msg.arg_in.cat = type;
	msg.sender = msg.receiver = SERVER_AUDIO;
	msg.message = id;
	msg.extra = data->vm_addr;
	msg.extra_size = data->bytesused;
	info.flag = data->flags;
	info.frame_index = data->frame_idx;
	info.timestamp = data->timestamp / 1000;	// ms = us/1000
	info.flag = FLAG_AUDIO_SAMPLE_8K << 3 | FLAG_AUDIO_DATABITS_8 << 7 | FLAG_AUDIO_CHANNEL_MONO << 9 |  FLAG_RESOLUTION_AUDIO_DEFAULT << 17;
	msg.arg = &info;
	msg.arg_size = sizeof(av_data_info_t);
	if( target == SERVER_MISS )
		ret = server_miss_audio_message(&msg);
//	else if( target == SERVER_MICLOUD )
//		ret = server_micloud_audio_message(&msg);
	else if( target == SERVER_RECORDER )
		ret = server_recorder_audio_message(&msg);
	return ret;
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
	msg_free(&info.task.msg);
	memset(&info, 0, sizeof(server_info_t));
	memset(&config, 0, sizeof(audio_config_t));
	memset(&stream, 0, sizeof(audio_stream_t));
	return ret;
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
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d", ret);
		return ret;
	}
	if( info.msg_lock ) {
		ret1 = pthread_rwlock_unlock(&message.lock);
		return 0;
	}
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1)
		return 0;
	/**************************/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg.arg_pass),sizeof(message_arg_t));
	send_msg.message = msg.message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_AUDIO;
	send_msg.result = 0;
	/***************************/
	switch(msg.message) {
		case MSG_AUDIO_START:
			if( msg.sender == SERVER_MISS) misc_set_bit(&info.status2, RUN_MODE_SEND_MISS, 1);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_SEND_MICLOUD, 1);
			if( msg.sender == SERVER_RECORDER)  {
				if( msg.arg_in.cat == RECORDER_TYPE_NORMAL)
					misc_set_bit(&info.status2, RUN_MODE_SAVE, 1);
				else if( msg.arg_in.cat == RECORDER_TYPE_MOTION_DETECTION )
					misc_set_bit(&info.status2, RUN_MODE_MOTION_DETECT, 1);
			}
			if( info.status == STATUS_RUN ) {
				ret = send_message(msg.receiver, &send_msg);
				break;
			}
			else if( info.status <= STATUS_NONE) {
				send_msg.result = -1;
				ret = send_message(msg.receiver, &send_msg);
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
			if( msg.sender == SERVER_RECORDER) {
				if( msg.arg_in.cat == RECORDER_TYPE_NORMAL)
					misc_set_bit(&info.status2, RUN_MODE_SAVE, 0);
				else if( msg.arg_in.cat == RECORDER_TYPE_MOTION_DETECTION )
					misc_set_bit(&info.status2, RUN_MODE_MOTION_DETECT, 0);
			}
			if( info.status != STATUS_RUN ) {
				ret = send_message(msg.receiver, &send_msg);
				break;
			}
			if( info.status2 > 0 ) {
				ret = send_message(msg.receiver, &send_msg);
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
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_REALTEK_PROPERTY_NOTIFY:
		case MSG_REALTEK_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == REALTEK_PROPERTY_AV_STATUS ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit(&info.thread_exit, AUDIO_INIT_CONDITION_REALTEK, 1);
			}
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %x", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

static int heart_beat_proc(void)
{
	int ret = 0;
	message_t msg;
	long long int tick = 0;
	tick = time_get_now_stamp();
	if( (tick - info.tick) > SERVER_HEARTBEAT_INTERVAL ) {
		info.tick = tick;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_HEARTBEAT;
		msg.sender = msg.receiver = SERVER_AUDIO;
		msg.arg_in.cat = info.status;
		msg.arg_in.dog = info.thread_start;
		msg.arg_in.duck = info.thread_exit;
		ret = manager_message(&msg);
		/***************************/
	}
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
			log_qcy(DEBUG_SERIOUS, "!!!!!!!!error in audio, restart in 5 s!");
			info.tick3 = time_get_now_stamp();
			info.status = STATUS_NONE;
			break;
		case STATUS_NONE:
			tick = time_get_now_stamp();
			if( (tick - info.tick3) > SERVER_RESTART_PAUSE ) {
				info.exit = 1;
				info.tick3 = tick;
			}
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_error = %d", info.status);
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
	int ret = 0;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_AUDIO;
	msg.result = 0;
	/***************************/
	switch( info.status ){
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			if( audio_init() == 0) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_IDLE:
			info.status = STATUS_START;
			break;
		case STATUS_START:
			if( stream_start()==0 ) info.status = STATUS_RUN;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_RUN:
			send_message(info.task.msg.receiver, &msg);
			goto exit;
			break;
		case STATUS_ERROR:
			msg.result = -1;
			send_message(info.task.msg.receiver, &msg);
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_start = %d", info.status);
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
	int ret = 0;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_AUDIO;
	msg.result = 0;
	/***************************/
	switch(info.status){
		case STATUS_IDLE:
			send_message(info.task.msg.receiver, &msg);
			goto exit;
			break;
		case STATUS_RUN:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			msg.result = -1;
			send_message(info.task.msg.receiver, &msg);
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_stop = %d", info.status);
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
	int ret = 0;
	message_t msg;
	switch( info.status ){
		case STATUS_NONE:
			if( !misc_get_bit( info.thread_exit, AUDIO_INIT_CONDITION_CONFIG ) ) {
				ret = config_audio_read(&config);
				if( !ret && misc_full_bit( config.status, CONFIG_AUDIO_MODULE_NUM) ) {
					misc_set_bit(&info.thread_exit, AUDIO_INIT_CONDITION_CONFIG, 1);
				}
				else {
					info.status = STATUS_ERROR;
					break;
				}
			}
			if( !misc_get_bit( info.thread_exit, AUDIO_INIT_CONDITION_REALTEK ) &&
					((time_get_now_stamp() - info.tick2 ) > MESSAGE_RESENT) ) {
				info.tick2 = time_get_now_stamp();
			    /********message body********/
				msg_init(&msg);
				msg.message = MSG_REALTEK_PROPERTY_GET;
				msg.sender = msg.receiver = SERVER_AUDIO;
				msg.arg_in.cat = REALTEK_PROPERTY_AV_STATUS;
				server_realtek_message(&msg);
				/****************************/
			}
			if( misc_full_bit( info.thread_exit, AUDIO_INIT_CONDITION_NUM ) )
				info.status = STATUS_WAIT;
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			if( audio_init() == 0) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_IDLE:
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
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
		}
	tick++;
	usleep(1000);
	return;
}

static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	pthread_detach(pthread_self());
	if( !message.init ) {
		msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	}
	//default task
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while( !info.exit ) {
		info.task.func();
		server_message_proc();
		heart_beat_proc();
	}
	if( info.exit ) {
		while( info.thread_start ) {
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
	log_qcy(DEBUG_SERIOUS, "-----------thread exit: server_audio-----------");
	pthread_exit(0);
}


/*
 * external interface
 */
int server_audio_start(void)
{
	int ret=-1;
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "audio server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_qcy(DEBUG_INFO, "audio server create successful!");
		return 0;
	}
}

int server_audio_message(message_t *msg)
{
	int ret=0,ret1;
	if( !message.init ) {
		log_qcy(DEBUG_INFO, "audio server is not ready for message processing!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d", ret);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_VERBOSE, "push into the audio message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in audio error =%d", ret);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d", ret1);
	return ret;
}
