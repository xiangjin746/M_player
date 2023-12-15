#include "ff_ffplay_def.h"

static AVPacket flush_pkt;
static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList *pkt1;
    int ret;
    // 1.检查队列的中止请求:
    if(q->abort_request){
        return -1;// 如果队列已经被标记为中止，应立即返回错误代码。
    }
    
    // 2.为新节点分配内存:
    pkt1 = (MyAVPacketList *)av_malloc(sizeof(MyAVPacketList));// 使用 av_malloc 来分配一个 MyAVPacketList 结构体的内存。
    if(!pkt1){
        return -1;// 如果内存分配失败，返回错误代码。
    }
    
    // 3.填充节点数据:
    pkt1->pkt = *pkt;// 将传入的 AVPacket 复制到新节点的 pkt 字段。注意这里是浅拷贝，所以 AVPacket 中的数据不会被复制。
    pkt1->next = NULL;
   
    // 4.更新队列状态:
    if(pkt == &flush_pkt){
        q->serial++;// 如果传入的是特殊的 flush_pkt，则递增队列的 serial。
    }
    pkt1->serial = q->serial;// 设置新节点的 serial 字段为队列当前的 serial。

    // 将新节点加入到队列的尾部。如果队列为空，则新节点同时也是队列的头部。
    if(!q->last_pkt){
        q->first_pkt = pkt1;
    }else{
        q->last_pkt->next = pkt1;
    }
    q->last_pkt = pkt1;
    // 更新队列的包数量、总大小和总持续时间。
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    q->duration += pkt1->pkt.duration;
    
    
    // 5.通知其他等待的线程:
    SDL_CondSignal(q->cond);// 使用 SDL_CondSignal 来通知其他可能在等待队列数据的线程。

    return ret;
}

// 向packet队列中放入packet
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    int ret;

    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q,pkt);
    SDL_UnlockMutex(q->mutex);

    return ret;
}

// 这个函数用于向队列中放入一个空的数据包（null packet）
int packet_queue_put_nullpacket(PacketQueue *q, int stream_index) {
    AVPacket pkt1, *pkt = &pkt1;
    av_init_packet(pkt);
    pkt->data = NULL;
    pkt->size = 0;
    pkt->stream_index = stream_index;
    return packet_queue_put(q, pkt);
}

// 初始化packet队列
int packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    q->abort_request = 1;
    return 0;
}

// 取出packet队列中的一个packet
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial) {
    MyAVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);
    // 1.循环直到获取数据包:
    for(;;){ // 使用一个无限循环来尝试从队列中获取数据包。这个循环会持续，直到找到数据包或者满足退出条件。
        // 2.处理中止请求:
        if(q->abort_request){// 如果队列被标记为中止（abort_request 为真），则退出循环，并返回一个负值表示中止。
            ret = -1;
            break;
        }

        // 3.检查队列中的数据包:
        pkt1 = q->first_pkt;
        if(pkt1){//  如果队列中有数据包（first_pkt 不为 NULL），则从队列头部取出一个数据包。
            q->first_pkt = pkt1->next;
            if(!q->first_pkt){
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            q->duration -= pkt1->pkt.duration;
            *pkt = pkt1->pkt;
            if(serial){
                *serial = pkt1->serial;
            }
            av_free(pkt1);
            ret = 1;
            break;
        }else if(!block){ // 如果队列为空，则根据 block 参数决定是退出循环（非阻塞），还是等待（阻塞）。
            ret = 0;
            break;
        }else{
            SDL_CondWait(q->cond,q->mutex); // 在阻塞模式下，使用 SDL_CondWait 等待条件变量，直到队列中有数据包可用。
        }
    }

    SDL_UnlockMutex(q->mutex);

    return ret;
}

// 这个函数负责清空队列中的所有数据包。
void packet_queue_flush(PacketQueue *q) {
    MyAVPacketList *pkt,*pkt1;

    SDL_LockMutex(q->mutex);
    // 遍历队列，释放每个节点的 AVPacket 和节点本身。
    for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1){
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_free(pkt);
    }
    // 重置队列状态（头尾指针、包数量、总大小和总持续时间）。
    q->first_pkt = NULL;
    q->last_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;

    SDL_UnlockMutex(q->mutex);
}

// 此函数用于中止队列的操作。
void packet_queue_abort(PacketQueue *q) {
    SDL_LockMutex(q->mutex);
    q->abort_request = 1;
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
}

// 这个函数用于销毁队列，释放其使用的所有资源。
void packet_queue_destroy(PacketQueue *q) {
    packet_queue_flush(q);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

// 这个函数用于启动队列，主要是清除中止标志，并放入一个特殊的 flush_pkt 数据包以标记队列的开始。
void packet_queue_start(PacketQueue *q) {
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt); // 特殊的 flush_pkt
    SDL_UnlockMutex(q->mutex);
}

/* 帧队列部分*/
// 释放frame数据
void frame_queue_unref_item(Frame *vp){
    av_frame_unref(vp->frame);
}

// 这个函数负责初始化帧队列。需要为每个 Frame 分配内存，并设置队列的初始状态。
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size) {
    // 初始化 FrameQueue
    int i;
    memset(f, 0, sizeof(FrameQueue));  // 清零 FrameQueue 结构体

    // 创建互斥锁
    if (!(f->mutex = SDL_CreateMutex())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    // 创建条件变量
    if (!(f->cond = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        SDL_DestroyMutex(f->mutex);  // 创建失败时，清理已创建的互斥锁
        return AVERROR(ENOMEM);
    }

    f->pktq = pktq;
    f->max_size = FFMIN(max_size,FRAME_QUEUE_SIZE);

    for(i = 0; i < f->max_size;i++){
        if(!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    }

    return 0;

}

// 这个函数用于销毁帧队列，释放所有分配的资源。
void frame_queue_destory(FrameQueue *f) {
    // 遍历并释放所有 Frame 的资源
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame *vp = &f->queue[i];
        frame_queue_unref_item(vp);  // 释放vp->frame中的数据缓冲区的引用
        av_frame_free(&vp->frame);   // 释放vp->frame对象
    }

    // 销毁互斥锁和条件变量
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);

}

// 用于在队列状态发生变化时发出信号，通常用于唤醒等待的线程。
void frame_queue_signal(FrameQueue *f) {
    SDL_LockMutex(f->mutex);  // 加锁以确保线程安全
    SDL_CondSignal(f->cond);  // 发送信号通知等待的线程
    SDL_UnlockMutex(f->mutex); // 解锁
}

// 更新写指针
void frame_queue_push(FrameQueue *f){
    if (++f->windex == f->max_size)
        f->windex = 0;
    SDL_LockMutex(f->mutex);
    f->size++;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/* 释放当前frame，并更新读索引rindex */
void frame_queue_next(FrameQueue *f){
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    SDL_LockMutex(f->mutex);
    f->size--;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

// 这个函数用于获取一个可写入的帧。当队列已满时，它会等待直到有可用的空间。
Frame *frame_queue_peek_writable(FrameQueue *f)
{
    /* wait until we have space to put a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size &&
           !f->pktq->abort_request) {	/* 检查是否需要退出 */
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)			 /* 检查是不是要退出 */
        return NULL;

    return &f->queue[f->windex];
}

// 这个函数用于获取一个可读取的帧。当队列为空时，它会等待直到有新的帧可读。
Frame *frame_queue_peek_readable(FrameQueue *f) {
    SDL_LockMutex(f->mutex);
    while(f->size <= 0 && !f->pktq->abort_request){
        SDL_CondWait(f->cond,f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if(f->pktq->abort_request) {
        return NULL;
    }

    return &f->queue[f->rindex % f->max_size];
}

/* 获取队列当前Frame, 在调用该函数前先调用frame_queue_nb_remaining确保有frame可读 */
Frame *frame_queue_peek(FrameQueue *f){
    return &f->queue[(f->rindex) % f->max_size];
}

/* 获取当前Frame的下一Frame, 此时要确保queue里面至少有2个Frame */
// 不管你什么时候调用，返回来肯定不是 NULL
Frame *frame_queue_peek_next(FrameQueue *f) {
    return &f->queue[(f->rindex + 1) % f->max_size];
}

/* 获取last Frame：
 */
Frame *frame_queue_peek_last(FrameQueue *f) {
    return &f->queue[f->rindex];
}

// 此函数返回队列中未显示的帧数量。
int frame_queue_nb_remaining(FrameQueue *f) {
    return f->size;  // 返回队列中未处理的帧数
}

// 此函数返回最后显示帧的位置。这可能涉及到帧的特定元数据，如播放时间戳或帧序号。
int64_t frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
//    if (f->rindex_shown && fp->serial == f->pktq->serial)
//   if(fp)
//        return fp->pos;
//   else
        return -1;
}








