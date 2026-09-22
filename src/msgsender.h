// Begin msgsender.h

#ifndef MSGSENDER_H
#define MSGSENDER_H

#include <QObject>
#include <QVector>
#include "msgtransport.h"
#include "msgdestination.h"

// Класс отправитель получает набор транспортов (sms, xmmp),
// отправляет сообщение с указанием адресата и текста сообщения
class MsgSender : public QObject {

    Q_OBJECT

    public:
        MsgSender(QObject* parent = nullptr);
        ~MsgSender();

        void addTransport(MsgTransport* transport);
        void send(const Message& msg);
    private:
        QVector<MsgTransport*> m_transports;
};

#endif
// End msgsender.h
