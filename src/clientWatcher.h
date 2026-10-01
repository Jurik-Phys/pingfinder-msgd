// Begin clientWatcher.h

#ifndef CLIENTWATCHER_H
#define CLIENTWATCHER_H

#include <QDateTime>
#include <QObject>
#include <QTimer>

struct FileMetadata {
    size_t    size;
    QDateTime lastModified;

    bool operator==(const FileMetadata& other) const {
        return size == other.size && lastModified == other.lastModified;
    }

    bool operator!=(const FileMetadata& other) const {
        return size != other.size || lastModified != other.lastModified;
    }
};

class ClientWatcher : public QObject{

    Q_OBJECT

    public:
        ClientWatcher(QObject* parent = nullptr);
        ~ClientWatcher();

        void updateFileMetadata();
        void setup(const QString& clientsFile);
        void start();

    signals:
        void fileChanged();

    public slots:
        void onClientsReloadedFromFile();

    private:
        QString      m_clientsFile;
        QTimer       m_checkClientsJsonFileTimer;
        const int    m_clientsCheckIntervalSec = 300;
        FileMetadata m_lastFileMetadata;
        void checkClientsJsonFile();

        FileMetadata getFileMetadata(const QString& clientsFile);
};

#endif
// End clientWatcher.h
