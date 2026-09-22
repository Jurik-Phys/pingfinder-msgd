#include <QCoreApplication>
#include "src/daemon.h"

int main(int argc, char** argv){

    QCoreApplication app(argc, argv);

    Daemon* daemon = new Daemon(&app);

    return app.exec();
}
