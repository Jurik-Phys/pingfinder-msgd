// Begin ipcserver.cpp

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>
#include "ipcserver.h"

IpcServer::IpcServer(QObject* parent) : QObject(parent){
    m_server = new QLocalServer(this);

    QFileInfo info(m_sockeFullPath);
    QString dirPath = info.absolutePath();
    QDir dir(dirPath);

    if (!dir.exists())
    {
        if (!dir.mkpath(dirPath))
        {
            qDebug() << "[EE] Failed to create socket directory" << dirPath;
            exit(1);
        }
    }

    // Удаление файла сокета, который мог остаться
    // после аварийного завершения работы программы
    QLocalServer::removeServer(m_sockeFullPath);

    if (!m_server->listen(m_sockeFullPath)){
        qDebug() << "[EE] Socket error" << m_server->errorString();
        exit(1);
    }

    // Установка прав на чтение/запись только для владельца и участника группы
    QFile::setPermissions(m_sockeFullPath, QFileDevice::ReadOwner |
                                           QFileDevice::WriteOwner|
                                           QFileDevice::ReadGroup |
                                           QFileDevice::WriteGroup);

    QObject::connect(m_server, &QLocalServer::newConnection,
                                              this,&IpcServer::onNewConnection);
}

IpcServer::~IpcServer(){
    if (m_server->isListening()){
        m_server->close();
    }

    QLocalServer::removeServer(m_sockeFullPath);
}

void IpcServer::onNewConnection(){
    // Ликбез:
    // - соединения могут приходить пачкой;
    // - Qt даёт их буферизованно
    // - необходимо выбирать очередь полностью
    while (m_server->hasPendingConnections())
    {
        QLocalSocket* socket = m_server->nextPendingConnection();

        QObject::connect(socket, &QLocalSocket::readyRead,
                                                 this, &IpcServer::onReadyRead);

        QObject::connect(socket, &QLocalSocket::disconnected,
            this, [this, socket](){
                                    m_buffers.remove(socket);
                                    // Запросов немного, подойдёт
                                    // простой перебор для удаляемого сокета
                                    auto it = m_pendingRequests.begin();
                                    while (it != m_pendingRequests.end()){
                                        if (it.value() == socket){
                                            it = m_pendingRequests.erase(it);
                                        }
                                        else {
                                            it++;
                                        }
                                    }
                                    socket->deleteLater();
                                  });
    }
}

void IpcServer::onReadyRead(){
    QLocalSocket * socket = qobject_cast<QLocalSocket*>(sender());

    if (!socket){
        return;
    }

    m_buffers[socket].append(socket->readAll());

    QByteArray& buffer = m_buffers[socket];

    // Символ новой строки '\n' означает окончание полученной комманды
    // Собственно, ниже разбор получаемых данных, каждая строка преобразуется
    // в QJsonObject, которая уже обрабатывается в handleCommand(obj);

    while (true){

        // Индекс символа переноса строки
        int idx = buffer.indexOf('\n');

        // Выход, если больше нет переносов строки
        if (idx < 0){
            break;
        }

        // Выделение строки c json'ом
        QByteArray line = buffer.left(idx).trimmed();

        // Удаление из буфера использованных данных
        buffer.remove(0, idx + 1);

        // Пришёл пустой json
        if (line.isEmpty()){
            continue;
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);

        if (error.error != QJsonParseError::NoError){
            qDebug() << "[EE] JSON parse error:" << error.errorString();
            continue;
        }

        QJsonObject obj = doc.object();

        // Вставка requestId в верхний уровень получаемого json'а
        QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        m_pendingRequests[requestId] = socket;
        obj["request_id"] = requestId;

        // Обработка полученного json'а
        handleCommand(obj);
    }
}

void IpcServer::handleCommand(QJsonObject obj){
    if (obj["action"] == "message_push"){
        handleMessagePush(obj["request_id"].toString(),
                                                     obj["payload"].toObject());
    }

    if (obj["action"] == "clients_request"){
        QJsonObject payload = obj["payload"].toObject();
        handleClientsListRequested(obj["request_id"].toString(),
                                                     obj["payload"].toObject());
    }
}

void IpcServer::handleMessagePush(const QString& requestId,
                                                        QJsonObject payloadObj){

    QString messageType = payloadObj["message_type"].toString();
    QString identType   = payloadObj["ident_type"].toString();
    QString message     = payloadObj["message"].toString();
    QString providedBy  = payloadObj["provided_by"].toString();
    QStringList idents;
    QJsonArray identsJsonArray = payloadObj["idents"].toArray();
    for (int i = 0; i < identsJsonArray.size(); ++i){
        idents.push_back(identsJsonArray[i].toString());
    }

    // Формирование структуры для передачи данных через сигнал
    // в Daemon::onMessagePushRequested
    MessagePushRequest messagePushRequest;
    messagePushRequest.requestId   = requestId;
    messagePushRequest.messageType = messageType;
    messagePushRequest.message     = message;
    messagePushRequest.identType   = identType;
    messagePushRequest.idents      = idents;
    messagePushRequest.providedBy  = providedBy;

    emit messagePushRequested(messagePushRequest);
}

void IpcServer::handleClientsListRequested(const QString& requestId,
                                                        QJsonObject payloadObj){
    ClientsListRequest clientsListRequest;
    clientsListRequest.providedBy = payloadObj["provided_by"].toString();
    clientsListRequest.requestId  = requestId;

    emit clientsListRequested(clientsListRequest);
}

void IpcServer::onIpcPushRequestValidationFailed(const QString& requestId,
                                                 const QString& failureType,
                                                 const QString& failureReport,
                                                 const QMap<int, QString>& cln){
    QJsonDocument pushValidationReportDoc;
    pushValidationReportDoc = pushValidationFailToJson(failureType,
                                                       failureReport, cln);

    qDebug() << "[II] [writeToSocket] Socket uuid" << requestId;

    writeToSocket(requestId, pushValidationReportDoc);
}

QJsonDocument IpcServer::pushValidationFailToJson(const QString& failureType,
                                                  const QString& failureReport,
                                            const QMap<int, QString>& fClients){
    QJsonObject obj;

    obj["action"] = "message_push_validation_failed";

    QJsonObject payloadObj;
    payloadObj["failure_type"] = failureType;
    payloadObj["failure_report"] = failureReport;

    QJsonArray failureClients;
    for (auto it = fClients.cbegin(); it != fClients.cend(); ++it){
        QJsonObject entryObj;
        entryObj["id"] = QString::number(it.key());
        entryObj["nickname"] = it.value();
        failureClients.push_back(entryObj);
    }
    payloadObj["clients"] = failureClients;

    obj["payload"] = payloadObj;

    return QJsonDocument(obj);
}

void IpcServer::writeToSocket(const QString& requestId,
                                                const QJsonDocument& inJsonDoc){
    QByteArray data = inJsonDoc.toJson(QJsonDocument::Compact);
    data.append('\n');

    QLocalSocket* socket = m_pendingRequests[requestId];

    if (socket->write(data) == - 1){
        qDebug() << "[EE] Write failed:" << socket->errorString();
    }

    if (!socket->waitForBytesWritten(3000)){
        qDebug() << "[EE] Write timeout:" << socket->errorString();
    }
}

void IpcServer::onIpcMessagesScheduled(const QString& requestId,
                                    const QString& resultType,
                                    const QString& resultReport,
                                    const QMap<int, QString>& clientNicknames,
                                    const QVector<ScheduleTask>& tasks){
    qDebug() << "[II] [IpcServer::onIpcMessagesScheduled] requesId" <<
                                                                      requestId;
    qDebug() << "[II] [IpcServer::onIpcMessagesScheduled] resultType" <<
                                                                     resultType;
    qDebug() << "[II] [IpcServer::onIpcMessagesScheduled] resultReport" <<
                                                                   resultReport;

    QJsonDocument pushMessagesScheduledDoc;
    pushMessagesScheduledDoc = pushMessagesScheduledToJson(resultType,
                                          resultReport, clientNicknames, tasks);

    qDebug() << "[II] [writeToSocket] [onIpcMessagesScheduled] request uuid"
                                                                   << requestId;

    writeToSocket(requestId, pushMessagesScheduledDoc);
}

void IpcServer::onIpcClientsListCreated(const QString& requestId,
                                        const QString& resultType,
                                        const QString& resultReport,
                                        const QVector<int>& listOfEnClinetsId,
                                        const QStringList& listOfEnClientsNick){

    QJsonDocument clientsListDoc = pushClientsListToJson(resultType,
                                                         resultReport,
                                                         listOfEnClinetsId,
                                                         listOfEnClientsNick);
    writeToSocket(requestId, clientsListDoc );
}

QJsonDocument IpcServer::pushMessagesScheduledToJson(const QString& resultType,
                                      const QString& resultReport,
                                      const QMap<int, QString>& clientNicknames,
                                      const QVector<ScheduleTask>& tasks){
    QJsonObject obj;

    obj["action"] = "message_push_scheduled";

    QJsonObject payloadObj;
    payloadObj["result_type"]   = resultType;
    payloadObj["result_report"] = resultReport;

    QJsonArray tasksArray;

    for (int i = 0; i < tasks.size(); ++i){
        QJsonObject taskObj;
        taskObj["client_id"] = tasks[i].clientId;
        taskObj["client_nickname"] = clientNicknames[tasks[i].clientId];
        taskObj["uuid"]      = tasks[i].uuid;
        taskObj["execute_time"]  = tasks[i].execTime;
        tasksArray.push_back(taskObj);
    }
    payloadObj["tasks"] = tasksArray;

    obj["payload"] = payloadObj;

    return QJsonDocument(obj);
}

QJsonDocument IpcServer::pushClientsListToJson(const QString& resultType,
                                        const QString& resultReport,
                                        const QVector<int>& listOfEnClinetsId,
                                        const QStringList& listOfEnClientsNick){
    QJsonObject obj;
    obj["action"] = "clients_list_created";

    QJsonObject payloadObj;
    payloadObj["result_type"]   = resultType;
    payloadObj["result_report"] = resultReport;

    QJsonArray clientsArray;

    int sizeId = listOfEnClinetsId.size();
    int sizeNick = listOfEnClientsNick.size();

    if (sizeId == sizeNick){
        for (int i = 0; i < sizeId; ++i){
            QJsonObject clientObj;
            clientObj["id"] = listOfEnClinetsId[i];
            clientObj["nickname"] = listOfEnClientsNick[i];
            clientsArray.push_back(clientObj);
        }
        payloadObj["clients"] = clientsArray;
        obj["payload"] = payloadObj;
    }

    return QJsonDocument(obj);
}
// End ipcserver.cpp
