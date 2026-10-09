#include <QCoreApplication>
#include "src/daemon.h"
#include "src/appCmd.h"
#include "src/appInfo.h"

int main(int argc, char** argv){

    QCoreApplication app(argc, argv);

    QStringList args = QCoreApplication::arguments();

    // Print application version & exit
    if (args.contains("-v") || args.contains("--version"))
    {
        AppInfo::print();
        return 0;
    }

    // Display help & exit
    if (args.contains("-h") || args.contains("--help") || args.size() > 1)
    {
        printAppHelp();
        return 0;
    }

    Daemon* daemon = new Daemon(&app);

    return app.exec();
}
