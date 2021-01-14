/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   play_audio.c
 * Author: ljx
 *
 * Created on September 27, 2020, 5:14 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <rtscamkit.h>
#include <rts_queue.h>
#include <rtsavapi.h>
#include <rtsaudio.h>
#include "rtsavdef.h"
#include <rts_errno.h>
//*
#include "../../tools/tools_interface.h"
#include "../../manager/global_interface.h"
//*
#include "play_audio.h"
#include "audio.h"
/*
 * 
 */
#define SIZE 512


int play_audio(char *path)
{
    int ret;
    FILE *fp = NULL;
    char buffer[SIZE] = {0};

    int finish = 0;

    if(access(path, R_OK))
    {
        log_qcy(DEBUG_SERIOUS, "can not find %s", path);
        return -1;
    }
    
    fp = fopen(path, "rb");
    if (!fp) {
    	log_qcy(DEBUG_SERIOUS, "fail to open %s: %s", path, strerror(errno));
        ret = -1;
        goto exit;
    }
    //第一次，跳过文件头
    fseek(fp, 0, SEEK_SET);
    fseek(fp, 0x3a, SEEK_SET);

    clearerr(fp);

    usleep(1000 * 1000 * 2);
    while (1) {

    	ret = fread(buffer, 1, SIZE, fp);

    	if(ret != SIZE && feof(fp))
    	{
    		if(ret != 0)
    			audio_speaker(buffer, ret);
    		log_qcy(DEBUG_INFO, "finish\n");
    		ret = 0;
    		break;
    	}else if(ferror(fp))
    	{
    		log_qcy(DEBUG_SERIOUS, "fread error");
    		ret = -1;
    		break;
    	}else
    	{
    		if(audio_speaker(buffer, ret))
    		{
    			ret = -1;
    			break;
    		}
    	}
    }
    fclose(fp);
exit:
    return ret;
}
