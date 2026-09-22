// Begin clientWatcher.cpp

#include <QDebug>
#include <QFileInfo>
#include "clientWatcher.h"

ClientWatcher::ClientWatcher(QObject* parent) : QObject(parent){
    m_checkClientsJsonFileTimer.setInterval(m_clientsCheckIntervalSec*1000);

    QObject::connect(&m_checkClientsJsonFileTimer, &QTimer::timeout,
                                    this, &ClientWatcher::checkClientsJsonFile);
}

ClientWatcher::~ClientWatcher(){

}

void ClientWatcher::checkClientsJsonFile(){
    FileMetadata currentFileMetadata = getFileMetadata(m_clientsFile);

    if (m_lastFileMetadata != currentFileMetadata ){
        emit fileChanged();
    }
}

void ClientWatcher::setup(const QString& clientsFile){
    m_clientsFile = clientsFile;
    m_lastFileMetadata = getFileMetadata(clientsFile);
}

void ClientWatcher::start(){
    m_checkClientsJsonFileTimer.start();
}

FileMetadata ClientWatcher::getFileMetadata(const QString& clientsFile){
    FileMetadata metadata;
    QFileInfo fInfo(clientsFile);

    metadata.size = fInfo.size();
    metadata.lastModified = fInfo.lastModified();

    return metadata;
}

void ClientWatcher::onClientsReloadedFromFile(){
    m_lastFileMetadata = getFileMetadata(m_clientsFile);
}

// End clientWatcher.cpp
