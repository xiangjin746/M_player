#ifndef PLAYLISTWIND_H
#define PLAYLISTWIND_H

#include <QWidget>

namespace Ui {
class PlayListWind;
}

class PlayListWind : public QWidget
{
    Q_OBJECT

public:
    explicit PlayListWind(QWidget *parent = 0);
    ~PlayListWind();

private:
    Ui::PlayListWind *ui;
};

#endif // PLAYLISTWIND_H
