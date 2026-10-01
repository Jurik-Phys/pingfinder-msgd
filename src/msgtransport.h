// Begin msgtransport.h
//
#ifndef MSGTRANSPORT_H
#define MSGTRANSPORT_H

#include <QString>
#include <QObject>
#include "msgdestination.h"

// *** Теория *** //
// В Qt нельзя так смешивать "чистый интерфейс" и QObject-иерархию.
// Рекомендуется cделать MsgTransport QObject-базой, которая и будет выполнять
// роль интерфейса. От него также будт наследваться конкретные реализации
// smsTransport, xmppTransport, mailTransport и т.д.
// Таким образом отправитель отделён от деталей реализации отправки сообщений
class MsgTransport : public QObject {

    Q_OBJECT

    public:
        MsgTransport(QObject* parent = nullptr);
        // "= default" - генерация стандартной реализации деструктора
        ~MsgTransport() override = default;

        // "= 0" - чисто виртуальная функция, нет реализаций в базовом классе
        virtual void sendMessage(const Message& ) = 0;

    signals:
        // По факту, в msgId будет передаваться taskUuid
        void msgSent(const QString& msgUuid);
        void msgFail(const QString& msgUuid);
};

#endif
// End msgtransport.h
