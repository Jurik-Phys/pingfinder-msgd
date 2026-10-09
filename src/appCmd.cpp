// Begin appCmd.cpp

#include "appCmd.h"

void printAppHelp(){

    qDebug() << "PingFinder Message Daemon is a utility for scheduling\n"
                "and sending SMS messages to PingFinder service clients";
    qDebug() << "";

    qDebug() << "Usage:";
    qDebug() << "  pingfinder-msgd [options]";
    qDebug() << "";

    qDebug() << "Options:";
    qDebug() << "  -h, --help              Show this help message";
    qDebug() << "  -v, --version           Show the version";
}

// End cmd.cpp
