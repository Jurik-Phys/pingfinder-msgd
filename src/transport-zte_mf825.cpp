// Begin transport-zte_mf825.cpp

#include <QFile>
#include <QTimer>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include "transport-zte_mf825.h"

TransportZteMF825::TransportZteMF825(QObject* parent, const QString& config)
                                                         : MsgTransport(parent){
    initTransportZteMF825(config);
}

void TransportZteMF825::sendMessage(const Message& msg){
    if (msg.destination.type != DestinationType::MobilePhone){
        // emit msgFail(msg.uuid);
        return;
    }

    if (!isExistsDevice()){
        qDebug() << "[EE] [ZTE MF825] Device not found at" << m_devGateIP;
        emit msgFail(msg.uuid);
        return;
    }

    // Получение общего числа сообщений на устройстве перед отправкой нового смс
    int send = getDevOutcomeMsgCount();
    int recv = getDevIncomeMsgCount();
    int drft = getDevDraftMessageCount();
    int total = send + recv + drft;

    qDebug() << "[II] [ZTE MF825] > send: " << send;
    qDebug() << "[II] [ZTE MF825] > recv: " << recv;
    qDebug() << "[II] [ZTE MF825] > drft: " << drft;
    qDebug() << "[II] [ZTE MF825] > total:" << total;

    // Хранилище в модеме заполнено, очистка старейших сообщений,
    // чтобы было где сохранить отправленное сообщение
    if (total > m_devSmsMaxCount){
        deleteOldMessages();
    }

    // Удаление лишних символов номера телефона и процентное кодирование;
    QString phoneNumber = preparePhoneNumber(msg.destination.address);

    QString msgGsmTime = getSentMessageGsmTime();
    QString msgGsmText = prepareMessateToZteModemHEX(msg.text);

    qDebug() << "[II] [ZTE MF825] Send message:" << msg.text;

    if (pushMessageToMobileNetwork(phoneNumber, msgGsmTime, msgGsmText)){
        emit msgSent(msg.uuid);
    }
    else {
        emit msgFail(msg.uuid);
    }
}

void TransportZteMF825::initTransportZteMF825(const QString& configFile){

    QFile config(configFile);

    if (!config.open(QIODevice::ReadOnly)){
        qDebug() << "[EE] [ZTE MF825] Error opening" << configFile;
        exit(1);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(config.readAll(), &error);
    config.close();

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] [ZTE MF825] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject configObj = doc.object();
    QJsonArray transports = configObj["transports"].toArray();

    for (int i = 0; i < transports.size(); ++i){
        QJsonObject transportInfo = transports[i].toObject();
        if (transportInfo["type"] == "zte_mf825"){
            m_devGateIP = transportInfo["device_ip"].toString();
        }
    }

    qDebug() << "[II] [ZTE MF825] [Device IP]" << m_devGateIP;

    if (isExistsDevice()){
        m_devSmsMaxCount = getDevSmsMaxCount();
        qDebug() << "[II] [ZTE MF825] [m_devSmsMaxCount]" << m_devSmsMaxCount;
    }
    else {
        qDebug() << "[EE] [ZTE MF825] Device not found at" << m_devGateIP;
        exit(1);
    }
}

bool TransportZteMF825::isExistsDevice(){
    bool res = false;

    QTcpSocket socket;
    socket.connectToHost(m_devGateIP, 80);
    if (socket.waitForConnected(2000)){
        // Устройство доступно, подключение к сокету состоялось
        res = true;
    }
    else {
        qDebug() << "[EE] [ZTE MF825] Connection to ZTE MF825 failed:"
                                                        << socket.errorString();
        res = false;
    }
    return res;
}

QJsonObject TransportZteMF825::getJsonInfoFromDevice(const QString& cmd){
    QJsonObject resObj;

    QNetworkAccessManager netManager;
    QUrl url = QString("http://%1/goform/goform_get_cmd_process"
                              "?isTest=false&cmd=%2").arg(m_devGateIP).arg(cmd);
    QNetworkRequest request(url);
    QNetworkReply* reply = netManager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // Timeout timer
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);

    loop.exec();

    QByteArray response;

    if (timer.isActive()){
        timer.stop();

        if (reply->error() == QNetworkReply::NoError){
            response = reply->readAll();
        }
        else {
            qDebug() << "[EE] [ZTE MF825] Error to do 'get' request to device";
            exit(1);
        }
    }
    else {
        qDebug() << "[EE] [ZTE MF825] Timeout of 'get' device request";
        reply->abort();
        exit(1);
    }

    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(response);
    resObj = doc.object();

    return resObj;
}

// Максимальное число смс, хранимое на устройстве (резерв 10%)
int TransportZteMF825::getDevSmsMaxCount(){

    QString cUrlArgument = "sms_capacity_info";
    QString jsonArgument = "sms_nv_total";

    QJsonObject responseObj = getJsonInfoFromDevice(cUrlArgument);
    int res = responseObj[jsonArgument].toString().toInt();

    // Резервирование 10% на модеме. Пусть программа считает,
    // что максимальное число смс в хранилище модема на 10% меньше
    res = res - res/10;
    return res;
}

// Число отправленных смс, хранящихся в данный момент на устройстве
int TransportZteMF825::getDevOutcomeMsgCount(){
    QString cUrlArgument = "sms_capacity_info";
    QString jsonArgument = "sms_nv_send_total";

    QJsonObject responseObj = getJsonInfoFromDevice(cUrlArgument);
    return responseObj[jsonArgument].toString().toInt();
}

// Число полученных смс, хранящихся в данный момент на устройстве
int TransportZteMF825::getDevIncomeMsgCount(){
    QString cUrlArgument = "sms_capacity_info";
    QString jsonArgument = "sms_nv_rev_total";

    QJsonObject responseObj = getJsonInfoFromDevice(cUrlArgument);
    return responseObj[jsonArgument].toString().toInt();
}

// Число черновиков смс, хранящихся сейчас на устройстве
int TransportZteMF825::getDevDraftMessageCount(){
    QString cUrlArgument = "sms_capacity_info";
    QString jsonArgument = "sms_nv_draftbox_total";

    QJsonObject responseObj = getJsonInfoFromDevice(cUrlArgument);
    return responseObj[jsonArgument].toString().toInt();
}

void  TransportZteMF825::deleteOldMessages(){

    int drft = getDevDraftMessageCount();
    int recv = getDevIncomeMsgCount();
    int send = getDevOutcomeMsgCount();
    int total = send + recv + drft;
    int delMsgId = -1;

    // Цикл на случай, когда превышен резерв в 90% от максимальной ёмкости
    while (total > m_devSmsMaxCount){
        // 1. Опеределние id сообщения для удаления среди:
        //    - черновиков
        if (drft > 0 && delMsgId == -1){
            delMsgId = getOldestDraftMessageId();
        }

        //    - вхоящих сообщений (прочитано & не прочитано)
        if (recv > 0 && delMsgId == -1){
            delMsgId = getOldestIncomeMessageId();
        }

        //    - отправлных собщений (успешно & не успешно)
        if (send > 0 && delMsgId == -1){
            delMsgId = getOldestOutcomeMessageId();
        }

         // 2. Собственно, само удаление на основе самого id
        if (delMsgId != -1){
            qDebug() << "[II] [ZTE MF825] [deleteOldMessages] Id" << delMsgId;
            deleteMessageById(delMsgId);
        }
        else {
            qDebug()<< "[EE] [ZTE MF825] Unknown id of delelted message";
            exit(1);
        }

        // Пересчёт числа сообщений
        drft = getDevDraftMessageCount();
        recv = getDevIncomeMsgCount();
        send = getDevOutcomeMsgCount();
        total = send + recv + drft;
        // Сброс флага удаления
        delMsgId = -1;
    }
}

QJsonObject TransportZteMF825::getDevSmsDataTotal(){
    QJsonObject resObj;

    QNetworkAccessManager netManager;
    QUrl url = QString("http://%1/goform/goform_get_cmd_process?isTest=false"
            "&cmd=sms_data_total&page=0&data_per_page=500&mem_store=1&tags=10"
                                 "&order_by=order+by+id+desc").arg(m_devGateIP);

    QNetworkRequest request(url);
    QNetworkReply* reply = netManager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // Timeout timer
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);

    loop.exec();

    QByteArray response;

    if (timer.isActive()){
        timer.stop();

        if (reply->error() == QNetworkReply::NoError){
            response = reply->readAll();
        }
        else {
            qDebug() << "[EE] [ZTE MF825] [smsData] Error to do 'get' request "
                                                                    "to device";
            exit(1);
        }
    }
    else{
        qDebug() << "[EE] [ZTE MF825] Timeout of 'get' device request";
        reply->abort();
        exit(1);
    }

    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(response);
    resObj = doc.object();

    return resObj;
}

int TransportZteMF825::getOldestDraftMessageId(){
    int draft = 4; // "tag" : 4 - черновик сообщения
    int msgId = -1;
    QJsonObject smsDataTotal = getDevSmsDataTotal();
    QJsonArray messagesArray = smsDataTotal["messages"].toArray();

    for (int i = 0; i < messagesArray.size(); ++i){

        QJsonObject message = messagesArray[i].toObject();

        if  (message["tag"].toString().toInt() == draft){
            int currentId = message["id"].toString().toInt();
            if ( msgId == -1 ){
                msgId = currentId;
            }
            else {
                if (currentId < msgId){
                    msgId = currentId;
                }
            }
        }
    }
    return msgId;
}

int TransportZteMF825::getOldestIncomeMessageId(){
    int incomeAndRead   = 0; // "tag" : 0 - получено и прочитано
    int incomeAndUnread = 1; // "tag" : 1 - получено, но не прочитано
    int msgId = -1;
    QJsonObject smsDataTotal = getDevSmsDataTotal();
    QJsonArray messagesArray = smsDataTotal["messages"].toArray();

    for (int i = 0; i < messagesArray.size(); ++i){

        QJsonObject message = messagesArray[i].toObject();
        int msgTag = message["tag"].toString().toInt();
        if  (msgTag == incomeAndRead || msgTag == incomeAndUnread){
            int currentId = message["id"].toString().toInt();
            if ( msgId == -1 ){
                msgId = currentId;
            }
            else {
                if (currentId < msgId){
                    msgId = currentId;
                }
            }
        }
    }
    return msgId;
}

int TransportZteMF825::getOldestOutcomeMessageId(){
    int outcomeSuccess = 2; // "tag" : 2 - отправленo успешно
    int outcomeFailure = 3; // "tag" : 3 - отправлено неуспешно
    int msgId = -1;
    QJsonObject smsDataTotal = getDevSmsDataTotal();
    QJsonArray messagesArray = smsDataTotal["messages"].toArray();

    for (int i = 0; i < messagesArray.size(); ++i){

        QJsonObject message = messagesArray[i].toObject();
        int msgTag = message["tag"].toString().toInt();
        if  (msgTag == outcomeSuccess || msgTag == outcomeFailure){
            int currentId = message["id"].toString().toInt();
            if ( msgId == -1 ){
                msgId = currentId;
            }
            else {
                if (currentId < msgId){
                    msgId = currentId;
                }
            }
        }
    }
    return msgId;
}

void TransportZteMF825::deleteMessageById(const int& id){

    QNetworkAccessManager netManager;
    QUrl url = QString("http://%1/goform/goform_set_cmd_process?isTest=false"
                             "&goformId=DELETE_SMS&msg_id=%2&notCallback=true")
                                     .arg(m_devGateIP).arg(QString::number(id));

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute,false);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply* reply = netManager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    //Timeout timer
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);

    loop.exec();

    if (timer.isActive()){
        timer.stop();

        if (reply->error() == QNetworkReply::NoError){
            qDebug() << "[II] [ZTE MF825] Message deleted. Id" << id;
        }
        else {
            qDebug() << "[EE] [ZTE MF825] Error delete message. Id" << id;
            exit(1);
        }
    }
    else {
        qDebug() << "[EE] [ZTE MF825] Error to do 'get' request to device";
        exit(1);
    }

    reply->deleteLater();
}

QString TransportZteMF825::preparePhoneNumber(const QString& rawPhoneNumber){
    QString phoneNumber = rawPhoneNumber;

    // Удаление лишних символов
    phoneNumber.remove(" ");
    phoneNumber.remove("(");
    phoneNumber.remove(")");
    phoneNumber.remove("-");

    // Процентное или URL-кодирование номера телефона:
    // Знак "+" в form-urlencoded означает пробел, что ломает отправку сообщения
    // > Закомментировать строку ниже для имитации ошибки отправки sms модемом <
    // phoneNumber = phoneNumber.replace("+", "%2B");

    return phoneNumber;
}

QString TransportZteMF825::getSentMessageGsmTime(){

    QDateTime dt = QDateTime::currentDateTime();
    const int timeZone = dt.offsetFromUtc() / 3600;

    QString msgGsmTime = dt.toString("yy;MM;dd;HH;mm;ss;")
                                           + QString::asprintf("%+d", timeZone);

    return msgGsmTime;
}

// UCS2 (Universal Character Set 2-byte) — это способ хранения текста,
// где каждый символ кодируется ровно 2 байтами (16 битами).
// UCS2 = Unicode, где каждый символ занимает 2 байта.
// Каждый символ представлен 16-битным Unicode значением в формате UTF-16BE,
// используется в GSM для поддержки международных символов.
QString TransportZteMF825::prepareMessateToZteModemHEX(const QString& text)
{
    // Qt хранит QString внутри как UTF-16 (platform endian),
    // но GSM SMS требует UTF-16BE (Big Endian).

    QByteArray utf16be;
    utf16be.reserve(text.size() * 2);

    // ------------------------------------------------------------
    // 1. Переводим QString → UTF-16BE вручную
    // ------------------------------------------------------------
    for (QChar ch : text)
    {
        // unicode() — это 16-битный код символа (UTF-16 code unit)
        ushort u = ch.unicode();

        // Старший байт (high byte)
        utf16be.append(static_cast<char>((u >> 8) & 0xFF));

        // Младший байт (low byte)
        utf16be.append(static_cast<char>(u & 0xFF));
    }

    // ------------------------------------------------------------
    // 2. Конвертируем байты в HEX
    //    аналог xxd -p
    // ------------------------------------------------------------
    QByteArray hex = utf16be.toHex();

    // ------------------------------------------------------------
    // 3. Убираем возможные переносы строк
    //    (Qt их не добавляет, но оставим эквивалент tr -d '\n')
    // ------------------------------------------------------------
    hex.replace("\n", "");

    // ------------------------------------------------------------
    // 4. Возвращаем как QString (готово для GSM PDU / HTTP API)
    // ------------------------------------------------------------
    return QString::fromLatin1(hex);
}

bool TransportZteMF825::pushMessageToMobileNetwork(const QString& phoneNumber,
                                const QString& msgGsmTime,
                                const QString& msgGsmText){
    bool res = true;

    int prevOutcomeMsgCount = getDevOutcomeMsgCount();
    int prevDraftMsgCount = getDevDraftMessageCount();

    QNetworkAccessManager netManager;

    QString urlTemplate = "http://%1/goform/goform_set_cmd_process";
    QUrl url(urlTemplate.arg(m_devGateIP));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                                           "application/x-www-form-urlencoded");

    QUrlQuery query;
    query.addQueryItem("isTest", "true");
    query.addQueryItem("goformId", "SEND_SMS");
    query.addQueryItem("Number", phoneNumber);
    query.addQueryItem("sms_time", msgGsmTime);
    query.addQueryItem("MessageBody", msgGsmText);
    query.addQueryItem("ID", "-1");
    query.addQueryItem("encode_type", "UNICODE");

    QByteArray postData = query.toString(QUrl::FullyEncoded).toUtf8();
    QNetworkReply* reply = netManager.post(request, postData);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    loop.exec();

    if (reply->error() != QNetworkReply::NoError){
        qDebug() << "[EE] [ZTE MF825] Error push message to mobile network"
            << reply->errorString();
        res = false;
    }
    else {
        qDebug() << "[II] [ZTE MF825] Modem response:" << reply->readAll();
        waiting(1000);
        res = true;
    }

    if (res){
        qDebug() << "[II] [ZTE MF825] Tracking sent message";
        qDebug() << "[II] [ZTE MF825] [prevOutcomeMsgCount]:"
                                                         << prevOutcomeMsgCount;
        qDebug() << "[II] [ZTE MF825] [prevDraftMsgCount]:"
                                                           << prevDraftMsgCount;

        if (!isOutcomeMsg(prevOutcomeMsgCount, prevDraftMsgCount, msgGsmTime)){
            res = false;
        }
    }

    reply->deleteLater();

    return res;
}

bool TransportZteMF825::isOutcomeMsg(const int& initPrevOutcomeMsgCount,
                       const int& initPrevDraftMsgCount, const QString& msgGsmTime){
    bool res = true;

    int prevDraftMsgCount   = initPrevDraftMsgCount;
    int prevOutcomeMsgCount = initPrevOutcomeMsgCount;
    int currOutcomeMsgCount = getDevOutcomeMsgCount();
    int currDraftMsgCount = getDevDraftMessageCount();

    qDebug() << "[II] [ZTE MF825] [currOutcomeMsgCount]:"<< currOutcomeMsgCount;
    qDebug() << "[II] [ZTE MF825] [currDraftMsgCount]:  "<< currDraftMsgCount;

    int loopCounter = 0;
    while(true){
        if (prevOutcomeMsgCount != currOutcomeMsgCount){
            // Число отправленных сообщений изменилось,
            // если в них будет сообщение с датой и временем отправляемого
            // собщения, то отправка в текущем сеансе состоялась успешно.
            const int unknownId = -1;
            if (getIdMessageByGsmTime(msgGsmTime) != unknownId){
                qDebug() << "[II] [ZTE MF825] Message successfully sent. Id"
                                           << getIdMessageByGsmTime(msgGsmTime);
                res = true;
                break;
            }
            else {
                // Число отправленных сообщений изменилось, но отправляемое
                // сообщение не найдено, следовательно, нечто внешнее отправило
                // сообщение, необходимо обновить число отправленных сообщений,
                // чтобы не попадать в ветку поиска отправленного сообщения
                prevOutcomeMsgCount = getDevOutcomeMsgCount();
            }
        }
        else {
            // Число отправленных сообщений не изменилось, значит должно
            // измениться число черновиков в т.ч., неотправленные сообщения
            if (prevDraftMsgCount != currDraftMsgCount){
                const int unknownId = -1;
                int foundId = getIdMessageByGsmTime(msgGsmTime);
                if (foundId != unknownId){
                    qDebug() << "[EE] [ZTE MF825] Message not sent and move to "
                                                         "draft. Id" << foundId;
                    qDebug() << "[II] [ZTE MF825] Delete unsent message from "
                                                                       "device";
                    deleteMessageById(foundId );
                    res = false;
                    break;
                }
                else {
                    // Число черновиков изменилось, но отпраляемое сообщение
                    // среди соощений не найдено, следовательно, что-то внешнее
                    // изменило число черновиков и неоходимо сбросить этот
                    // счётчик, например, для случая отсутствия сети, когда
                    // сообщения попадают в черновик только через минуту после
                    // запроса на отправку
                    prevDraftMsgCount = getDevDraftMessageCount();
                }
            }
            else {
                // Число черновиков может не сразу измениться так, например,
                // при отсутствии сети, сообщение попадает в черновики только
                // через 60 секунд. Ожидание 5 сек.
                waiting(5000);
                prevDraftMsgCount = getDevDraftMessageCount();
            }
        }
        loopCounter++;
        // Таймаут 5 сек., -> одна минута 12 увеличений счётчика,
        // 60 увеличений - 5 минут. Таким образом, через 5 минут ожидания
        // нового черновика, система перестанет его ждать
        if (loopCounter > 60){
            qDebug() << "[EE] [ZTE MF825] Timeout tracking outcomming message";
            res = false;
            break;
        }
    }
    return res;
}

void TransportZteMF825::waiting(const int ms){
    QEventLoop loop;

    QTimer::singleShot(ms, &loop, &QEventLoop::quit);

    loop.exec();
}

int TransportZteMF825::getIdMessageByGsmTime(const QString& msgGsmTime){
    int msgId = -1;
    QJsonObject smsDataTotal = getDevSmsDataTotal();
    QJsonArray messagesArray = smsDataTotal["messages"].toArray();

    for (int i = 0; i < messagesArray.size(); ++i){

        QJsonObject message = messagesArray[i].toObject();
        QString time = message["date"].toString();

        // При передачи часового пояса в модем, его система преобразует часовой
        // пояс в число четвертей часа т.е., умножает переданное число на 4.
        // Для сравнения с оригинальной строкой необхоимо сделать обратное
        // преобразование. Данные из модема поделить на 4
        // и вернуть оригинальные разделители полей
        int tz = time.section(',', -1).toInt() / 4;
        time = time.section(',', 0, -2).replace(",",";");
        time = time + ";" + QString::asprintf("%+d", tz);
        if  (msgGsmTime == time){
            msgId = message["id"].toString().toInt();
            break;
        }
    }
    return msgId;
}

 // End transport-zte_mf825.cpp
