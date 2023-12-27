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
        if(win_aspect_ratio > video_aspect_ratio) {
            //此时应该是调整x的起始位置，以高度为基准
            img_height = win_height;
            if(img_height %2 != 0) {
                img_height -= 1;
            }

            img_width = img_height*video_aspect_ratio;
            if(img_width %2 != 0) {
                img_width -= 1;
            }
            y_ = 0;
            x_ = (win_width - img_width) / 2;
        } else {
            //此时应该是调整y的起始位置，以宽度为基准
            img_width = win_width;
            if(img_width %2 != 0) {
                img_width -= 1;
            }
            img_height = img_width / video_aspect_ratio;
            if(img_height %2 != 0) {
                img_height -= 1;
            }
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
