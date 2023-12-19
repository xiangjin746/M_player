#include "ctrlbar.h"
#include "ui_ctrlbar.h"
#include <QDebug>
CtrlBar::CtrlBar(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CtrlBar)
{
    ui->setupUi(this);

    // 设置播放图标
    QIcon icon_play(":/ctrl/icon/play.png");
    ui->playOrPauseBtn->setIcon(icon_play);

    // 设置停止图标
    QIcon icon_stop(":/ctrl/icon/stop.png");
    ui->stopBtn->setIcon(icon_stop);
}

CtrlBar::~CtrlBar()
{
    delete ui;
}

void CtrlBar::on_playOrPauseBtn_clicked()
{
    qDebug() << "on_playOrPauseBtn_clicked";
    emit SigPlayOrPause();      // 发送信号，这里只发送信号具体是播放还是暂停由播放逻辑判断。
}

void CtrlBar::on_stopBtn_clicked()
{
    qDebug() << "on_stopBtn_clicked";
    emit SigStop();
}
