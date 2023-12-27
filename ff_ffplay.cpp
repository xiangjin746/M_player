
#include <iostream>
#include <string.h>
#include "ffmsg.h"
#include "SDL.h"
#include "ff_ffplay.h"

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

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
    video_refresh_thread_ = new std::thread(& FFPlayer::video_refresh_thread,this);

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
    // 设置pkt_timebase
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

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

            /* prepare audio output 准备音频输出*/
            //调用audio_open打开sdl音频输出，实际打开的设备参数保存在audio_tgt，返回值表示输出设备的缓冲区大小
            if ((ret = audio_open( channel_layout, nb_channels, sample_rate, &audio_tgt)) < 0)
                goto fail;
            
            audio_hw_buf_size = ret;
            audio_src = audio_tgt;  //暂且将数据源参数等同于目标输出参数
            //初始化audio_buf相关参数
            audio_buf_size  = 0;
            audio_buf_index = 0;

            audio_stream = stream_index;
            audio_st = ic->streams[stream_index];
            // 初始化ffplay封装的音频解码器, 并将解码器上下文 avctx和Decoder绑定
             auddec.decoder_init(avctx, &audioq);
            // 启动音频解码线程
            auddec.decoder_start(AVMEDIA_TYPE_AUDIO, "audio_thread", this);

            // 允许音频输出
            //play audio
            SDL_PauseAudio(0);
            break;
        case AVMEDIA_TYPE_VIDEO:
            video_stream = stream_index;
            video_st = ic->streams[stream_index];
            // 初始化ffplay封装的视频解码器
            viddec.decoder_init(avctx, &videoq); //  is->continue_read_thread
            // 启动视频频解码线程
            if ((ret = viddec.decoder_start(AVMEDIA_TYPE_VIDEO, "video_decoder", this)) < 0)
                goto out;
            break;
        default:
            break;
    }

    goto out;//此处不加的话 进入错误处理把avctx释放掉了导致无效的参数了。。。
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

    switch (codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        // ... 关闭音频相关的组件
        // 请求终止解码器线程
        auddec.decoder_abort(&sampq);
        // 关闭音频设备
        // 销毁解码器
        auddec.decoder_destroy();
        // 释放重采样器
        swr_free(&swr_ctx);
        // 释放audio buf
        av_freep(&audio_buf1);
        audio_buf1_size = 0;
        audio_buf = NULL;
        break;
    case AVMEDIA_TYPE_VIDEO:
        // ... 关闭视频相关的组件
        // 请求退出视频画面刷新线程
        if(video_refresh_thread_ && video_refresh_thread_->joinable()) {
            video_refresh_thread_->join();  // 等待线程退出
        }
        // 请求终止解码器线程
        // 关闭音频设备
        // 销毁解码器
        viddec.decoder_abort(&pictq);
        viddec.decoder_destroy();
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

static int audio_decode_frame(FFPlayer *is)
{
    Frame *af = NULL;
    int data_size = 0,resampled_data_size = 0;
    int wanted_nb_samples = 0;
    uint64_t dec_channel_layout = 0;
    int ret = 0;

    // 1.获取可读的音频帧：
    if(!(af = frame_queue_peek_readable(&is->sampq)))
        return -1;

    // 2.计算原始音频数据大小：
    data_size = av_samples_get_buffer_size(NULL, af->frame->channels, af->frame->nb_samples, (enum AVSampleFormat)af->frame->format, 1);

    // 3.确定声道布局：
    dec_channel_layout = (af->frame->channel_layout 
                            && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) 
                            ?  af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);

    wanted_nb_samples = af->frame->nb_samples; // 目前不考虑非音视频同步的是情况

    // 4.音频重采样条件判断和设置：
    if (af->frame->format           != is->audio_src.fmt            || // 采样格式
            dec_channel_layout      != is->audio_src.channel_layout || // 通道布局
            af->frame->sample_rate  != is->audio_src.freq          // 采样率
            ) {
        // ... 设置重采样上下文 ...
        swr_free(&is->swr_ctx);
        is->swr_ctx = swr_alloc_set_opts(NULL,
                                        is->audio_tgt.channel_layout, 
                                        is->audio_tgt.fmt, 
                                        is->audio_tgt.freq,
                                        dec_channel_layout, 
                                        (enum AVSampleFormat)af->frame->format, 
                                        af->frame->sample_rate,
                                        0,NULL);
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0){
            av_log(NULL, AV_LOG_ERROR,
                "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                af->frame->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)af->frame->format), af->frame->channels,
                is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            ret = -1;
            goto fail;
        }
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels = af->frame->channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = (enum AVSampleFormat)af->frame->format;
    }

    // 5.执行音频重采样：
    if (is->swr_ctx) {
        // 设置重采样输入
        // 重采样输入参数1：输入音频样本数是af->frame->nb_samples
        // 重采样输入参数2：输入音频缓冲区
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;

        // 重采样输出参数1：输出音频缓冲区
        uint8_t **out = &is->audio_buf1;     // 输出数据缓冲区

        // 重采样输出参数2：输出音频缓冲区尺寸， 高采样率往低采样率转换时得到更少的样本数量，比如 96k->48k, wanted_nb_samples=1024
        // 则wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate 为1024*48000/96000 = 512
        // +256 的目的是重采样内部是有一定的缓存，就存在上一次的重采样还缓存数据和这一次重采样一起输出的情况，所以目的是多分配输出buffer
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;

         // 计算对应的样本数 对应的采样格式 以及通道数，需要多少buffer空间
        int out_size = av_samples_get_buffer_size(NULL, 
                                                    is->audio_tgt.channels,
                                                    out_count, 
                                                    is->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0) {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            ret = -1;
            goto fail;
        }

        // 分配输出缓冲区
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        // 执行重采样转换
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0) {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            ret = -1;
            goto fail;
        }
        if (len2 == out_count) { // 这里的意思是我已经多分配了buffer，实际输出的样本数不应该超过我多分配的数量
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }

        // 处理转换后的数据
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * av_get_bytes_per_sample(is->audio_tgt.fmt) * is->audio_tgt.channels;

    } else {
        // ... 无需重采样，直接使用原始数据 ...
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    // 6.更新音频帧队列：
    frame_queue_next(&is->sampq);
    ret = resampled_data_size;

fail:
    return ret;
}

/* prepare a new audio buffer */
/**
 * @brief sdl_audio_callback
 * @param opaque    指向user的数据
 * @param stream    拷贝PCM的地址
 * @param len       需要拷贝的长度
 */
static void sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    // 1.初始化
    FFPlayer *is = (FFPlayer *)opaque;
    int audio_size,len1;

    // 2.处理音频数据
    while(len > 0){ //步骤 2.1：循环检查和填充
        if(is->audio_buf_index >= is->audio_buf_size){// 步骤 2.2：检查和填充音频缓冲区
            // 步骤 2.3：解码音频帧
            audio_size = audio_decode_frame(is);
            if(audio_size < 0){
                is->audio_buf = NULL;
                is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
            }else{
                is->audio_buf_size = audio_size; // 更新缓冲区大小
            }
            is->audio_buf_index = 0;
        }

        // 3. 更新和拷贝数据
        len1 = is->audio_buf_size - is->audio_buf_index;// 步骤 3.1：计算拷贝长度
        if(len1 > len) {
            len1 = len;
        }
        
        if(is->audio_buf){
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1); // 步骤 3.2：拷贝数据到SDL缓冲区
        }
        len -= len1;// 步骤 3.3：更新指针和长度
        stream += len1;
        is->audio_buf_index += len1;
    }
}

int FFPlayer::audio_open(int64_t wanted_channel_layout,
                          int wanted_nb_channels, int wanted_sample_rate,
                          struct AudioParams *audio_hw_params)
{
    // 初始化SDL_AudioSpec结构体，设置期望的音频参数。
    SDL_AudioSpec wanted_spec;
    memset(&wanted_spec, 0, sizeof(wanted_spec));
    wanted_spec.freq = wanted_sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 2048;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;

    // 打开音频设备，并确保我们得到了我们请求的格式参数。
    if(SDL_OpenAudio(&wanted_spec,NULL) != 0){
        printf("Failed to open audio device, %s\n", SDL_GetError());
        return -1;
    }

    // 将实际的音频参数（可能与请求的参数有所不同）存储在audio_hw_params结构体中。
    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = wanted_spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels = wanted_spec.channels;
    // 计算每个采样点的大小。
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 
                                                                1, 
                                                                audio_hw_params->fmt, 1);
    // 计算每秒音频数据的字节大小。
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 
                                                                audio_hw_params->freq, // 44100
                                                                audio_hw_params->fmt, 1);
    // 检查参数是否有效。
    if ( audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0){
        printf("av_samples_get_buffer_size failed!\n");
        return -1;
    }

    // 返回SDL内部音频缓冲区的大小。
    return wanted_spec.size;    /* SDL内部缓存的数据字节, samples * channels *byte_per_sample */
}

void FFPlayer::audio_close()
{
    SDL_CloseAudio();  // SDL_CloseAudioDevice
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
    //    std::cout << "read_thread sleep, mp:" << this << std::endl;
        // 先模拟线程运行
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(abort_request){
            break;
        }
        
        // 7.读取媒体数据，得到的是音视频分离后、解码前的数据
        ret = av_read_frame(ic, pkt); // 调用不会释放pkt的数据，需要我们自己去释放packet的数据
        if(ret < 0) { // 出错或者已经读取完毕了
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !eof) {        // 读取完毕了
                eof = 1;
            }
            if (ic->pb && ic->pb->error)  // io异常 // 退出循环
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));     // 读取完数据了，这里可以使用timeout的方式休眠等待下一步的检测
            continue;		// 继续循环
        } else {
            eof = 0;
        }
        // 插入队列  先只处理音频包
        if (pkt->stream_index == audio_stream) {
            printf("audio ===== pkt pts:%ld, dts:%ld\n", pkt->pts/48, pkt->dts);
            packet_queue_put(&audioq, pkt);
        } else {
            av_packet_unref(pkt);// // 不入队列则直接释放数据
        }
    }

    std::cout << __FUNCTION__ << " leave" << std::endl;
fail:
    return 0;
}

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.04  // 每帧休眠10ms

// 默认是10ms检测一次是不是下一帧要输出了
// 如果只差5ms输出 remaining_time =
int FFPlayer::video_refresh_thread()
{
    double remaining_time = 0.0;

    while (!abort_request){
        if(remaining_time > 0.0){
            av_usleep((int)(int64_t)(remaining_time*1000000.0));
        }
        remaining_time = REFRESH_RATE;
        video_refresh(&remaining_time);
    }
    std::cout << __FUNCTION__ << " leave" << std::endl;
}

void FFPlayer::video_refresh(double *remaining_time)
{
    Frame *vp = NULL;

    // 检查视频流:
    if(video_st){
        // 检查帧队列是否为空:
        if(frame_queue_nb_remaining(&pictq) == 0){
            return;
        }
        // 获取帧队列中的帧:
        vp = frame_queue_peek(&pictq);

        // 处理帧:
        if(video_refresh_callback_){
            video_refresh_callback_(vp);
        }else{
             std::cout << __FUNCTION__ << " video_refresh_callback_ NULL" << std::endl;
        }

        // 从队列中移除帧:
        frame_queue_next(&pictq);
    }
}

void FFPlayer::AddVideoRefreshCallback(std::function<int (const Frame *)> callback)
{
    video_refresh_callback_ = callback;
}

Decoder::Decoder() 
{
    // 初始化构造函数中的成员变量
    pkt_serial_ = 0;
    finished_ = 0;
    decoder_thread_ = NULL;
    queue_ = NULL;
    avctx_ = NULL;
    av_init_packet(&pkt_);
}

Decoder::~Decoder()
{
    // 在析构函数中进行清理工作
    // decoder_destroy();
}

void Decoder::decoder_init(AVCodecContext *avctx, PacketQueue *queue)
{
    avctx_ = avctx;
    queue_ = queue;
    pkt_serial_ = 0;
    finished_ = 0;
}

int Decoder::decoder_start(enum AVMediaType codec_type, const char *thread_name, void* arg)
{
    int ret = 0;

    //包队列启动 
    packet_queue_start(queue_);

    switch(codec_type)
    {
        case AVMEDIA_TYPE_AUDIO:
            decoder_thread_ = new std::thread(&Decoder::audio_thread, this, arg);
            break;
        case AVMEDIA_TYPE_VIDEO:
            decoder_thread_ = new std::thread(&Decoder::video_thread, this, arg);
            break;
        default:
            ret = -1;
            break;
    }
    
    return ret;
}

void Decoder::decoder_abort(FrameQueue *fq)
{
    packet_queue_abort(queue_);     // 请求退出包队列
    frame_queue_signal(fq);     // 唤醒阻塞的帧队列
    if (decoder_thread_ != NULL && decoder_thread_->joinable()) {
        decoder_thread_->join(); // 等待线程结束
        delete decoder_thread_;
        decoder_thread_ = NULL;
    }
    packet_queue_flush(queue_);  // 清空packet队列，并释放数据
}

void Decoder::decoder_destroy()
{
    av_packet_unref(&pkt_);
    if (avctx_ != NULL) {
        avcodec_free_context(&avctx_);
        avctx_ = NULL;
    }
}

// 返回值-1: 请求退出
//       0: 解码已经结束了，不再有数据可以读取
//       1: 获取到解码后的frame
int Decoder::decoder_decode_frame(AVFrame *frame)
{
    int ret = AVERROR(EAGAIN);

    for (;;) {
        AVPacket pkt;
        do{
            // ... 主要循环逻辑 ...
            if (queue_->abort_request)
                return -1;

            switch (avctx_->codec_type) {
                case AVMEDIA_TYPE_VIDEO:
                    ret = avcodec_receive_frame(avctx_, frame);
                    if(ret >= 0){

                    }else{
                        char errStr[256] = { 0 };
                        av_strerror(ret, errStr, sizeof(errStr));
                        printf("video dec:%s\n", errStr);
                    }
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    ret = avcodec_receive_frame(avctx_, frame);
                    if(ret >= 0){
                        AVRational tb =  (AVRational){1, frame->sample_rate};
                        if (frame->pts != AV_NOPTS_VALUE) {
                            frame->pts = av_rescale_q(frame->pts, avctx_->time_base,tb);
                        }
                    }else{
                        char errStr[256] = { 0 };
                        av_strerror(ret, errStr, sizeof(errStr));
                        printf("audio dec:%s\n", errStr);
                    }
                    break;
                default:
                    break;
            }
            // 1.3. 检查解码是否已经结束，解码结束返回0
            if(ret == AVERROR_EOF){
                printf("avcodec_flush_buffers %s(%d)\n", __FUNCTION__, __LINE__);
                avcodec_flush_buffers(avctx_);
                return 0;
            }
            // 1.4. 正常解码返回1
            if(ret >= 0)
                return 1;
        }while(ret != AVERROR(EAGAIN));// 没帧可读时ret返回EAGIN，需要继续送packet


        if (packet_queue_get(queue_, &pkt, 1, &pkt_serial_) < 0)
            return -1;

        if (avcodec_send_packet(avctx_, &pkt) == AVERROR(EAGAIN)) {
            av_log(avctx_, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
        }
        av_packet_unref(&pkt);
    }
}

int Decoder::get_video_frame(AVFrame *frame)
{
    int got_picture;
    // 1. 获取解码后的视频帧
    if ((got_picture = decoder_decode_frame(frame)) < 0) {
        return -1; // 问题：返回-1意味着要退出解码线程, 所以要分析decoder_decode_frame什么情况下返回-1
    }
    if (got_picture) {
        // 2. 问题：分析获取到的该帧是否要drop掉, 该机制的目的是在放入帧队列前先drop掉过时的视频帧
//        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);
    }

    return got_picture;
}

int Decoder::queue_picture(FrameQueue *fq, AVFrame *src_frame, double pts,
                           double duration, int64_t pos, int serial)
{
    Frame *vp;

    if(!(vp = frame_queue_peek_writable(fq)))
        return -1;      // 请求退出则返回-1

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;

    av_frame_move_ref(vp->frame, src_frame);
    frame_queue_push(fq);
    return 0; // 返回结果
}

int Decoder::audio_thread(void* arg)
{
    std::cout << __FUNCTION__ <<  " into " << std::endl;
    FFPlayer *is = (FFPlayer *)arg;
    int got_frame;
    AVRational tb;
    Frame *af;
    int ret = 0;


    AVFrame *frame = av_frame_alloc();  // 分配解码帧
    if (!frame)
        return AVERROR(ENOMEM);

     // 1. 循环从 `queue_` 中获取音频数据包。问题：音频数据包应该还是未解码的，这部分的操作应该是在decoder_decode_frame中执行？
    do{
        // 2. 对每个数据包调用 `decoder_decode_frame` 进行解码。
        if ((got_frame = decoder_decode_frame(frame)) < 0)
            goto the_end; // < =0 abort
        if(got_frame){
            tb = (AVRational){1, frame->sample_rate};  // 设置为sample_rate为timebase
        
            // 3. 处理解码后的帧（例如，加入到播放队列）。
            if(!(af = frame_queue_peek_writable(&is->sampq)))
                goto the_end;
            af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : (frame->pts * av_q2d(tb));
            af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate});//问题：为何不固定帧间隔，而是每次都去计算呢？因为在不同的音频流中，帧的样本数可能会有所不同，尤其是在可变比特率编码中。

            av_frame_move_ref(af->frame, frame);//问题：这个是不是相当于拷贝动作，就是将frame->af->frame？这个函数实际上是将 frame 的所有权转移到 af->frame，而不是简单地复制数据。这比复制快且高效，因为它避免了不必要的数据复制
            frame_queue_push(&is->sampq);//问题：这个函数只是更新了索引值，似乎没有实际插入链表的什么操作呀，那他是怎么向队列插入一帧数据的呢？这个函数不仅更新索引，它还将帧放入队列中
        }
    }while(ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);

the_end:
    std::cout << __FUNCTION__ <<  " leave " << std::endl;
    av_frame_free(&frame);
    return ret; // 返回结果
}

int Decoder::video_thread(void* arg)
{
    std::cout << __FUNCTION__ <<  " into " << std::endl;
    FFPlayer *is = (FFPlayer *)arg;
    double pts;                 // pts
    double duration;            // 帧持续时间
    Frame *af;
    int ret = 0;

    //1 获取stream timebase
    AVRational tb = is->video_st->time_base;
    //2 获取帧率，以便计算每帧picture的duration
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);

    AVFrame *frame = av_frame_alloc();  // 分配解码帧
    if (!frame)
        return AVERROR(ENOMEM);

    for(;;){
        // 3 获取解码后的视频帧
        ret = get_video_frame(frame);
        if (ret < 0)
            goto the_end;   //问题：解码结束, 什么时候会结束
        if (!ret)           //问题：没有解码得到画面, 什么情况下会得不到解后的帧
            continue;

        // 4 计算帧持续时间和换算pts值为秒
        duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
        // 根据AVStream timebase计算出pts值, 单位为秒
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
        ret = queue_picture(&is->pictq, frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial_);
        // 6 释放frame对应的数据
        av_frame_unref(frame);

        if (ret < 0) // 返回值小于0则退出线程
            goto the_end;
    }

the_end:
    std::cout << __FUNCTION__ <<  " leave " << std::endl;
    av_frame_free(&frame);
    return 0;

    return 0; // 返回结果
}






