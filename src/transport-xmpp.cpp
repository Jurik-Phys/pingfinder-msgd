// Begin transportxmpp.cpp

#include "transport-xmpp.h"
#include <QTime>
#include <QFile>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>

TransportXmpp::TransportXmpp(QObject* parent, const QString& config)
                                                         : MsgTransport(parent){
    initTransportXmpp(config);
}

void TransportXmpp::sendMessage(const Message& msg){

    if (msg.destination.type != DestinationType::XmppRoom){
        // emit msgFail(msg.uuid);
        return;
    }

    QTime currentTime = QTime::currentTime();
    QString oText = "[" + currentTime.toString("HH:mm:ss") + "] " + msg.text;

    QProcess process;

    QStringList sendxmppArgs = { "-u", m_xmppLogin,
                                 "-j", m_xmppServer,
                                 "-p", m_xmppPassword,
                                 "--chatroom", msg.destination.address,
                                 "-t"
                               };

    QString room = msg.destination.address;

    if (room.isEmpty()){
        qDebug() << "[EE] [XMPP] No logroom";
        emit msgFail(msg.uuid);
        return;
    }
    process.start("sendxmpp", sendxmppArgs);

    if (!process.waitForStarted(1000)) {
        qDebug() << "[EE] [XMPP] Failed to start sendxmpp";
        emit msgFail(msg.uuid);
        return;
    }

    process.write(oText.toUtf8());
    process.closeWriteChannel();

    int timeoutMs = 7500;
    if (!process.waitForFinished(timeoutMs)) {
        qDebug() << "[EE] [XMPP] Timeout waiting for sendxmpp";
        process.kill();
        emit msgFail(msg.uuid);
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0){
        qDebug() << "[EE] [XMPP] Sendxmpp failed:"
                                              << process.readAllStandardError();
        emit msgFail(msg.uuid);
    }

    qDebug() << "[II] [XMPP] Send message:" << oText;

    // Отправка сигнала удачной отправки сообщения
    emit msgSent(msg.uuid);
}

void TransportXmpp::initTransportXmpp(const QString& configFile){

    QFile config(configFile);

    if (!config.open(QIODevice::ReadOnly)){
        qDebug() << "[EE] [XMPP] Error opening" << configFile;
        exit(1);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(config.readAll(), &error);
    config.close();

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] [XMPP] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject configObj= doc.object();
    QJsonArray transports = configObj["transports"].toArray();

    for (int i = 0; i < transports.size(); ++i){
        QJsonObject transportInfo = transports[i].toObject();
        if (transportInfo["type"].toString() == "xmpp"){
            QString xmppLogin  = transportInfo["sender_login"].toString();
            QString xmppServer = transportInfo["sender_server"].toString();
            QString xmppPassword = transportInfo["sender_passwd"].toString();
            QString admin_logroom = transportInfo["admin_logroom"].toString();

            if (xmppLogin.isEmpty() || xmppServer.isEmpty()
                          || xmppPassword.isEmpty() || admin_logroom.isEmpty()){
                qDebug() << "[EE] [XMPP] Not all xmpp options are available "
                                               "in the " + configFile + " file";
                exit(1);
            }

            m_xmppLogin  = xmppLogin;
            m_xmppServer = xmppServer;
            m_xmppPassword = xmppPassword;
            m_admin_logroom = admin_logroom;
        }
    }
}

// End transportxmpp.cpp
