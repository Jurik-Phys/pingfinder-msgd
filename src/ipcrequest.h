// Begin ipcrequest.h

#ifndef IPCREQUEST_H
#define IPCREQUEST_H

#include <QString>
#include <QUuid>
#include <QStringList>

struct MessagePushRequest {
    QString     requestId;
    QString     messageType;
    QString     message;
    QString     identType;
    QStringList idents;
    QString     providedBy;
};

#endif
// End ipcrequest.h
