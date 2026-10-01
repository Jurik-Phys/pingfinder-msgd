// Begin transportxmpp.h

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "msgtransport.h"
// #include "msgsender.h"
#include <QObject>

class TransportXmpp : public MsgTransport {

    Q_OBJECT

    public:
        TransportXmpp(QObject* parent=nullptr,
                 const QString& config = "/etc/pingfinder/msgd/transport.json");

        void sendMessage(const Message& msg) override;

    private:
        QString m_xmppLogin;
        QString m_xmppServer;
        QString m_xmppPassword;
        QString m_admin_logroom;
        void initTransportXmpp(const QString& config);
};

#endif
// End transportxmpp.h
