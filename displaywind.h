#ifndef DISPLAYWIND_H
#define DISPLAYWIND_H

#include <QWidget>
#include <QMutex>
#include "ijkmediaplayer.h"
#include "imagescaler.h"



namespace Ui {
class DisplayWind;
}

class DisplayWind : public QWidget
{
    Q_OBJECT

public:
    explicit DisplayWind(QWidget *parent = 0);
    ~DisplayWind();
    int Draw(const Frame *frame);
protected:
    // 这里不要重载event事件，会导致paintEvent不被触发
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *event);
private:
    Ui::DisplayWind *ui;

    int m_nLastFrameWidth; ///< 记录视频宽高
    int m_nLastFrameHeight;
    bool is_display_size_change_ = false;

    int x_ = 0; //  起始位置
    int y_ = 0;
    int video_width = 0;
    int video_height = 0;
    int img_width = 0;
    int img_height = 0;
    QImage img;
    VideoFrame dst_video_frame_;
    QMutex m_mutex;
    ImageScaler *img_scaler_ = NULL;
};

#endif // DISPLAYWIND_H
