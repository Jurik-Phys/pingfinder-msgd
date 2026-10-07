// Begin ipcserver.h

#ifndef IPCSERVER_H
#define IPCSERVER_H

#include <QObject>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include "ipcrequest.h"
#include "schedule.h"

class IpcServer : public QObject{

    Q_OBJECT

    public:
        IpcServer(QObject* parent = nullptr);
        ~IpcServer();

        void writeToSocket(const QString& requestId, const QJsonDocument& doc);

    signals:
        void messagePushRequested(const MessagePushRequest& messagePushRequest);
        void clientsListRequested(const ClientsListRequest& clientsRequested);

    public slots:
        void onIpcPushRequestValidationFailed(const QString& requestId,
                                              const QString& failureType,
                                              const QString& failureReport,
                                              const QMap<int, QString>& fClnts);

        void onIpcMessagesScheduled(const QString& requestId,
                                    const QString& resultType,
                                    const QString& resultReport,
                                    const QMap<int, QString>& clientNicknames,
                                    const QVector<ScheduleTask>& tasks);

        void onIpcClientsListCreated(const QString& requestId,
                                     const QString& resultType,
                                     const QString& resultReport,
                                     const QVector<int>& listOfEnClinetsId,
                                     const QStringList& listOfEnClientsNick);
    private slots:
        void onNewConnection();
        void onReadyRead();

    private:
        QLocalServer* m_server = nullptr;
        QHash<QLocalSocket*, QByteArray> m_buffers;
        QHash<QString, QLocalSocket*> m_pendingRequests;

        const QString m_sockeFullPath = "/tmp/pingfinder/msgd.socket";
        void handleCommand(QJsonObject ipcObj);
        void handleMessagePush(const QString& requestId,
                                                     QJsonObject ipcPayloadObj);
        void handleClientsListRequested(const QString& requestId,
                                                     QJsonObject ipcPayloadObj);

        QJsonDocument pushValidationFailToJson(const QString& failureType,
                                               const QString& failureReport,
                                               const QMap<int, QString>& clnts);

        QJsonDocument pushMessagesScheduledToJson(const QString& resultType,
                                      const QString& resultReport,
                                      const QMap<int, QString>& clientNicknames,
                                      const QVector<ScheduleTask>& tasks);

        QJsonDocument pushClientsListToJson(const QString& resultType,
                                        const QString& resultReport,
                                        const QVector<int>& listOfEnClinetsId,
                                        const QStringList& listOfEnClientsNick);

};

#endif
// End ipcserver.h
