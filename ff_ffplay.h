#ifndef FF_FFPLAY_H
#define FF_FFPLAY_H

#include <thread>
#include <functional>      // 用于 std::function 的头文件
#include "ffmsg_queue.h"
#include "ff_ffplay_def.h"

class FFPlayer
{
public:
    FFPlayer();
    ~FFPlayer();
    int ffp_create();
    int ffp_prepare_async_l(char *file_name);
    int stream_open( const char *file_name);  
    void stream_close();
    int read_thread();

    MessageQueue msg_queue_;
    std::thread *read_thread_;

    char *input_filename_;

    int abort_request = 0;



private:
    
    // std::function<int(const Frame *)> video_refresh_callback_ = NULL;
    // /* ffplay context */
    // VideoState *is;
    // const char* wanted_stream_spec[AVMEDIA_TYPE_NB];

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
