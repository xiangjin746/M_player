#ifndef CTRLBAR_H
#define CTRLBAR_H

#include <QWidget>

namespace Ui {
class CtrlBar;
}

class CtrlBar : public QWidget
{
    Q_OBJECT

public:
    explicit CtrlBar(QWidget *parent = 0);
    ~CtrlBar();
signals:
    void SigPlayOrPause();      // 先实现这个

private slots:
    void on_playOrPauseBtn_clicked();

    void on_stopBtn_clicked();

private:
    Ui::CtrlBar *ui;
};

#endif // CTRLBAR_H
