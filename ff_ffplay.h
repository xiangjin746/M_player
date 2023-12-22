#ifndef FF_FFPLAY_H
#define FF_FFPLAY_H

#include <thread>
#include <functional>      // 用于 std::function 的头文件
#include "ffmsg_queue.h"
#include "ff_ffplay_def.h"

class Decoder
{
public:
    AVPacket pkt_;
    PacketQueue	*queue_;         // 数据包队列
    AVCodecContext	*avctx_;     // 解码器上下文
    int		pkt_serial_;         // 包序列
    int		finished_;           // =0，解码器处于工作状态；=非0，解码器处于空闲状态
    std::thread *decoder_thread_ = NULL;
    Decoder();
    ~Decoder();
    void decoder_init(AVCodecContext *avctx, PacketQueue *queue);
    // 创建和启动线程
    int decoder_start(enum AVMediaType codec_type, const char *thread_name, void* arg);
    // 停止线程
    void decoder_abort(FrameQueue *fq);
    void decoder_destroy();
    int decoder_decode_frame(AVFrame *frame);
    int get_video_frame(AVFrame *frame);
    int queue_picture(FrameQueue *fq, AVFrame *src_frame, double pts,
                            double duration, int64_t pos, int serial);
    int audio_thread(void* arg);
    int video_thread(void* arg);
};

class FFPlayer
{
public:
    FFPlayer();
    ~FFPlayer();
    int ffp_create();
    void ffp_destroy();
    int ffp_prepare_async_l(char *file_name);

    int ffp_start_l();
    int ffp_stop_l();
    int stream_open( const char *file_name);  
    void stream_close();
    // 打开指定stream对应解码器、创建解码线程、以及初始化对应的输出
    int stream_component_open(int stream_index);
    // 关闭指定stream的解码线程，释放解码器资源
    void stream_component_close(int stream_index);
    
    int read_thread();

    MessageQueue msg_queue_;
    std::thread *read_thread_;

    char *input_filename_;

    int abort_request = 0;

    // 帧队列
    FrameQueue pictq;
    FrameQueue sampq;

    // 包队列
    PacketQueue audioq;
    PacketQueue videoq;

    AVStream *audio_st = NULL;
    AVStream *video_st = NULL;
    
    int audio_stream = -1;
    int video_stream = -1;

    Decoder auddec; //音频解码器
    Decoder viddec; //视频解码器

    int eof = 0;
    AVFormatContext *ic = NULL;
};

inline static void ffp_notify_msg1(FFPlayer *ffp, int what) {
    msg_queue_put_simple3(&ffp->msg_queue_, what, 0, 0);
}

inline static void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1) {
    msg_queue_put_simple3(&ffp->msg_queue_, what, arg1, 0);
}

inline static void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2) {
    msg_queue_put_simple3(&ffp->msg_queue_, what, arg1, arg2);
}

inline static void ffp_notify_msg4(FFPlayer *ffp, int what, int arg1, int arg2, void *obj, int obj_len) {
    msg_queue_put_simple4(&ffp->msg_queue_, what, arg1, arg2, obj, obj_len);
}

inline static void ffp_remove_msg(FFPlayer *ffp, int what) {
    msg_queue_remove(&ffp->msg_queue_, what);
}

#endif // FF_FFPLAY_H
