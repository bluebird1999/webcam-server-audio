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
#include "../../server/micloud/micloud_interface.h"
#include "../../server/device/device_interface.h"
//server header
#include "audio.h"
#include "config.h"
#include "audio_interface.h"
#include "play_audio.h"

/*
 * static
 */
//variable
static 	message_buffer_t	message;
static 	server_info_t 		info;
static	audio_stream_t		stream={-1,-1,-1,-1,-1,-1,-1,-1};
static	audio_config_t		config;
static 	av_buffer_t			abuffer;
static  pthread_rwlock_t	ilock = PTHREAD_RWLOCK_INITIALIZER;
static	pthread_rwlock_t	alock = PTHREAD_RWLOCK_INITIALIZER;
static	pthread_mutex_t		mutex = PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t		cond = PTHREAD_COND_INITIALIZER;
static 	miss_session_t		*session[MAX_SESSION_NUMBER];

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static void server_release_1(void);
static void server_release_2(void);
static void server_release_3(void);
static void server_thread_termination(void);
static void task_default(void);
static void task_start(void);
static void task_stop(void);
static void task_exit(void);
//specific
static int stream_init(void);
static int stream_destroy(void);
static int stream_start(void);
static int stream_stop(void);
static int audio_init(void);
static int write_audio_buffer(av_packet_t *data, int id, int target, int type);
static void write_audio_info(struct rts_av_buffer *data, av_data_info_t	*info);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int server_set_status(int type, int st, int value)
{
	int ret=0;
	pthread_rwlock_wrlock(&ilock);
	if(type == STATUS_TYPE_STATUS)
		info.status = st;
	else if(type==STATUS_TYPE_EXIT)
		info.exit = st;
	else if(type==STATUS_TYPE_CONFIG)
		config.status = st;
	else if(type==STATUS_TYPE_THREAD_START)
		misc_set_bit(&info.thread_start, st, value);
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static void recycle_buffer(void *master, struct rts_av_buffer *buffer)
{
    RTS_SAFE_RELEASE(buffer, rts_av_delete_buffer);
}

static int audio_play(char *path)
{
    int ret;
	message_t dev_send_msg;
	device_iot_config_t device_iot_tmp;
	msg_init(&dev_send_msg);
	memset(&device_iot_tmp, 0 , sizeof(device_iot_config_t));
	device_iot_tmp.amp_on_off = 1;
	dev_send_msg.message = MSG_DEVICE_CTRL_DIRECT;
	dev_send_msg.sender = dev_send_msg.receiver = SERVER_AUDIO;
	dev_send_msg.arg = (void*)&device_iot_tmp;
	dev_send_msg.arg_in.cat = DEVICE_CTRL_AMPLIFIER;
	dev_send_msg.arg_size = sizeof(device_iot_config_t);
	manager_common_send_message(SERVER_DEVICE, &dev_send_msg);

    ret = play_audio(path);
    if(ret)
        log_qcy(DEBUG_WARNING, "play_audio failed");
    return ret;
}

static int send_iot_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg, int size)
{
	int ret = 0;
    /********message body********/
//	msg_init(msg);
	memcpy(&(msg->arg_pass), &(org_msg->arg_pass),sizeof(message_arg_t));
	msg->message = id | 0x1000;
	msg->sender = msg->receiver = SERVER_AUDIO;
	msg->result = result;
	msg->arg = arg;
	msg->arg_size = size;
	ret = manager_common_send_message(receiver, msg);
	/***************************/
	return ret;
}

static int audio_quit_send(int server, int channel)
{
	int ret = 0;
	message_t msg;
	msg_init(&msg);
	msg.sender = msg.receiver = server;
	msg.arg_in.wolf = channel;
	msg.message = MSG_AUDIO_STOP;
	manager_common_send_message(SERVER_AUDIO, &msg);
	return ret;
}

static int *audio_main_func(void* arg)
{
	int ret=0, i, st;
	audio_stream_t ctrl;
	av_qos_t qos;
	av_packet_t *packet = NULL;
	struct rts_av_buffer *buffer = NULL;
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    misc_set_thread_name("server_audio_main");
    pthread_detach(pthread_self());
    //init
    memset( &qos, 0, sizeof(qos));
    memcpy( &ctrl,(audio_stream_t*)arg, sizeof(audio_stream_t));
    av_buffer_init(&abuffer, &alock);
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_AUDIO, 1 );
    manager_common_send_dummy(SERVER_AUDIO);
    while( 1 ) {
    	st = info.status;
    	if( info.exit )
    		break;
    	if( misc_get_bit(info.thread_exit, THREAD_AUDIO) )
    		break;
    	if( info.status != STATUS_RUN )
    		continue;
    	usleep(1000);
    	ret = rts_av_poll(stream.encode_ch);
    	if(ret)
    		continue;
    	ret = rts_av_recv(stream.encode_ch, &buffer);
    	if(ret) {
    		if( buffer )
    			rts_av_put_buffer(buffer);
    		continue;
    	}
    	if ( buffer ) {
        	if( (info.status2 == (1<<RUN_MODE_SPEAKER)) ) {
        		rts_av_put_buffer(buffer);
        		continue;
        	}
        	if( buffer->bytesused > MAX_AUDIO_FRAME_SIZE ) {
    			log_qcy(DEBUG_WARNING, "++++++++++++++++++++realtek audio frame size=%d!!!!!!", buffer->bytesused);
    			rts_av_put_buffer(buffer);
    			continue;
        	}
        	if( misc_mips_address_check((unsigned int)buffer->vm_addr) ) {
    			log_qcy(DEBUG_WARNING, "realtek audio memory address anomity =%p!!!!!!", buffer->vm_addr);
    			rts_av_put_buffer(buffer);
    			continue;
        	}
    		if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
				packet = av_buffer_get_empty(&abuffer, &qos.buffer_overrun, &qos.buffer_success);
				if( packet == NULL ) {
					log_qcy(DEBUG_INFO, "-------------AUDIO buffer overrun!!!---");
					rts_av_put_buffer(buffer);
					continue;
				}
				packet->data = malloc( buffer->bytesused );
				if( packet->data == NULL) {
					log_qcy(DEBUG_WARNING, "allocate memory failed in audio buffer, size=%d", buffer->bytesused);
					rts_av_put_buffer(buffer);
					continue;
				}
				memcpy(packet->data, buffer->vm_addr, buffer->bytesused);
    		}
    		else {
        		packet = &(abuffer.packet[0]);
        		packet->data = buffer->vm_addr;
    		}
    		if( (stream.realtek_stamp == 0) && (stream.unix_stamp == 0) ) {
    			stream.realtek_stamp = buffer->timestamp;
    			stream.unix_stamp = time_get_now_stamp();
    		}
    		write_audio_info( buffer, &packet->info);
    		for(i=0;i<MAX_SESSION_NUMBER;i++) {
    			if( misc_get_bit(info.status2, RUN_MODE_MISS+i) ) {
    				ret = write_audio_buffer(packet, MSG_MISS_AUDIO_DATA, SERVER_MISS, i);
    				if( (ret == MISS_LOCAL_ERR_MISS_GONE) || (ret == MISS_LOCAL_ERR_SESSION_GONE) ) {
    					log_qcy(DEBUG_WARNING, "Miss audio ring buffer send failed due to non-existing miss server or session");
    					audio_quit_send(SERVER_MISS, i);
    					log_qcy(DEBUG_WARNING, "----shut down audio miss stream due to session lost!------");
    				}
    				else if( ret == MISS_LOCAL_ERR_AV_NOT_RUN) {
    					qos.failed_send[RUN_MODE_MISS+i]++;
    					if( qos.failed_send[RUN_MODE_MISS+i] > AUDIO_MAX_FAILED_SEND) {
    						qos.failed_send[RUN_MODE_MISS+i] = 0;
    						audio_quit_send(SERVER_MISS, i);
    						log_qcy(DEBUG_WARNING, "----shut down audio miss stream due to long overrun!------");
    					}
    				}
					else if( ret == MISS_LOCAL_ERR_MSG_BUFF_FULL ) {

					}
    				else if( ret == 0) {
    					qos.failed_send[RUN_MODE_MISS+i] = 0;
						if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
							av_packet_add(packet);
						}
    				}
    			}
    		}
    		for(i=0;i<MAX_RECORDER_JOB;i++) {
				if( misc_get_bit(info.status2, RUN_MODE_SAVE+i) ) {
					ret = write_audio_buffer(packet, MSG_RECORDER_AUDIO_DATA, SERVER_RECORDER, i);
					if( ret) {
						qos.failed_send[RUN_MODE_SAVE+i]++;
						if( qos.failed_send[RUN_MODE_SAVE+i] > AUDIO_MAX_FAILED_SEND) {
							qos.failed_send[RUN_MODE_SAVE+i] = 0;
							audio_quit_send(SERVER_RECORDER, i);
							log_qcy(DEBUG_WARNING, "----shut down audio recorder stream due to long overrun!------");
						}
					}
					else {
						if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
							av_packet_add(packet);
						}
						qos.failed_send[RUN_MODE_SAVE+i] = 0;
					}
				}
			}
			if( misc_get_bit(info.status2, RUN_MODE_MICLOUD) ) {
				if( write_audio_buffer(packet, MSG_MICLOUD_AUDIO_DATA, SERVER_MICLOUD,0) != 0 ) {
					log_qcy(DEBUG_WARNING, "Micloud ring buffer push failed!");
				}
				else {
					if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
						av_packet_add(packet);
					}
				}
			}
    		if( _config_.memory_mode == MEMORY_MODE_SHARED) {
    			av_packet_check(packet);
    		}
			packet = NULL;
   			rts_av_put_buffer(buffer);
    	}
    }
    //release
exit:
	av_buffer_release(&abuffer);
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_AUDIO, 0 );
    manager_common_send_dummy(SERVER_AUDIO);
    log_qcy(DEBUG_INFO, "-----------thread exit: server_audio_main-----------");
    pthread_exit(0);
}

static int stream_init(void)
{
	stream.capture_ch = -1;
	stream.encode_ch = -1;
	stream.decode_ch = -1;
	stream.atoe_resample_ch = -1;
	stream.capture_aec_ch = -1;
	stream.ptoc_resample_ch = -1;
	stream.dtom_resample_ch = -1;
	stream.mixer_ch = -1;
	stream.playback_ch = -1;
	stream.frame = 0;
}

static int stream_destroy(void)
{
	int ret = 0;
    RTS_SAFE_CLOSE(stream.decode_ch, rts_av_destroy_chn);
    RTS_SAFE_CLOSE(stream.dtom_resample_ch, rts_av_destroy_chn);
    RTS_SAFE_CLOSE(stream.mixer_ch, rts_av_destroy_chn);
    RTS_SAFE_CLOSE(stream.playback_ch, rts_av_destroy_chn);
    RTS_SAFE_CLOSE(stream.ptoc_resample_ch, rts_av_destroy_chn);
    RTS_SAFE_CLOSE(stream.capture_ch, rts_av_destroy_chn);
    RTS_SAFE_CLOSE(stream.capture_aec_ch, rts_av_destroy_chn);
    RTS_SAFE_CLOSE(stream.atoe_resample_ch, rts_av_destroy_chn);
    RTS_SAFE_CLOSE(stream.encode_ch, rts_av_destroy_chn);
    stream_init();
	return ret;
}

static int stream_start(void)
{
	int ret=0;
	struct rts_aec_control *aec_ctrl;
	int volume;
	pthread_t	id;
    ret = rts_av_enable_chn(stream.decode_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable decode_ch fail, ret = %d", ret);
		return -1;
	}
    ret = rts_av_enable_chn(stream.dtom_resample_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable dtom_resample_ch fail, ret = %d", ret);
		return -1;
	}
    ret = rts_av_enable_chn(stream.mixer_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable mixer_ch fail, ret = %d", ret);
		return -1;
	}
    ret = rts_av_enable_chn(stream.playback_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable playback_ch fail, ret = %d", ret);
		return -1;
	}
    ret = rts_av_enable_chn(stream.ptoc_resample_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable ptoc_resample_ch fail, ret = %d", ret);
		return -1;
	}
    ret = rts_av_enable_chn(stream.capture_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable capture_ch fail, ret = %d", ret);
		return -1;
	}
    ret = rts_av_enable_chn(stream.capture_aec_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable capture_aec_ch fail, ret = %d", ret);
		return -1;
	}
    ret = rts_av_enable_chn(stream.atoe_resample_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable atoe_resample_ch fail, ret = %d", ret);
		return -1;
	}
    ret = rts_av_enable_chn(stream.encode_ch);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "enable encode_ch fail, ret = %d", ret);
		return -1;
	}

/*
	if( stream.ptoc_resample_ch != -1 ) {
		ret = rts_av_enable_chn(stream.ptoc_resample_ch);
		if (ret) {
			log_qcy(DEBUG_SERIOUS, "enable ptoc_resample_ch fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	log_qcy(DEBUG_INFO, "enable ptoc_resample_ch chnno:%d success", stream.ptoc_resample_ch);
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
	if( stream.capture_aec_ch != -1 ) {
		ret = rts_av_enable_chn(stream.capture_aec_ch);
		if (ret) {
			log_qcy(DEBUG_SERIOUS, "enable capture_aec_ch fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	if( stream.atoe_resample_ch != -1 ) {
		ret = rts_av_enable_chn(stream.atoe_resample_ch);
		if (ret) {
			log_qcy(DEBUG_SERIOUS, "enable atoe_resample_ch fail, ret = %d", ret);
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
*/
	rts_av_query_aec_ctrl(stream.capture_aec_ch, &aec_ctrl);
	aec_ctrl->aec_enable = config.profile.aec_enable;
	aec_ctrl->ns_enable = config.profile.ns_enable;
	aec_ctrl->ns_level = config.profile.ns_level;
	aec_ctrl->aec_scale = config.profile.aec_scale;
	aec_ctrl->aec_thr = config.profile.aec_thr;
	rts_av_set_aec_ctrl(aec_ctrl);
	rts_av_release_aec_ctrl(aec_ctrl);
	aec_ctrl = NULL;
	stream.frame = 0;
	rts_audio_get_playback_volume(&volume);
	rts_audio_set_playback_volume(config.profile.playback_volume);
	rts_audio_get_capture_volume(&volume);
	rts_audio_set_capture_volume(config.profile.capture_volume);
	//***
    ret = rts_av_start_recv(stream.encode_ch);
    if (ret) {
    	log_qcy(DEBUG_SERIOUS, "start recv audio fail, ret = %d", ret);
    	return -1;
    }
    ret = rts_av_start_send(stream.decode_ch);
    if (ret) {
    	log_qcy(DEBUG_SERIOUS, "start send audio fail, ret = %d", ret);
    	return -1;
    }
	ret = pthread_create(&id, NULL, audio_main_func, (void*)&stream);
	if(ret != 0) {
		log_qcy(DEBUG_INFO, "audio main thread create error! ret = %d",ret);
		return -1;
	 }
	else {
		log_qcy(DEBUG_SERIOUS, "audio main thread create successful!");
	}
    return 0;
}

static int stream_stop(void)
{
	int ret=0;
	if(stream.encode_ch!=-1)
		ret = rts_av_stop_recv(stream.encode_ch);
	if(stream.ptoc_resample_ch!=-1)
		ret = rts_av_disable_chn(stream.ptoc_resample_ch);
	if(stream.capture_ch!=-1)
		ret = rts_av_disable_chn(stream.capture_ch);
	if(stream.capture_aec_ch!=-1)
		ret = rts_av_disable_chn(stream.capture_aec_ch);
	if(stream.atoe_resample_ch!=-1)
		ret = rts_av_disable_chn(stream.atoe_resample_ch);
	if(stream.encode_ch!=-1)
		ret = rts_av_disable_chn(stream.encode_ch);
	stream.frame = 0;
	stream.realtek_stamp = 0;
	stream.unix_stamp = 0;
	return ret;
}

static int audio_init(void)
{
	int ret;
	int codec_samplerate = 16000;
	int codec_format = 16;
	int codec_channels = 2;
	struct rts_av_profile profile;
	stream_init();
	//start
	stream.decode_ch = rts_av_create_audio_decode_chn();
	if (RTS_IS_ERR(stream.decode_ch)) {
		 ret = stream.decode_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "audio decode chn : %d", stream.decode_ch);
	rts_av_get_profile(stream.decode_ch, &profile);
	profile.fmt = RTS_A_FMT_ULAW;
	ret = rts_av_set_profile(stream.decode_ch, &profile);
	if (RTS_IS_ERR(ret)) {
		 log_qcy(DEBUG_WARNING, "set decode fail, ret = %d", ret);
		 return ret;
	}
	stream.dtom_resample_ch = rts_av_create_audio_resample_chn(
		config.playback.rate, config.playback.format, config.playback.channels);
	if (RTS_IS_ERR(stream.dtom_resample_ch)) {
		 ret = stream.dtom_resample_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "audio resample chn : %d", stream.dtom_resample_ch);
	stream.mixer_ch = rts_av_create_audio_mixer_chn();
	if (RTS_IS_ERR(stream.mixer_ch)) {
		 ret = stream.mixer_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "audio mixer chn : %d", stream.mixer_ch);
	stream.playback_ch = rts_av_create_audio_playback_chn(&config.playback);
	if (RTS_IS_ERR(stream.playback_ch)) {
		 ret = stream.playback_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "audio playback chn : %d", stream.playback_ch);
	stream.ptoc_resample_ch = rts_av_create_audio_resample_chn(
		config.capture.rate, config.capture.format, config.capture.channels);
	if (RTS_IS_ERR(stream.ptoc_resample_ch)) {
		 ret = stream.ptoc_resample_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "audio resample chn : %d", stream.ptoc_resample_ch);
	stream.capture_ch = rts_av_create_audio_capture_chn(&config.capture);
	if (RTS_IS_ERR(stream.capture_ch)) {
		 ret = stream.capture_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "audio capture chn : %d", stream.capture_ch);
	stream.capture_aec_ch = rts_av_create_audio_aec_chn();
	if (RTS_IS_ERR(stream.capture_aec_ch)) {
		 ret = stream.capture_aec_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "audio aec chn : %d", stream.capture_aec_ch);
	stream.encode_ch = rts_av_create_audio_encode_chn(RTS_AUDIO_TYPE_ID_ALAW, 0);
	if (RTS_IS_ERR(stream.encode_ch)) {
		 ret = stream.encode_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "encode chn : %d", stream.encode_ch);
	rts_av_get_profile(stream.encode_ch, &profile);
	codec_samplerate = profile.audio.samplerate;
	codec_format = profile.audio.bitfmt;
	codec_channels = profile.audio.channels;
	stream.atoe_resample_ch = rts_av_create_audio_resample_chn(
				 codec_samplerate, codec_format,
				 codec_channels);
	if (RTS_IS_ERR(stream.atoe_resample_ch)) {
		 ret = stream.atoe_resample_ch;
		 return ret;
	}
	log_qcy(DEBUG_INFO, "audio resample chn : %d", stream.atoe_resample_ch);

/*
	stream.ptoc_resample_ch = rts_av_create_audio_resample_chn(config.capture.rate, config.capture.format, config.capture.channels);
	if (stream.ptoc_resample_ch < 0) {
		log_qcy(DEBUG_SERIOUS, "fail to create audio ptoc_resample_ch chn, ret = %d", stream.ptoc_resample_ch);
		return -1;
	}
	log_qcy(DEBUG_INFO, "ptoc_resample_ch chnno:%d", stream.ptoc_resample_ch);
	stream.capture = rts_av_create_audio_capture_chn(&config.capture);
	if (stream.capture < 0) {
		log_qcy(DEBUG_SERIOUS, "fail to create audio capture chn, ret = %d", stream.capture);
		return -1;
	}
	log_qcy(DEBUG_INFO, "capture chnno:%d", stream.capture);
	stream.capture_aec_ch = rts_av_create_audio_aec_chn();
	if (stream.capture_aec_ch < 0) {
		log_qcy(DEBUG_SERIOUS, "fail to create audio capture_aec_ch chn, ret = %d", stream.capture_aec_ch );
		return -1;
	}
	log_qcy(DEBUG_INFO, "capture_aec_ch chnno:%d", stream.capture_aec_ch);

	stream.encoder = rts_av_create_audio_encode_chn(RTS_AUDIO_TYPE_ID_ALAW, 0);
	if (stream.encoder < 0) {
		log_qcy(DEBUG_SERIOUS, "fail to create audio encoder chn, ret = %d", stream.encoder);
		return -1;
	}
	log_qcy(DEBUG_INFO, "encoder chnno:%d", stream.encoder);
	rts_av_get_profile(stream.encoder, &profile);
	codec_samplerate = profile.audio.samplerate;
	codec_format = profile.audio.bitfmt;
	codec_channels = profile.audio.channels;
	stream.atoe_resample_ch = rts_av_create_audio_resample_chn(
	codec_samplerate, codec_format, codec_channels);
	if (stream.atoe_resample_ch < 0) {
		log_qcy(DEBUG_SERIOUS, "fail to create audio atoe_resample_ch chn, ret = %d", stream.atoe_resample_ch);
		return -1;
	}
	log_qcy(DEBUG_INFO, "atoe_resample_ch chnno:%d", stream.atoe_resample_ch);
*/
	ret = rts_av_bind(stream.decode_ch, stream.dtom_resample_ch);
	if (ret) {
		 log_qcy(DEBUG_WARNING, "fail to bind decode and resample, ret = %d", ret);
		 return ret;
	}
	ret = rts_av_bind(stream.dtom_resample_ch, stream.mixer_ch);
	if (ret) {
		 log_qcy(DEBUG_WARNING, "fail to bind resample and mixer, ret = %d", ret);
		 return ret;
	}
	ret = rts_av_bind(stream.mixer_ch, stream.playback_ch);
	if (ret) {
		 log_qcy(DEBUG_WARNING, "fail to bind mixer and playback, ret = %d", ret);
		 return ret;
	}
	ret = rts_av_bind(stream.playback_ch, stream.ptoc_resample_ch);
	if (ret) {
		 log_qcy(DEBUG_WARNING, "fail to bind playback and resample, ret = %d", ret);
		 return ret;
	}
	ret = rts_av_bind(stream.ptoc_resample_ch, stream.capture_aec_ch);
	if (ret) {
		 log_qcy(DEBUG_WARNING, "fail to bind resample and aec, ret = %d", ret);
		 return ret;
	}
	ret = rts_av_bind(stream.capture_ch, stream.capture_aec_ch);
	if (ret) {
		 log_qcy(DEBUG_WARNING, "fail to bind capture and aec, ret = %d", ret);
		 return ret;
	}
	ret = rts_av_bind(stream.capture_aec_ch, stream.atoe_resample_ch);
	if (ret) {
		 log_qcy(DEBUG_WARNING, "fail to bind aec and resample, ret = %d", ret);
		 return ret;
	}
	ret = rts_av_bind(stream.atoe_resample_ch, stream.encode_ch);
	if (ret) {
		 log_qcy(DEBUG_WARNING, "fail to bind resample and encode, ret = %d", ret);
		 return ret;
	}



/*
	ret = rts_av_bind(stream.playback_ch, stream.ptoc_resample_ch);
	if (ret) {
	   	log_qcy(DEBUG_SERIOUS, "fail to bind playback_ch and ptoc_resample_ch, ret = %d", ret);
	   	return -1;
	}
	ret = rts_av_bind(stream.ptoc_resample_ch, stream.capture_aec_ch);
	if (ret) {
	   	log_qcy(DEBUG_SERIOUS, "fail to bind ptoc_resample_ch and capture_aec_ch, ret = %d", ret);
	   	return -1;
	}
	log_qcy(DEBUG_INFO, "bind ptoc_resample_ch chnno:%d, success", stream.ptoc_resample_ch);
	ret = rts_av_bind(stream.capture, stream.capture_aec_ch);
	if (ret) {
	   	log_qcy(DEBUG_SERIOUS, "fail to bind capture_ch and capture_aec_ch, ret = %d", ret);
	   	return -1;
	}
	ret = rts_av_bind(stream.capture_aec_ch, stream.atoe_resample_ch);
	if (ret) {
	  	log_qcy(DEBUG_SERIOUS, "fail to bind capture_aec_ch and atoe_resample_ch, ret = %d", ret);
	   	return -1;
	}
	ret = rts_av_bind(stream.atoe_resample_ch, stream.encoder);
    if (ret) {
    	log_qcy(DEBUG_SERIOUS, "fail to bind capture and encode, ret = %d", ret);
    	return -1;
    }
*/
	return 0;
}

static void write_audio_info(struct rts_av_buffer *data, av_data_info_t	*info)
{
	info->flag = data->flags;
	info->frame_index = data->frame_idx;
//	info->timestamp = data->timestamp / 1000;	// ms = us/1000
	info->timestamp = ( ( data->timestamp - stream.realtek_stamp ) / 1000) + stream.unix_stamp * 1000;
	info->flag = FLAG_AUDIO_SAMPLE_8K << 3 | FLAG_AUDIO_DATABITS_16 << 7 | FLAG_AUDIO_CHANNEL_MONO << 9 |  FLAG_RESOLUTION_AUDIO_DEFAULT << 17;
	info->size = data->bytesused;
}

static int write_audio_buffer(av_packet_t *data, int id, int target, int channel)
{
	int ret=0;
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.arg_in.wolf = channel;
	msg.arg_in.handler = session[channel];
	msg.sender = msg.receiver = SERVER_AUDIO;
	msg.message = id;
	if( _config_.memory_mode == MEMORY_MODE_SHARED ) {
		msg.arg = data;
		msg.arg_size = 0;	//make sure this is 0 for non-deep-copy
		msg.extra_size = 0;
	}
	else {
		msg.arg = data->data;
		msg.arg_size = data->info.size;
		msg.extra = &(data->info);
		msg.extra_size = sizeof(data->info);
	}
	if( target == SERVER_MISS )
		ret = server_miss_audio_message(&msg);
	else if( target == SERVER_MICLOUD )
		ret = server_micloud_audio_message(&msg);
	else if( target == SERVER_RECORDER )
		ret = server_recorder_audio_message(&msg);
	return ret;
	/****************************/
}

static int audio_add_session(miss_session_t *ses, int sid)
{
	session[sid] = ses;
	return 0;
}

static int audio_remove_session(miss_session_t *ses, int sid)
{
	session[sid] = NULL;
	return 0;
}

static void server_thread_termination(void)
{
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_AUDIO_SIGINT;
	msg.sender = msg.receiver = SERVER_AUDIO;
	/****************************/
	manager_common_send_message(SERVER_MANAGER, &msg);
}

static void audio_broadcast_thread_exit(void)
{
}

static void server_release_1(void)
{
	stream_stop();
	stream_destroy();
	usleep(1000*10);
}

static void server_release_2(void)
{
	msg_buffer_release2(&message, &mutex);
	memset(&config, 0, sizeof(audio_config_t));
	memset(&stream, 0, sizeof(audio_stream_t));
}

static void server_release_3(void)
{
	msg_free(&info.task.msg);
	memset(&info, 0, sizeof(server_info_t));
}
/*
 * State Machine
 */
static int audio_message_block(void)
{
	int ret = 0;
	int id = -1, id1, index = 0;
	message_t msg;
	//search for unblocked message and swap if necessory
	if( !info.msg_lock ) {
		log_qcy(DEBUG_VERBOSE, "===audio message block, return 0 when first message is msg_lock=0");
		return 0;
	}
	index = 0;
	msg_init(&msg);
	ret = msg_buffer_probe_item(&message, index, &msg);
	if( ret ) {
		log_qcy(DEBUG_VERBOSE, "===audio message block, return 0 when first message is empty");
		return 0;
	}
	if( msg_is_system(msg.message) || msg_is_response(msg.message) ) {
		log_qcy(DEBUG_VERBOSE, "===audio message block, return 0 when first message is system or response message %x", msg.message);
		return 0;
	}
	id = msg.message;
	do {
		index++;
		msg_init(&msg);
		ret = msg_buffer_probe_item(&message, index, &msg);
		if(ret) {
			log_qcy(DEBUG_VERBOSE, "===audio message block, return 1 when message index = %d is not found!", index);
			return 1;
		}
		if( msg_is_system(msg.message) ||
				msg_is_response(msg.message) ) {	//find one behind system or response message
			msg_buffer_swap(&message, 0, index);
			id1 = msg.message;
			log_qcy(DEBUG_INFO, "AUDIO: swapped message happend, message %x was swapped with message %x", id, id1);
			return 0;
		}
	}
	while(!ret);
	return ret;
}

static int audio_message_filter(message_t  *msg)
{
	int ret = 0;
	if( info.task.func == task_exit) { //only system message
		if( !msg_is_system(msg->message) && !msg_is_response(msg->message) )
			return 1;
	}
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0;
	message_t msg;
	//condition
	pthread_mutex_lock(&mutex);
	if( message.head == message.tail ) {
		if( (info.status == info.old_status ) ) {
			pthread_cond_wait(&cond,&mutex);
		}
	}
	if( audio_message_block() ) {
		pthread_mutex_unlock(&mutex);
		return 0;
	}
	msg_init(&msg);
	ret = msg_buffer_pop(&message, &msg);
	pthread_mutex_unlock(&mutex);
	if( ret == 1)
		return 0;
	if( audio_message_filter(&msg) ) {
		msg_free(&msg);
		log_qcy(DEBUG_VERBOSE, "AUDIO message--- sender=%d, message=%x, ret=%d, head=%d, tail=%d was screened, the current task is %p", msg.sender, msg.message,
				ret, message.head, message.tail, info.task.func);
		return -1;
	}
	log_qcy(DEBUG_VERBOSE, "-----pop out from the AUDIO message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg.sender, msg.message,
			ret, message.head, message.tail);
	/**************************/
	switch(msg.message) {
		case MSG_AUDIO_START:
			msg_init(&info.task.msg);
			msg_copy(&info.task.msg, &msg);
			info.task.func = task_start;
			info.task.start = info.status;
			info.msg_lock = 1;
			break;
		case MSG_AUDIO_STOP:
			msg_init(&info.task.msg);
			msg_copy(&info.task.msg, &msg);
			info.task.msg.arg_in.cat = info.status2;
			if( msg.sender == SERVER_MISS) misc_set_bit(&info.task.msg.arg_in.cat, (RUN_MODE_MISS + msg.arg_in.wolf), 0);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.task.msg.arg_in.cat, RUN_MODE_MICLOUD, 0);
			if( msg.sender == SERVER_RECORDER) misc_set_bit(&info.task.msg.arg_in.cat, (RUN_MODE_SAVE + msg.arg_in.wolf), 0);
			info.task.func = task_stop;
			info.task.start = info.status;
			info.msg_lock = 1;
			break;
		case MSG_MANAGER_EXIT:
			msg_init(&info.task.msg);
			msg_copy(&info.task.msg, &msg);
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_REALTEK_PROPERTY_NOTIFY:
		case MSG_REALTEK_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == REALTEK_PROPERTY_AV_STATUS ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit(&info.init_status, AUDIO_INIT_CONDITION_REALTEK, 1);
			}
			break;
		case MSG_MANAGER_EXIT_ACK:
			misc_set_bit(&info.error, msg.sender, 0);
			break;
		case MSG_MANAGER_DUMMY:
			break;
        case MSG_AUDIO_SPEAKER_CTL_PLAY: {
        	if( msg.arg_in.cat == SPEAKER_CTL_DEV_START_FINISH ) {
				ret = audio_play(DEV_START_FINISH);
			} else if( msg.arg_in.cat == SPEAKER_CTL_ZBAR_SCAN_SUCCEED ) {
				ret = audio_play(ZBAR_SCAN_SUCCEED);
			} else if( msg.arg_in.cat == SPEAKER_CTL_WIFI_CONNECT ) {
				ret = audio_play(WIFI_CONNECT_SUCCEED);
			} else if( msg.arg_in.cat == SPEAKER_CTL_ZBAR_SCAN ) {
				ret = audio_play(ZBAR_SCAN);
			} else if( msg.arg_in.cat == SPEAKER_CTL_INTERCOM_START ) {
				device_iot_config_t device_iot_tmp;
				message_t send_msg;
				message_t dev_send_msg;
				msg_init(&dev_send_msg);
			    msg_init(&send_msg);
				device_iot_tmp.amp_on_off = 1;
				dev_send_msg.message = MSG_DEVICE_CTRL_DIRECT;
				dev_send_msg.arg_in.cat = DEVICE_CTRL_AMPLIFIER;
				dev_send_msg.sender = dev_send_msg.receiver = SERVER_AUDIO;
				dev_send_msg.arg = (void*)&device_iot_tmp;
				dev_send_msg.arg_size = sizeof(device_iot_config_t);
				manager_common_send_message(SERVER_DEVICE, &dev_send_msg);

				system("amixer cset numid=11 20");
				system("amixer cset numid=1 108");

				send_iot_ack(&msg, &send_msg, MSG_AUDIO_SPEAKER_CTL_PLAY_ACK, msg.receiver, ret,
						NULL, 0);
			} else if( msg.arg_in.cat == SPEAKER_CTL_INTERCOM_STOP ) {
				message_t send_msg;
				device_iot_config_t device_iot_tmp;
				message_t dev_send_msg;
				msg_init(&dev_send_msg);
			    msg_init(&send_msg);
				device_iot_tmp.amp_on_off = 0;
				dev_send_msg.message = MSG_DEVICE_CTRL_DIRECT;
				dev_send_msg.arg_in.cat = DEVICE_CTRL_AMPLIFIER;
				dev_send_msg.sender = dev_send_msg.receiver = SERVER_AUDIO;
				dev_send_msg.arg = (void*)&device_iot_tmp;
				dev_send_msg.arg_size = sizeof(device_iot_config_t);
				manager_common_send_message(SERVER_DEVICE, &dev_send_msg);

				system("amixer cset numid=11 46");
				system("amixer cset numid=1 127");

				send_iot_ack(&msg, &send_msg, MSG_AUDIO_SPEAKER_CTL_PLAY_ACK, msg.receiver, ret,
						NULL, 0);
			}
			else if( msg.arg_in.cat == SPEAKER_CTL_INSTALLING ) {
				ret = audio_play(INSTALLING);
			}
			else if( msg.arg_in.cat == SPEAKER_CTL_INSTALLEND ) {
				ret = audio_play(INSTALLEND);
			}
			else if( msg.arg_in.cat == SPEAKER_CTL_INSTALLFAILED ) {
				ret = audio_play(INSTALLFAILED);
			}
			else if( msg.arg_in.cat == SPEAKER_CTL_RESET ) {
				ret = audio_play(RESET_SUCCESS);
			}
			else if( msg.arg_in.cat == SPEAKER_CTL_INTERNET_CONNECT_DEFEAT ) {
				ret = audio_play(INTERNET_CONNECT_DEFEAT);
			}
            break;
        }
        case MSG_AUDIO_SPEAKER_CTL_DATA:
        	if( msg.arg_in.cat == SPEAKER_CTL_INTERCOM_DATA ) {
        		if(msg.arg) {
					audio_speaker(msg.arg, msg.arg_size);
				}
			}
            break;

		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %x", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

/*
 *
 */
static int server_none(void)
{
	int ret = 0;
	message_t msg;
	if( !misc_get_bit( info.init_status, AUDIO_INIT_CONDITION_CONFIG ) ) {
		ret = config_audio_read(&config);
		if( !ret && misc_full_bit( config.status, CONFIG_AUDIO_MODULE_NUM) ) {
			misc_set_bit(&info.init_status, AUDIO_INIT_CONDITION_CONFIG, 1);
		}
		else {
			info.status = STATUS_ERROR;
			return -1;
		}
	}
	if( !misc_get_bit( info.init_status, AUDIO_INIT_CONDITION_REALTEK ) ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_REALTEK_PROPERTY_GET;
		msg.sender = msg.receiver = SERVER_AUDIO;
		msg.arg_in.cat = REALTEK_PROPERTY_AV_STATUS;
		manager_common_send_message(SERVER_REALTEK, &msg);
		/****************************/
		usleep(MESSAGE_RESENT_SLEEP);
	}
	if( misc_full_bit( info.init_status, AUDIO_INIT_CONDITION_NUM ) ) {
		info.status = STATUS_WAIT;
	}
	return ret;
}

static int server_setup(void)
{
	int ret = 0;
	if( audio_init() == 0)
		info.status = STATUS_IDLE;
	else
		info.status = STATUS_ERROR;
	return ret;
}

static int server_start(void)
{
	int ret = 0;
	static int first_start = 0;
	if( stream_start()==0 ) {
		info.status = STATUS_RUN;
	    if(!first_start) {
	    	ret = audio_play(DEV_START_ING);
	    	first_start = 1;
	    }
	}
	else
		info.status = STATUS_ERROR;
	return ret;
}

static int server_stop(void)
{
	int ret = 0;
	if( stream_stop()==0 )
		info.status = STATUS_IDLE;
	else
		info.status = STATUS_ERROR;
	return ret;
}
/*
 *
 * task
 */
/*
 * task start: idle->start
 */
static void task_start(void)
{
	message_t msg;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_AUDIO;
	msg.result = 0;
	/***************************/
	switch(info.status){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			info.status = STATUS_START;
			break;
		case STATUS_START:
			misc_set_bit(&info.status2, RUN_MODE_SPEAKER, 1);
			server_start();
			break;
		case STATUS_RUN:
			if( misc_get_bit( info.thread_start, THREAD_AUDIO ) )
				goto exit;
			break;
		case STATUS_ERROR:
			msg.result = -1;
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_start = %d", info.status);
			break;
	}
	return;
exit:
	if( msg.result == 0 ) {
		if( info.task.msg.sender == SERVER_MISS ) {
			audio_add_session(info.task.msg.arg_in.handler, info.task.msg.arg_in.wolf);
		}
		if( info.task.msg.sender == SERVER_MISS) misc_set_bit(&info.status2, (RUN_MODE_MISS + info.task.msg.arg_in.wolf), 1);
		if( info.task.msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_MICLOUD, 1);
		if( info.task.msg.sender == SERVER_RECORDER) misc_set_bit(&info.status2, (RUN_MODE_SAVE + info.task.msg.arg_in.wolf), 1);
	}
	manager_common_send_message(info.task.msg.receiver, &msg);
	msg_free(&info.task.msg);
	info.task.func = &task_default;
	info.msg_lock = 0;
	return;
}
/*
 * task start: run->stop->idle
 */
static void task_stop(void)
{
	message_t msg;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_AUDIO;
	msg.result = 0;
	switch(info.status){
		case STATUS_NONE:
		case STATUS_WAIT:
		case STATUS_SETUP:
			goto exit;
			break;
		case STATUS_IDLE:
			if( info.thread_start == 0 )
				goto exit;
			break;
		case STATUS_RUN:
			if( (info.task.msg.arg_in.cat > 0) ||
					(!info.task.msg.arg_in.duck && (info.task.msg.sender == SERVER_RECORDER) ) ) {	//real stop == 0
				goto exit;
				break;
			}
			else
				server_stop();
			break;
		case STATUS_ERROR:
			msg.result = -1;
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_stop = %d", info.status);
			break;
	}
	return;
exit:
	if( msg.result == 0 ) {
		if( info.task.msg.sender == SERVER_MISS ) {
			audio_remove_session(info.task.msg.arg_in.handler, info.task.msg.arg_in.wolf);
		}
		if( info.task.msg.sender == SERVER_MISS) misc_set_bit(&info.status2, (RUN_MODE_MISS + info.task.msg.arg_in.wolf), 0);
		if( info.task.msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_MICLOUD, 0);
		if( info.task.msg.sender == SERVER_RECORDER) misc_set_bit(&info.status2, (RUN_MODE_SAVE + info.task.msg.arg_in.wolf), 0);
	}
	manager_common_send_message(info.task.msg.receiver, &msg);
	msg_free(&info.task.msg);
	info.task.func = &task_default;
	info.msg_lock = 0;
	return;
}

/*
 * default exit: *->exit
 */
static void task_exit(void)
{
	switch( info.status ){
		case EXIT_INIT:
			log_qcy(DEBUG_INFO,"AUDIO: switch to exit task!");
			if( info.task.msg.sender == SERVER_MANAGER) {
				info.error = AUDIO_EXIT_CONDITION;
				info.error &= (info.task.msg.arg_in.cat);
			}
			else {
				info.error = 0;
			}
			info.status = EXIT_SERVER;
			break;
		case EXIT_SERVER:
			if( !info.error )
				info.status = EXIT_STAGE1;
			break;
		case EXIT_STAGE1:
			server_release_1();
			info.status = EXIT_THREAD;
			break;
		case EXIT_THREAD:
			info.thread_exit = info.thread_start;
			audio_broadcast_thread_exit();
			if( !info.thread_start )
				info.status = EXIT_STAGE2;
			break;
		case EXIT_STAGE2:
			server_release_2();
			info.status = EXIT_FINISH;
			break;
		case EXIT_FINISH:
			info.exit = 1;
		    /********message body********/
			message_t msg;
			msg_init(&msg);
			msg.message = MSG_MANAGER_EXIT_ACK;
			msg.sender = SERVER_AUDIO;
			manager_common_send_message(SERVER_MANAGER, &msg);
			/***************************/
			info.status = STATUS_NONE;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_exit = %d", info.status);
			break;
		}
	return;
}

/*
 * default task: none->run
 */
static void task_default(void)
{
	switch( info.status ){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			misc_set_bit(&info.status2, RUN_MODE_SPEAKER, 1);
			server_start();
			break;
		case STATUS_RUN:
			break;
		case STATUS_ERROR:
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
	}
	return;
}

static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	pthread_detach(pthread_self());
	misc_set_thread_name("server_audio");
	msg_buffer_init2(&message, _config_.msg_overrun, &mutex);
	info.init = 1;
	//default task
	info.task.func = task_default;
	while( !info.exit ) {
		info.old_status = info.status;
		info.task.func();
		server_message_proc();
	}
	server_release_3();
	log_qcy(DEBUG_INFO, "-----------thread exit: server_audio-----------");
	pthread_exit(0);
}

/*
 * internal interface
 *
 */
int audio_speaker(char *buffer, unsigned int buffer_size)
{
    struct rts_av_buffer *rts_buffer = NULL;

    if( info.status != STATUS_RUN) {
        log_qcy(DEBUG_SERIOUS, "intercom already stop");
        return -1;
    }
    rts_buffer = rts_av_new_buffer(buffer_size);
    if (!rts_buffer) {
        log_qcy(DEBUG_SERIOUS, "alloc buffer fail");
        return -1;
    }

    rts_av_set_buffer_callback(rts_buffer, &buffer_size, recycle_buffer);
    rts_av_get_buffer(rts_buffer);

    memcpy(rts_buffer->vm_addr, buffer, buffer_size);
    rts_buffer->bytesused = buffer_size;
    rts_buffer->timestamp = 0;
    rts_av_send(stream.decode_ch, rts_buffer);

    RTS_SAFE_RELEASE(rts_buffer, rts_av_put_buffer);
    return 0;
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
	int ret=0;
	pthread_mutex_lock(&mutex);
	if( !message.init ) {
		log_qcy(DEBUG_INFO, "audio server is not ready for message processing!");
		pthread_mutex_unlock(&mutex);
		return -1;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_VERBOSE, "push into the audio message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in audio error =%d", ret);
	else {
		pthread_cond_signal(&cond);
	}
	pthread_mutex_unlock(&mutex);
	return ret;
}
