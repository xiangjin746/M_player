
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
    /* close each stream */
    if (audio_stream >= 0)
        stream_component_close(audio_stream);  // 解码器线程请求abort的时候有调用 packet_queue_abort
    if (video_stream >= 0)
        stream_component_close(video_stream);

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

// 打开指定stream对应解码器、创建解码线程、以及初始化对应的输出
int FFPlayer::stream_component_open(int stream_index)
{ 
    AVCodecContext *avctx;
    AVCodec *codec;
    int sample_rate;
    int nb_channels;
    int64_t channel_layout;
    int ret;

    // 1. 检查`stream_index`的合法性
    if (stream_index < 0 || stream_index >= ic->nb_streams) {
        av_log(NULL, AV_LOG_FATAL, "Invalid stream index %d\n", stream_index);
        ret = -1;
        goto out;
    }
    // 2. 分配编解码器上下文
    avctx = avcodec_alloc_context3(NULL);
    if(!avctx){
        ret = AVERROR(ENOMEM);
        goto out;
    }

    // 3. 拷贝码流参数到编解码器上下文
    ret = avcodec_parameters_to_context(avctx,ic->streams[stream_index]->codecpar);
    if(ret < 0){
        goto fail;
    }

    // 4. 查找并打开解码器
    codec = avcodec_find_decoder(avctx->codec_id);
    if(!codec){
        av_log(NULL, AV_LOG_FATAL, "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if( (ret = avcodec_open2(avctx,codec,NULL) ) < 0){
         goto fail;
    }

    // 5. 根据流类型处理
    switch(avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            sample_rate = avctx->sample_rate;
            nb_channels = avctx->channels;
            channel_layout = avctx->channel_layout;

            audio_stream = stream_index;
            audio_st = ic->streams[stream_index];
            break;
        case AVMEDIA_TYPE_VIDEO:
            video_stream = stream_index;
            video_st = ic->streams[stream_index];
            break;
        default:
            break;
    }

    // 6. 错误处理
fail:
    avcodec_free_context(&avctx);
out:
    return ret;
}

// 关闭指定stream的解码线程，释放解码器资源
void FFPlayer::stream_component_close(int stream_index)
{
    AVCodecParameters *codecpar;
    
    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;

    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            // ... 关闭音频相关的组件
            break;
        case AVMEDIA_TYPE_VIDEO:
            // ... 关闭视频相关的组件
            break;
        default:
            break;
    }

    // ic->streams[stream_index]->discard = AVDISCARD_ALL;  // 这个又有什么用?
    // 这行注释掉的代码看起来是用于告诉解复用器（demuxer）放弃处理指定流的所有数据包。AVDISCARD_ALL 通常用于指示解复用器忽略该流，不再读取或处理其数据。

    switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            audio_st = NULL;
            audio_stream = -1;
            break;
        case AVMEDIA_TYPE_VIDEO:
            video_st = NULL;
            video_stream = -1;
            break;
        default:
            break;
    }
}

int FFPlayer::read_thread() {
    int err,i,ret;
    int st_index[AVMEDIA_TYPE_NB];
    AVPacket pkt1;
    AVPacket *pkt = &pkt1;

    memset(st_index,-1,sizeof(st_index));
    video_stream = -1;
    audio_stream = -1;
    eof =0;

    // 创建输入上下文
    ic = avformat_alloc_context();
    if(!ic){
        av_log(NULL, AV_LOG_FATAL, "Could not allocate input context\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // 打开文件并探测协议类型: avformat_open_input 
    if(avformat_open_input(&ic,input_filename_,NULL,NULL)!= 0){
        av_log(NULL, AV_LOG_FATAL, "Could not open input file\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }
    ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT);
    std::cout << "read_thread FFP_MSG_OPEN_INPUT " << this << std::endl;

    // 探测媒体类型:avformat_find_stream_info 
    err = avformat_find_stream_info(ic,NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_WARNING,
               "%s: could not find codec parameters\n", input_filename_);
        ret = -1;
        goto fail;
    }

    ffp_notify_msg1(this, FFP_MSG_FIND_STREAM_INFO);
    std::cout << "read_thread FFP_MSG_FIND_STREAM_INFO " << this << std::endl;

    // 选择最佳流:av_find_best_stream 
    st_index[AVMEDIA_TYPE_VIDEO] = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    st_index[AVMEDIA_TYPE_AUDIO] = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, st_index[AVMEDIA_TYPE_AUDIO], st_index[AVMEDIA_TYPE_VIDEO], NULL, 0);

    /* open the streams */
    /* 打开视频、音频解码器。在此会打开相应解码器，并创建相应的解码线程。 */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {// 如果有音频流则打开音频流
        ret = stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) { // 如果有视频流则打开视频流
        ret = stream_component_open( st_index[AVMEDIA_TYPE_VIDEO]);
    }

    ffp_notify_msg1(this, FFP_MSG_COMPONENT_OPEN);
    std::cout << "read_thread FFP_MSG_COMPONENT_OPEN " << this << std::endl;

    if (video_stream < 0 && audio_stream < 0) {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               input_filename_);
        ret = -1;
        goto fail;
    }

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
fail:
    return ret;
}
