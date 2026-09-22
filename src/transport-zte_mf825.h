// Begin transport-zte_mf825.h

#ifndef TRANSPORT_ZTE_MF825_H
#define TRANSPORT_ZTE_MF825_H

#include "msgtransport.h"
#include <QObject>

class TransportZteMF825 : public MsgTransport {

    Q_OBJECT

    public:
        TransportZteMF825(QObject* parent = nullptr,
                 const QString& config = "/etc/pingfinder/msgd/transport.json");

        void sendMessage(const Message& msg) override;

    private:
        QString m_devGateIP;
        int     m_devSmsMaxCount;
        int     m_devSmsNowCount;

        void initTransportZteMF825(const QString& config);
        QJsonObject getJsonInfoFromDevice(const QString& cmd);
        int getDevSmsMaxCount();
        int getDevOutcomeMsgCount();
        int getDevIncomeMsgCount();
        int getDevDraftMessageCount();
        void deleteOldMessages();

        QJsonObject getDevSmsDataTotal();
        int getOldestDraftMessageId();
        int getOldestIncomeMessageId();
        int getOldestOutcomeMessageId();

        void deleteMessageById(const int& id);

        QString preparePhoneNumber(const QString& rawPhoneNumber);
        QString prepareMessateToZteModemHEX(const QString& message);
        QString getSentMessageGsmTime();

        bool pushMessageToMobileNetwork(const QString& phoneNumber,
                                        const QString& msgGsmTime,
                                        const QString& msgGsmText);
        bool isOutcomeMsg(const int& prevOutcomeMsgCount,
                                        const int& prevDraftMsgCount,
                                                    const QString& msgGsmTime);
        int getIdMessageByGsmTime(const QString& msgGsmDateTime);

        bool isExistsDevice();
        void waiting(const int ms);
};

//// Теория ////
//"messages": [
//    {
//      "id": "95",
//      "number": "+7..."
//      "content": "005400650073007400200075006E00720065006100640020006D006",
//      "tag": "1",
//      "date": "26,05,29,17,57,08,+12",
//      "draft_group_id": "",
//      "received_all_concat_sms": "1",
//      "concat_sms_total": "0",
//      "concat_sms_received": "0",
//      "sms_class": "4",
//      "sms_mem": "nv",
//      "sms_submit_msg_ref": ""
//    },
//
// Здесь "tag" "0" - полученно и прочитано
//       "tag" "1" - полученно и непрочитано
//       "tag" "2" - успешно отправленно
//       "tag" "3" - ошибка отправки
//       "tag" "4" - черновик сообщения
////        /////

#endif
// End transport-zte_mf825.h
