#include "displaywind.h"
#include "ui_displaywind.h"

#include <QPainter>
DisplayWind::DisplayWind(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DisplayWind)
{
    ui->setupUi(this);
}

DisplayWind::~DisplayWind()
{
    delete ui;
    if(dst_video_frame_.data[0])
        free(dst_video_frame_.data[0]);
    if(img_scaler_) {
        delete img_scaler_;
        img_scaler_ = NULL;
    }
}

int DisplayWind::Draw(const Frame *frame)
{
    QMutexLocker locker(&m_mutex);

    if(!img_scaler_) {
        int win_width = width();
        int win_height = height();
        video_width = frame->width;
        video_height = frame->height;
        img_scaler_ = new ImageScaler();
        double video_aspect_ratio = frame->width *1.0 / frame->height;
        double win_aspect_ratio = win_width*1.0 / win_height;

        // 根据窗口和视频的宽高比，调整图像大小并保证尺寸为4的倍数
        if (win_aspect_ratio > video_aspect_ratio) {
            // 窗口宽高比较大，以视频高度为基准缩放
            img_height = (win_height / 4) * 4; // 确保为4的倍数
            img_width = ((int)(img_height * video_aspect_ratio) / 4) * 4; // 确保为4的倍数
            y_ = 0;
            x_ = (win_width - img_width) / 2;
        } else {
            // 视频宽高比较大，以窗口宽度为基准缩放
            img_width = (win_width / 4) * 4; // 确保为4的倍数
            img_height = ((int)(img_width / video_aspect_ratio) / 4) * 4; // 确保为4的倍数
            x_ = 0;
            y_ = (win_height - img_height) / 2;
        }
        img_scaler_->Init(video_width, video_height, frame->format,
                          img_width, img_height, AV_PIX_FMT_RGB24);
        memset(&dst_video_frame_, 0, sizeof(VideoFrame));
        dst_video_frame_.width = img_width;
        dst_video_frame_.height = img_height;
        dst_video_frame_.format = AV_PIX_FMT_RGB24;
        dst_video_frame_.data[0] = (uint8_t*)malloc(img_width * img_height * 3);
        dst_video_frame_.linesize[0] = img_width * 3; // 每行的字节数
    }
    img_scaler_->Scale3(frame, &dst_video_frame_);

    QImage imageTmp =  QImage((uint8_t *)dst_video_frame_.data[0],
            img_width, img_height, QImage::Format_RGB888);
    img = imageTmp.copy(0, 0, img_width, img_height);

    update();
//    repaint();
    return 0;
}


void DisplayWind::paintEvent(QPaintEvent *)
{
    QMutexLocker locker(&m_mutex);
    if (img.isNull()) {
        return;
    }
    QPainter painter(this);

    //    //    p.translate(X, Y);
    //    //    p.drawImage(QRect(0, 0, W, H), img);
    QRect rect = QRect(x_, y_, img.width(), img.height());
    painter.drawImage(rect, img);
}

void DisplayWind::resizeEvent(QResizeEvent *event)
{

}
