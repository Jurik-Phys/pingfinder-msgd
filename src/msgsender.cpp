// Begin msgsender.cpp

#include "msgsender.h"

MsgSender::MsgSender(QObject* parent) : QObject(parent){
}

MsgSender::~MsgSender(){
}

void MsgSender::addTransport(MsgTransport* transport){
    m_transports.push_back(transport);
}

void MsgSender::send(const Message& msg){
    for (int i = 0; i < m_transports.size(); ++i){
        m_transports.at(i)->sendMessage(msg);
    }
}

// End msgsender.cpp
