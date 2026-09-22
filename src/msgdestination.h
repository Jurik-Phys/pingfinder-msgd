// Begin msgdestination.h

#ifndef MSGDESTINATION_H
#define MSGDESTINATION_H

#include <QString>

enum class DestinationType {
    MobilePhone,
    XmppRoom,
    XmppUser
};

struct MsgDestination{
    DestinationType type;
    QString address;
};

struct Message{
    QString uuid;
    MsgDestination destination;
    QString text;
};

#endif
// End msgdestination.h
