#include "mainwind.h"
#include <QApplication>

#undef main
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWind w;
    w.show();

    return a.exec();
}
