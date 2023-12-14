#include "playlistwind.h"
#include "ui_playlistwind.h"

PlayListWind::PlayListWind(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PlayListWind)
{
    ui->setupUi(this);
}

PlayListWind::~PlayListWind()
{
    delete ui;
}
