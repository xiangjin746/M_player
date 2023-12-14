#include "displaywind.h"
#include "ui_displaywind.h"

DisplayWind::DisplayWind(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DisplayWind)
{
    ui->setupUi(this);
}

DisplayWind::~DisplayWind()
{
    delete ui;
}
