
#include <iostream>
#include <string.h>
#include "ffmsg.h"
#include "SDL.h"
#include "ff_ffplay.h"

FFPlayer::FFPlayer() {

}

FFPlayer::~FFPlayer() {

}

int FFPlayer::ffp_create() {
    std::cout << "ffp_create" << std::endl;
    msg_queue_init(&msg_queue_);
    return 0;
}

void FFPlayer::ffp_destroy()
{
    stream_close();

    // 销毁消息队列
    msg_queue_destroy(&msg_queue_);
}

int FFPlayer::ffp_prepare_async_l(char *file_name) {
    input_filename_ = strdup(file_name);

    int reval = stream_open(input_filename_);

    return reval;
}

int FFPlayer::ffp_start_l()
{
    // 触发播放
    std::cout << __FUNCTION__;
}

int FFPlayer::ffp_stop_l()
{
    abort_request = 1;  // 请求退出
    msg_queue_abort(&msg_queue_);  // 禁止再插入消息
}


int FFPlayer::stream_open(const char *file_name) {
    // ● 初始化SDL以允许⾳频输出；
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)){
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        return -1;
    }
    // ● 初始化帧Frame队列
    if (frame_queue_init(&pictq,&videoq,VIDEO_PICTURE_QUEUE_SIZE_DEFAULT) < 0 || frame_queue_init(&sampq,&audioq,SAMPLE_QUEUE_SIZE) < 0){
        goto fail;
    }

    // ● 初始化包Packet队列
    if (packet_queue_init(&videoq) < 0 || packet_queue_init(&audioq) < 0){
        goto fail;
    }

    // ● 初始化时钟Clock
    // ● 初始化⾳量
    // ● 创建解复⽤读取线程read_thread
    read_thread_ = new std::thread(& FFPlayer::read_thread,this);
    // ● 创建视频刷新线程video_refresh_thread

    return 0;
fail:
    stream_close();
    return -1;
}

void FFPlayer::stream_close()
{
    abort_request = 1; // 请求退出
    if(read_thread_ && read_thread_->joinable()) {
        read_thread_->join();       // 等待线程退出
    }

    // 关闭解复用器 avformat_close_input(&is->ic);
    // 释放packet队列
    packet_queue_destroy(&videoq);
    packet_queue_destroy(&audioq);

    // 释放frame队列
    frame_queue_destory(&pictq);
    frame_queue_destory(&sampq);

    if(input_filename_) {
        free(input_filename_);
        input_filename_ = NULL;
    }
}

int FFPlayer::read_thread() {
    ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT);
    std::cout << "read_thread FFP_MSG_OPEN_INPUT " << this << std::endl;
    ffp_notify_msg1(this, FFP_MSG_FIND_STREAM_INFO);
    std::cout << "read_thread FFP_MSG_FIND_STREAM_INFO " << this << std::endl;
    ffp_notify_msg1(this, FFP_MSG_COMPONENT_OPEN);
    std::cout << "read_thread FFP_MSG_COMPONENT_OPEN " << this << std::endl;
    ffp_notify_msg1(this, FFP_MSG_PREPARED);
    std::cout << "read_thread FFP_MSG_PREPARED " << this << std::endl;

    while (1) {
       std::cout << "read_thread sleep, mp:" << this << std::endl;
        // 先模拟线程运行
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if(abort_request){
            break;
        }
    }

    std::cout << __FUNCTION__ << " leave" << std::endl;

    return 0;
}
