// Begin client.h

#ifndef CLIENT_H
#define CLIENT_H

#include <QString>

struct Client {
    int id;
    QString nickname;
    QString firstName;
    QString middleName;
    QString lastName;
    QString phoneNumber;
    int reminderDay;
    int payAmount;
    bool enabled;
};

#endif
// End client.h
