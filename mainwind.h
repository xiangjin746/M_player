#ifndef MAINWIND_H
#define MAINWIND_H

#include <QMainWindow>
#include "ijkmediaplayer.h"

namespace Ui {
class MainWind;
}

class MainWind : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWind(QWidget *parent = 0);
    ~MainWind();

    int InitSignalsAndSlots();
    int message_loop(void *arg);
    void OnPlayOrPause();

private:
    Ui::MainWind *ui;
    IjkMediaPlayer *mp_ = NULL;
};

#endif // MAINWIND_H
