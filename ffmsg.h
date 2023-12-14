#ifndef FFMSG_H
#define FFMSG_H

#define FFP_MSG_FLUSH 0
#define FFP_MSG_ERROR                       100     /*出现错误 arg1 = error */
#define FFP_MSG_PREPARED                    200     // 准备好了
#define FFP_MSG_COMPLETED                   300     // 播放完成
#define FFP_MSG_VIDEO_SIZE_CHANGED          400     /* 视频大小发送变化 arg1 = width, arg2 = height */
#define FFP_MSG_SAR_CHANGED                 401     /* arg1 = sar.num, arg2 = sar.den */
#define FFP_MSG_VIDEO_RENDERING_START       402     //开始画面渲染
#define FFP_MSG_AUDIO_RENDERING_START       403     //开始声音输出
#define FFP_MSG_VIDEO_ROTATION_CHANGED      404     /* arg1 = degree */
#define FFP_MSG_AUDIO_DECODED_START         405     // 开始音频解码
#define FFP_MSG_VIDEO_DECODED_START         406     // 开始视频解码
#define FFP_MSG_OPEN_INPUT                  407     // read_thread 调用了 avformat_open_input
#define FFP_MSG_FIND_STREAM_INFO            408     // read_thread 调用了 avformat_find_stream_info
#define FFP_MSG_COMPONENT_OPEN              409     // read_thread 调用了 stream_component_open


#define FFP_REQ_START                       20001       // 核心播放器已经准备好了，请求ui模块调用start
#define FFP_REQ_PAUSE                       20002       // ui模块请求暂停
#define FFP_REQ_SEEK                        20003       // ui模块请求seek位置

#endif // FFMSG_H
