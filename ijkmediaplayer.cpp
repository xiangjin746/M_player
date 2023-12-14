#include "ijkmediaplayer.h"
#include <iostream>
#include <string.h>
#include "ffmsg.h"

IjkMediaPlayer::IjkMediaPlayer()
{
    std::cout << " IjkMediaPlayer()\n ";
}

IjkMediaPlayer::~IjkMediaPlayer()
{
     std::cout << "~IjkMediaPlayer()\n ";
}

#include "IjkMediaPlayer.h" // 包含你的类的头文件，可能是 IjkMediaPlayer.h


// 创建 IJKMP 实例，参数是消息循环函数
int IjkMediaPlayer::ijkmp_create(std::function<int(void *)> msg_loop) {

    int ret = 0;
    // 创建 FFPlayer
    ffplayer_ = new FFPlayer(); // 假设有一个 FFPlayer 类

    if (!ffplayer_) {
        
        std::cout << "new FFPlayer failed" << std::endl;
        return -1; // 返回错误码，表示创建失败
    }

    msg_loop_ = msg_loop;

    ret = ffplayer_->ffp_create();
    if(ret < 0){
        return -1;
    }

    return 0;
}

// 销毁 IJKMP 实例
int IjkMediaPlayer::ijkmp_destroy() {
    return 0;
}

// 设置要播放的 URL
int IjkMediaPlayer::ijkmp_set_data_source(const char *url) {
    if(!url){
        return -1;
    }
    data_source_ = strdup(url);
    return 0;
}

// 准备播放
int IjkMediaPlayer::ijkmp_prepare_async() {
    // 判断mp的状态
    // 正在准备中
    mp_state_ = MP_STATE_ASYNC_PREPARING;

    // 启用消息队列
    msg_queue_start(&ffplayer_->msg_queue_);

    // 创建循环线程
    msg_thread_ = new std::thread(&IjkMediaPlayer::ijkmp_msg_loop,this,this);

    // 调用ffplayer
    int ret = ffplayer_->ffp_prepare_async_l(data_source_);
    if(ret < 0){
        mp_state_ = MP_STATE_ERROR;
        return -1;
    }
    return 0;
}

// 触发播放
int IjkMediaPlayer::ijkmp_start() {
    ffp_notify_msg1(ffplayer_,FFP_REQ_START);
}

int IjkMediaPlayer::ijkmp_msg_loop(void *arg)
{
    msg_loop_(arg);
    return 0;
}

// 停止
int IjkMediaPlayer::ijkmp_stop() {
    // 实现代码
    // 返回适当的结果
}

// 暂停
int IjkMediaPlayer::ijkmp_pause() {
    // 实现代码
    // 返回适当的结果
}

// Seek 到指定位置
int IjkMediaPlayer::ijkmp_seek_to(long msec) {
    // 实现代码
    // 返回适当的结果
}

// 获取播放状态
int IjkMediaPlayer::ijkmp_get_state() {
    // 实现代码
    // 返回适当的结果
}

// 是否播放中
bool IjkMediaPlayer::ijkmp_is_playing() {
    // 实现代码
    // 返回适当的结果
}

// 获取当前播放位置
long IjkMediaPlayer::ijkmp_get_current_position() {
    // 实现代码
    // 返回适当的结果
}

// 获取总长度
long IjkMediaPlayer::ijkmp_get_duration() {
    // 实现代码
    // 返回适当的结果
}

// 获取已播放长度
long IjkMediaPlayer::ijkmp_get_playable_duration() {
    // 实现代码
    // 返回适当的结果
}

// 设置循环播放
void IjkMediaPlayer::ijkmp_set_loop(int loop) {
    // 实现代码
}

// 获取是否循环播放
int IjkMediaPlayer::ijkmp_get_loop() {
    // 实现代码
    // 返回适当的结果
}

// 读取消息
int IjkMediaPlayer::ijkmp_get_msg(AVMessage *msg, int block) 
{
    while (1)
    {
        int continue_wait_next_msg = 0;
        int retval = msg_queue_get(&ffplayer_->msg_queue_,msg,block);
        if(retval <= 0){
            return retval;
        }

        switch (msg->what)
        {
            case FFP_MSG_PREPARED:
                std::cout << __FUNCTION__ << " FFP_MSG_PREPARED" << std::endl;
                break;
            case FFP_REQ_START:
                std::cout << __FUNCTION__ << " FFP_REQ_START" << std::endl;
                continue_wait_next_msg = 1;
                break;
            
            default:
                break;
        }
        if(continue_wait_next_msg){
            msg_free_res(msg);
            continue;
        }
        return retval;
    }
    return -1;
}

// 设置音量
void IjkMediaPlayer::ijkmp_set_playback_volume(float volume) {
    // 实现代码
}
