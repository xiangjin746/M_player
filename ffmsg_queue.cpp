#include "ffmsg_queue.h"
extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
}

#include "ffmsg.h"

// 释放msg的obj资源
void msg_free_res(AVMessage *msg) {
    if (msg->obj && msg->free_l) {
        msg->free_l(msg->obj);
        msg->obj = NULL;
    }
}

// 私有插⼊消息
int msg_queue_put_private(MessageQueue *q, AVMessage *msg)
{
    AVMessage *msg1;
    // 检查 abort_request,如果请求终止队列,直接返回错误。
    if(q->abort_request){
        return -1;
    }

    // 使用一个消息缓存池recycle_msg来重复利用消息结构体内存,而不是每次都malloc。这能提高效率。
    if(q->recycle_msg){
        q->recycle_count++;
        msg1 = q->recycle_msg;
        q->recycle_msg = q->recycle_msg->next;
    }else{
        // 优先从缓存池获取,如果池空则才malloc新结构体。
        q->alloc_count++;
        msg1 = (AVMessage *)av_malloc(sizeof(AVMessage));
    }

    // 拷贝输入消息内容到队列中的消息,而不是直接把指针入队。
    // 使用*msg1 = *msg来拷贝结构体。
    *msg1 = *msg;
    msg1->next = NULL;

    // 插入消息
    if(!q->first_msg){
        q->first_msg = msg1;
    }else{
        q->last_msg->next = msg1;
    }
    q->last_msg = msg1;

    // 在插入消息后发送条件变量信号,通知在cond上等待的线程。
    // 这usually是消费者线程,插入新消息后可以唤醒它。
    // 更新其他队列统计信息,如分配次数等。
    q->nb_messages++;
    SDL_CondSignal(q->cond);
}

// 插⼊消息
int msg_queue_put(MessageQueue *q, AVMessage *msg) 
{
    int ret;
    SDL_LockMutex(q->mutex);
    ret = msg_queue_put_private(q, msg);
    SDL_UnlockMutex(q->mutex);
    return ret;
}

// 初始化消息
void msg_init_msg(AVMessage *msg)
{
    memset(msg,0,sizeof(AVMessage));
}

// 插⼊简单消息，只带消息类型，不带参数
void msg_queue_put_simple1(MessageQueue *q, int what)
{
    AVMessage msg1;
    msg1.what = what;
    msg_queue_put(q,&msg1);
    
}

// 插⼊简单消息，只带消息类型，只带1个参数
void msg_queue_put_simple2(MessageQueue *q, int what, int arg1)
{
    AVMessage msg1;
    msg1.what = what;
    msg1.arg1 = arg1;
    msg_queue_put(q,&msg1);
}

// 插⼊简单消息，只带消息类型，带2个参数
void msg_queue_put_simple3(MessageQueue *q, int what, int arg1, int arg2)
{
    AVMessage msg1;
    msg1.what = what;
    msg1.arg1 = arg1;
    msg1.arg2 = arg2;
    msg_queue_put(q,&msg1);
}

// 释放msg的obj资源
void msg_obj_free_l(void *obj)
{
    av_free(obj);
}

// 插⼊消息，带消息类型，带2个参数，带obj
void msg_queue_put_simple4(MessageQueue *q, int what, int arg1, int arg2, void *obj, int obj_len)
{
    AVMessage msg1;
    msg1.what = what;
    msg1.arg1 = arg1;
    msg1.arg2 = arg2;
    msg1.obj = av_malloc(obj_len);
    memcpy(msg1.obj,obj,obj_len);
    msg1.free_l = msg_obj_free_l;
    msg_queue_put(q,&msg1);
}

// 消息队列初始化
void msg_queue_init(MessageQueue *q)
{
    memset(q,0,sizeof(MessageQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
    q->abort_request = 1;
}

// 消息队列flush，清空所有的消息
void msg_queue_flush(MessageQueue *q)
{
    AVMessage *msg, *msg1;

    SDL_LockMutex(q->mutex);
    for(msg = q->first_msg; msg != NULL; msg = msg1){
        msg->next = q->recycle_msg;
        q->recycle_msg = msg;

        msg1 = msg->next;
    }
    q->last_msg = NULL;
    q->first_msg = NULL;
    q->nb_messages = 0;
    SDL_UnlockMutex(q->mutex);
}

// 消息销毁
void msg_queue_destroy(MessageQueue *q)
{
    msg_queue_flush(q);

    SDL_LockMutex(q->mutex);
    while(q->recycle_msg){
        AVMessage *msg = q->recycle_msg;
        if(msg){
            q->recycle_msg = msg->next;
        }
        msg_free_res(msg);
        av_freep(&msg);
    }
    SDL_UnlockMutex(q->mutex);

    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);

}

// 消息队列终⽌
void msg_queue_abort(MessageQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 1;
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
}

// 启⽤消息队列
void msg_queue_start(MessageQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;

    AVMessage msg;
    msg_init_msg(&msg);
    msg.what = FFP_MSG_FLUSH;
    msg_queue_put(q,&msg);
    SDL_UnlockMutex(q->mutex);
    
}

// 读取消息
/* return < 0 if aborted, 0 if no msg and > 0 if msg. */
int msg_queue_get(MessageQueue *q, AVMessage *msg, int block)
{
    AVMessage *msg1;
    int ret;

    SDL_LockMutex(q->mutex);
    for(;;){
        if(q->abort_request){
            return -1;
        }
        msg1 = q->first_msg;
        if(msg1){
            // 从队列中移除消息:
            q->first_msg  = msg1->next;
            // 检查并更新队列尾部指针:
            if(!q->first_msg)q->last_msg = NULL;
            // 更新队列的消息数量:
            q->nb_messages --;
            // 将消息复制到输出参数:
            *msg = *msg1;
            // 处理消息对象:
            msg->obj = NULL;
            // 消息对象的回收利用:
            msg1->next = q->recycle_msg;
            q->recycle_msg = msg1;
            // 设置函数返回值:
            ret = 1;
            // 退出循环:
            break;
        }else if(!block){
            ret = 0;//如果 block 为 false，即表示函数应该在非阻塞模式下运行。
            break;
        }else{
            SDL_CondWait(q->cond, q->mutex);//阻塞模式下的等待:
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

// 消息删除 把队列⾥同⼀消息类型的消息全删除掉
void msg_queue_remove(MessageQueue *q, int what)
{
    AVMessage **p_msg, *msg, *last_msg;

    SDL_LockMutex(q->mutex);
    last_msg = q->first_msg;
    if (!q->abort_request && q->first_msg){
        p_msg = &q->first_msg;
        while(*p_msg){
            msg = *p_msg;
            if(msg->what == what){
                *p_msg = msg->next;
                msg_free_res(msg);
                msg->next = q->recycle_msg;     // 消息体回收
                q->recycle_msg = msg;
                q->nb_messages--;
            }else{
                last_msg = msg;
                p_msg = &msg->next;
            }

            // 更新队列的尾部指针:
            if(q->first_msg){
                q->last_msg = last_msg;
            }else{
                q->last_msg = NULL;
            }
        }
    }
    SDL_UnlockMutex(q->mutex);
}

