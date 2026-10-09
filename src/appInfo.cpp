// Begin appInfo.cpp

#include <QDebug>
#include "appInfo.h"

#ifndef APP_VERSION
#define APP_VERSION "?.?.?"
#endif

#ifndef APP_GIT_HASH
#define APP_GIT_HASH "unknown"
#endif

#ifndef APP_GIT_DIRTY
#define APP_GIT_DIRTY "-?????"
#endif

#ifndef APP_GIT_BRANCH
#define APP_GIT_BRANCH "unknown"
#endif

#ifndef APP_BASE_NAME
#define APP_BASE_NAME "PingFinder Message Daemon"
#endif

#ifndef APP_FULL_NAME
#define APP_FULL_NAME "Utility for scheduling and sending SMS messages"
#endif

#ifndef APP_AUTHOR_NAME
#define APP_AUTHOR_NAME "Yury Ovsyannikov"
#endif

#ifndef APP_AUTHOR_MAIL
#define APP_AUTHOR_MAIL "jurik.phys@gmail.com"
#endif

#ifndef APP_AUTHOR_SITE
#define APP_AUTHOR_SITE "https://jurik-phys.net.ru"
#endif

QString AppInfo::appBaseName(){
    return QString(APP_BASE_NAME);
}

QString AppInfo::appFullName(){
    return QString(APP_FULL_NAME);
}

QString AppInfo::appBaseVersion(){
    return QString(APP_VERSION);
}

QString AppInfo::appFullVersion(){
    QString res;
    if (QString(APP_GIT_HASH).isEmpty()){
        res = QString("v%1").arg(APP_VERSION);
    }
    else {
        res = QString("v%1 (git-%2%3, %4)").arg(APP_VERSION)
                                           .arg(APP_GIT_HASH)
                                           .arg(APP_GIT_DIRTY)
                                           .arg(APP_GIT_BRANCH);
    }

    return res;
}

QString AppInfo::appAuthorName(){
    return QString(APP_AUTHOR_NAME);
}

QString AppInfo::appAuthorMail(){
    return QString(APP_AUTHOR_MAIL);
}

QString AppInfo::appAuthorSite(){
    return QString(APP_AUTHOR_SITE);
}

void AppInfo::print(){
    QString text(R"(%1 is a utility for scheduling
and sending SMS messages to PingFinder service clients

Version:     %2
Author:
    Name:    %3
    Email:   %4
    Website: %5
License:     GNU General Public License v3.0 (GPL-3.0))");

    text = text.arg(AppInfo::appBaseName());
    text = text.arg(AppInfo::appFullVersion());
    text = text.arg(AppInfo::appAuthorName());
    text = text.arg(AppInfo::appAuthorMail());
    text = text.arg(AppInfo::appAuthorSite());

    qInfo().noquote() << text;
}

// End appInfo.cpp
