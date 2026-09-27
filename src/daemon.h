// Begin daemon.h
#ifndef DAEMON_H
#define DAEMON_H

#include <QDate>
#include <QTimer>
#include <QDebug>
#include <QDate>
#include "client.h"
#include "schedule.h"
#include "msgsender.h"
#include "transport-xmpp.h"
#include "transport-zte_mf825.h"
#include "ipcrequest.h"
#include "ipcserver.h"
#include "clientWatcher.h"

class Daemon : public QObject {

    Q_OBJECT

    public:
        Daemon(QObject *parent = nullptr);
        ~Daemon();

        void start();

    signals:
        // Сигнал при возникновении ошибок при проверке вхоящего сообщения
        void ipcPushRequestValidationFailed(const QString& requestId,
                                            const QString& failureType,
                                            const QString& failureReport,
                                            const QMap<int, QString>& fClients);

        // Сигнал при успешном добавлении входящих сообщений в расписание
        void ipcMessagesScheduled(const QString& requestId,
                                  const QString& resultType,
                                  const QString& resultReport,
                                  const QMap<int, QString>& clientNicknames,
                                  const QVector<ScheduleTask> scheduledTasks);

        // Сигнал при повторной загрузке списка клиентов из файла 'clients.json'
        void clientsReloadedFromFile();

    public slots:
        void onTick();
        void taskDone(const QString& taskUuid);
        void taskSendFail(const QString& taskUuid);
        void onMessagePushRequested(const MessagePushRequest& request);

    private:
        const int              m_scheduleStartHH   = 10;
        const int              m_scheduleStartMin  = 00;
        const int              m_scheduleStopHH    = 17;
        const int              m_scheduleStopMin   = 00;
        const int              m_taskDurationMin   = 3;
        const int              m_maxFailedAttempts = 5;
        QTimer                 m_timer;
        QVector<Client>        m_clients;
        QMap<QString, QString> m_smsTemplate;
        Schedule               m_schedule;
        QVector<ScheduleTask>  m_loadedTasks;
        ClientWatcher          m_clientWatcher;
        QString m_clientsFile = "/etc/pingfinder/msgd/clients.json";
        QString m_transportFile = "/etc/pingfinder/msgd/transport.json";
        QString m_clientStatusFile="/var/lib/pingfinder/msgd/clientStatus.json";
        QString m_taskStatusFile="/var/lib/pingfinder/msgd/taskStatus.json";
        QString m_taskIpcInfFile="/var/lib/pingfinder/msgd/taskIpcInf.json";
        QString m_scheduleFile="/var/lib/pingfinder/msgd/schedule.json";
        QString m_clientNewStatus     = "new";
        QString m_clientRegularStatus = "regular";
        QString m_taskNewStatus  = "pending";
        QString m_taskInProgress = "running";
        QString m_taskDoneStatus = "done";
        QString m_taskFailStatus = "failed";
        QString m_serviceName = "PingFinder";
        QString m_applicationName = "pingfinder-msgd";
        QString m_clientStatusFileDescription = "Statuses of PingFinder service"
                                                                     " clients";
        // Отправитель сообщений, который в блоке инициализации
        // получает доступные транспорты (sms, xmpp)
        MsgSender m_sender;
        bool initSendTransports();

        // При необходимости запускает формирование расписания на текущий день
        void runScheduleAction(const QDate&, const QTime&);

        // Получение списка дней, которые должны быть учтены в создаваемом
        // расписании (список т.к., может быть хвост из 29, 30, 31 чисел месяца)
        QVector<int> getActiveDays(const QDate&);

        // На основе списка активных дней текущего дневного расписания и списка
        // клиентов выделяется список активных клиентов (список их id)
        QVector<int> getActiveClientId(const QVector<int> activeDays);

        // Проверка того, является ли текущий день месяца последним. Проверка
        // необходима для определения хвоста из 29, 30 и 31 чисел месяца
        bool isLastDayOfMonth(const QDate& today);

        // Инициализация шаблонов сообщений
        void smsTemplateInit();

        // Загрузка списка клиентов программу. В данном случае из файла
        bool loadClientsFromFile(const QString& fileName);
        void reloadClientsFromFile();

        // Проверка владельца и прав доступа к файлу с данными клиентов
        bool isSecureConfigFile(const QString& fileName);

        // Очистка файла со статусами клиентов от записей тех клиентов, которых
        // больше нет в базе клиентов (были удалены вручную)
        // Возврат "true", если были внесены изменения
        bool removeOrphanClientStatuses(QMap<int, QString>&);

        // Получение статуса каждого активного пользователя по id.
        // При отсутствии файла clientStatus.json файл создаётся, если нет
        // стауса рассматриваемого пользователя id, то для него (id)
        // добавляется статус "New" После цикла по всем пользователям
        // изменения записываются в файл, a для активных пользователей
        // возвращается QMap с соответсвующими статусами
        QMap<int, QString> getClientStatusMap(QVector<int>);

        // Инициализация рабочего каталога сервиса /var/lib/pingfinder/msgd
        bool ensureWorkDirectory();

        // Инициализация файла /var/lib/pingfinder/msgd/clientStatus.json
        bool ensureClientStatusJsonFile();

        // Загрузка всех статусов из файла
        QMap<int, QString> loadClientStatusFromJsonFile();


        bool isUniqueNicknames(const QVector<Client>& clients);

        // Добавляет в общий список статусы активных клиентов, если их ещё нет
        // в общем списке статусов
        // Возврат "true", если были внесены изменения
        bool addClientStatusIfMissing(QMap<int, QString>&, const QVector<int>&);

        // Обновление файла статусов клиентов после добавления активных id
        // пользователей, а также после очистки статусов удалённых клиентов
        void updateClientStatusJsonFile(const QMap<int, QString>&);

        // Генерация набора задач для расписания на текущий день
        // Проверять флаг "enabled" у клиентов, если он "false",
        // то игнорировать клиента и не добавлять в расписание
        QVector<ScheduleTask> makeScheduleTasks(const QVector<int>&
                                                                activeClientId);

        // Функции генерации уникальных идентификаторов и времени запуска
        // создаваемых задач
        QString generateUuid(const int& clientId, const bool updTask = false);
        QString generateExecTime(const int& clientId, const QString& execTime);

        // Создание/перезапись файла расписания на сегодняшний день
        void updateScheduleJsonFile(const Schedule&);

        // Создание/обновление файла с данными о внешних входящих заданиях
        void updateTaskIpcInfJsonFile(const QVector<ScheduleTask>& tasks,
                            const QString& messageType, const QString& message);

        // Создание/перезапись файла статусов заданий
        void updateTaskStatusJsonFile(const Schedule&);

        // Инкремент числа неудачных попыток отправки сообщения
        // с сохранением информации в файл статусов задач (m_taskStatusFile)
        void incremetalFailedAttempts(const QString& taskUuid, int& failCount);

        QVector<QTime> getExecutionTimes(const int& taskCount);

        // Загрузка актуального списка задач из файла расписания
        void loadActualTaskListIfExists(const QDate&);

        // Метод обработки задач в расписании
        void runTaskAction();

        // Проверка необходимости выполнения текущей задачи
        bool isTaskDue(const ScheduleTask &task);

        // Метода, запускающий выполнение программы
        void executeTask(const ScheduleTask& task);

        // Методы генерации назначения отправления (MsgDestination)
        // для различных способов отправки сообщения (т.н. транспортов)
        MsgDestination buildSmsDestination(const int& clientId);
        MsgDestination buildXmppDestination(const int& clientId);
        MsgDestination buildXmppAdminDestination();

        // Генерация тела сообщения
        // a) Для сообщений, генериуемых самой программой
        QString getInternalMessage(const ScheduleTask& task);
        // б) Для сообщений, пришедших через канал IPC
        QString getExternalMessage(const ScheduleTask& task);
        // Обёртка над двумя вышеописанными методами генерации сообщений
        QString buildSendMessage(const ScheduleTask& task);

        // Для генерации сообщения необходимо знать статус и ник клиента
        Client getClientInfo(const int& clientId);
        QString getClientStatus(const int& clientId);
        QString getClientNick(const int& clientId);
        int getClientPayAmount(const int& clientId);

        // получение адреса jabbber-комнаты администратора;
        QString getAdminXmppDestination();

        // Установка статуса задачи/клиента
        void setTaskStatus(const QString& taskStatus, const QString& taskUuid);
        void setClientStatus(const QString& clientStatus, const int& clientId);

        // Получение id клиента по uuid задачи
        int getClientIdByTaskUuid(const QString& taskUuid);

        // Получение id клиента по его нику (требование их уникальности)
        int getClientIdByNick(const QString& nickname);

        QVector<int> getClientIdsFromIpcIdents(const QStringList& idents,
                                               const QString& identType);

        // IPC Server для отработки внешних комманд демону
        IpcServer* m_ipcServer;


        // Проверка попадания времени отправки сообщения пользователем
        // в рабочее время отправки сообщений сервисом
        bool validateIpcPushMessageAllowedTime(QString& failureReport);

        // Проверка разрешения отправить пользователю сообщение c возвращением
        // ассоциативного массива с исключёнными клиентами и причиной исключения
        bool validateIpcPushMessageType(const QString& messageType,
                                        const QString& identType,
                                        const QStringList& idents,
                                        QMap<int, QString>& problemClients,
                                        QString& failureReport);

        // Проверка необходимого запаса времени, для отправки сообщения
        // всем запланированным адресатам
        bool validateIpcPushMessageEnoughtTime(const QString& identType,
                                        const QStringList& idents,
                                        QMap<int, QString>& problemClients,
                                        QString& failureReport);

        // Число задач в расписании, ожидающих выполнения в данный момент
        int getSchedulePendingTask();

        // Получение списка задач к выполнению из файла со статусами задач
        QStringList getPendingTasksUuid();
        // Фильтрация списка задач к задачам из расписания текущего дня
        QStringList getPendingTasksUuidInSchedule();

        // Отcортированный список "зафрахтованного" времени для новых,
        // ожидающих выполнения задач в расписании
        QVector<QTime> getPendingTaskExecutionTimes();

        // Очистка списка со временем от тех пунктов, что уже в прошлом
        void cleanupObsoleteExecutionTimes(QVector<QTime>& pendingTaskExecTimes,
                                                        const QTime& startTime);

        QVector<QTime> makePushMessagesExecutionTimes(const int& count);

        // Вставить задачи в расписание (глобальную переменную m_schedule)
        void pushTasksToSchedule(const QVector<ScheduleTask>&);

        // Если в "taskIpcInf.json" есть выполненная задача, то надо её убрать
        void removeDoneTaskIpcInfo(const QString& taskUuid);

        // Получить задачу по её uuid
        ScheduleTask getTaskByUuid(const QString& taskUuid);

        ScheduleTask getTaskByClientId(const int& clientId);

        QString getTaskStatusByUuid(const QString& taskUuid);

        QString getTaskStatusByClientId(const int& clientId);

        QString addSendStatusMarker(const QString& taskUuid,const QString& msg);

        // Получить число неудачных попыток отправить сообщение
        int getTaskFailedAttempts(const QString& taskUuid);

        // Удаление осиротевших (orphan) клиентов из расписания;
        bool removeOrphanClientsFromSchedule(const QSet<int>& orphanClientsId);
        void removeClientById(const int& id);

        bool pushNewClientsToSchedule(const QSet<int>& newClientsId);

        // Перезапись времени выполнения для набора задач по их uuid
        void updTaskExecTimes(const QStringList& taskUuids,
                                               const QVector<QTime>& execTimes);
};

#endif
// End daemon.h
