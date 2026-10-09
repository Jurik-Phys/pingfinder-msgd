// Begin daemon.cpp

#include <QDir>
#include <QDate>
#include <QFile>
#include <QUuid>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QRandomGenerator>
#include "daemon.h"
#include <unistd.h>

Daemon::Daemon(QObject *parent) : QObject(parent){

    m_timer.setInterval(5000);
    QObject::connect(&m_timer, &QTimer::timeout, this, &Daemon::onTick);

    // Загрузка списка клиентов
    if (!loadClientsFromFile(m_clientsFile)){
        qDebug() << "[EE] Error loading client list. Exitting";
        exit(1);
    }

    // Инициализация доступных транспортов для отправки сообщений
    if (!initSendTransports()){
        qDebug() << "[EE] Error loading transports settings. Exitting";
        exit(1);
    };

    // Загрузка актуального по дате списка заданий
    loadActualTaskListIfExists(QDate::currentDate());

    // Запуск IPC Server'а для взаимодействия с демоном
    m_ipcServer = new IpcServer();

    QObject::connect(m_ipcServer, &IpcServer::messagePushRequested,
                                         this, &Daemon::onMessagePushRequested);
    QObject::connect(this, &Daemon::ipcPushRequestValidationFailed,
                     m_ipcServer, &IpcServer::onIpcPushRequestValidationFailed);
    QObject::connect(this, &Daemon::ipcMessagesScheduled,
                               m_ipcServer, &IpcServer::onIpcMessagesScheduled);

    QObject::connect(m_ipcServer, &IpcServer::clientsListRequested,
                                         this, &Daemon::onClientsListRequested);
    QObject::connect(this, &Daemon::ipcClientsListCreated,
                              m_ipcServer, &IpcServer::onIpcClientsListCreated);

    // При изменнеии файла со списком клиентов произвести перезагрузку файла
    QObject::connect(&m_clientWatcher, &ClientWatcher::fileChanged,
                                          this, &Daemon::reloadClientsFromFile);
    // После загрузки данных из файла, сообщаем ClientWatcher'у,
    // что обновление произошло и можно обновить метаданные отслежиаемого файла
    QObject::connect(this, &Daemon::clientsReloadedFromFile,
                   &m_clientWatcher, &ClientWatcher::onClientsReloadedFromFile);
    start();
}

Daemon::~Daemon(){
}

void Daemon::start(){
    qDebug() << "[II] Timer start";
    m_timer.start();

    // Настройка и запуск отслеживания изменений файла 'clients.json'
    m_clientWatcher.setup(m_clientsFile);
    m_clientWatcher.start();
}

void Daemon::onTick(){

    QTime time(QTime::currentTime());
    QDate date(QDate::currentDate());

    runScheduleAction(date, time);
    runTaskAction();
};

bool Daemon::isLastDayOfMonth(const QDate& today){
    bool last = false;

    if (today.day() == today.daysInMonth())
    {
        last = true;
    }
    else
    {
        last = false;
    }

    return last;
}

void Daemon::smsTemplateInit(){

    m_smsTemplate["hello"] = "Добро пожаловать в ряды пользователей сервиса "
                                                                   "PingFinder";
    m_smsTemplate["reminder"] = "Уважаемый клиент, напоминаем, что сервис "
                         "PingFinder существует благодаря Вам, поддержите его.";
}

void Daemon::runScheduleAction(const QDate& date, const QTime& time){
    // I. Проверка на наличие расписания на сегодняшний день,
    //    если оно есть в переменной m_schedule, то необходимо проверить
    //    актуальность списка клиентов в нём
    if (m_schedule.date == date){
        QSet<int> actualClientsId, scheduleClientsId;
        QVector<int> actualId = getActiveClientId(QVector<int> {date.day()});
        actualClientsId = QSet<int>(actualId.begin(), actualId.end());

        // Генерация списка клиентов, присутствующих в расписании
        // Внимание. В данный список попадают только то, что было сгенерировано
        // самим сервисом "pingfinder-msgd". Записи от клиентов, пришедших через
        // ipc сюда не входят т.к., тогда возникает проблема с удалением этих
        // самых клентов, пришедших через ipc (их нет в актуальных клиентах,
        // а значит их надо удалить по логике текущего момента).
        for (int j = 0; j < m_schedule.tasks.size(); ++j){
            if (m_schedule.tasks[j].providedBy == m_applicationName){
                scheduleClientsId.insert(m_schedule.tasks[j].clientId);
            }
        }

        // Если множества актуальных клиентов и клиентов в расписании разные,
        // значит было либо добавление новых клиентов, либо удаление, отключение
        // существующих клиентов. Всё это надо отразить в текущем расписании.
        // Добавление, удаление производится руками в файле 'clients.json'
        if (actualClientsId != scheduleClientsId){
            qDebug() << "[II] actualClientsId (" << actualClientsId.size()
               << ") != scheduleClientsId (" << scheduleClientsId.size() << ")";

            QSet<int> orphanClientsId = scheduleClientsId;
            orphanClientsId.subtract(actualClientsId);
            qDebug() << "[II] Orphan clients count:" << orphanClientsId.size();

            if (!orphanClientsId.isEmpty()){
                qDebug() << "[II] Remove orphaned clients";
                // Очистка расписания от осиротевших (orphan) клиентов
                removeOrphanClientsFromSchedule(orphanClientsId);
                updateScheduleJsonFile(m_schedule);
                updateTaskStatusJsonFile(m_schedule);

                // Очистка статусов клиентов от осиротевших (orphan) клиентов
                QMap<int, QString> clStatusMap = loadClientStatusFromJsonFile();
                removeOrphanClientStatuses(clStatusMap);
                updateClientStatusJsonFile(clStatusMap);
            }

            QSet<int> newClientsId = actualClientsId;
            newClientsId.subtract(scheduleClientsId);
            qDebug() << "[II] New clients count:" << newClientsId.size();

            if (!newClientsId.isEmpty()){
                qDebug() << "[II] Push new clients to schedule";

                pushNewClientsToSchedule(newClientsId);
                updateScheduleJsonFile(m_schedule);
                updateTaskStatusJsonFile(m_schedule);

                // Обновление статусов клиентов
                QMap<int, QString> allClientStatus
                                               = loadClientStatusFromJsonFile();
                if (addClientStatusIfMissing(allClientStatus,
                                                     actualClientsId.values())){
                    updateClientStatusJsonFile(allClientStatus);
                }
            }
            // Проверка на выполнение действий на следующей итерации,
            // После произведённых действий (добавление, удаление пользователей)
            // расписание не требует действий, выход на следующую итерацию
            return;
        }
        else {
            // Расписание не требует действий, выход на следующую итерацию
            return;
        }
    }
    else {
        qDebug() << "[II] Creating a new schedule";
    }

    // II. Расписания в программе нет, актуального расписания из файла нет,
    //     необходима генерация нового расписания

    // Набор дней, для которых будет строится расписание;
    // Набор для случая последнего дня месяца + хвост из (29), (30), 31 чисел
    QVector<int> activeDays = getActiveDays(date);
    qDebug() << "[II] Active days:" << activeDays;

    // Установка даты создаваемого ежедневного расписания
    m_schedule.date = date;

    // Список активных идентификаторов пользователей, включаемых в расписание
    QVector<int> activeClientId = getActiveClientId(activeDays);

    // Получение статуса каждого активного пользователя по id.
    // Кривой код, внутри функции записывается статус для новых клиентов
    // (хорошо бы это исправить в будущем)
    QMap<int, QString> activeClientStatusMap;
    activeClientStatusMap = getClientStatusMap(activeClientId);

    // Генерация расписания, число задач по числу активных id пользователей,
    m_schedule.tasks = makeScheduleTasks(activeClientId);

    // Запиь в файл полученного расписания текущего дня
    updateScheduleJsonFile(m_schedule);

    // Cоздание/перезапись файла статуса задач (статус задач "pending")
    updateTaskStatusJsonFile(m_schedule);

    qDebug() << "[II] Schedule creation is complete";
}

QVector<int> Daemon::getActiveDays(const QDate& date){
    int maxMonthDays = 31;
    QVector<int> activeDays;
    activeDays.push_back(date.day());

    // В случае последнего дня месяца, добавляем хвостовые дни в список активных
    // дней для построения расписания
    if (isLastDayOfMonth(date)){
        for (int i = date.day(); i <= maxMonthDays; ++i){
            if (i > date.day()){
                activeDays.push_back(i);
            }
        }
    }

    return activeDays;
}

bool Daemon::loadClientsFromFile(const QString& fileName){

    m_clients.clear();

    QFile clientsFile(fileName);

    if (!clientsFile.open(QIODevice::ReadOnly)){
        qDebug() << "[EE] Error opening" << fileName;
        qDebug() << "[EE] Check permissions or create the file using "
                                                    "the documentation example";
        return false;
    }
    else {
        // Проверка владельца и прав доступа к файлу с данными клиентов
        if (!isSecureConfigFile(fileName)){
            return false;
        }
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(clientsFile.readAll(), &error);

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
        return false;
    }

    QJsonObject root = doc.object();

    QJsonArray clients = root["clients"].toArray();

    for (int i = 0; i < clients.size(); ++i){
        Client client;
        QJsonObject obj = clients[i].toObject();

        client.id           = obj["id"].toInt();
        client.nickname     = obj["nickname"].toString();
        client.firstName    = obj["first_name"].toString();
        client.middleName   = obj["middle_name"].toString();
        client.lastName     = obj["last_name"].toString();
        client.phoneNumber  = obj["phone_number"].toString();
        client.reminderDay  = obj["reminder_day"].toInt();
        client.payAmount    = obj["pay_amount"].toInt();
        client.enabled      = obj["enabled"].toBool();

        if (client.enabled){
            m_clients.push_back(client);
        }
    }

    // Проверка nickname'ов на уникальность.
    if (!isUniqueNicknames(m_clients)){
        qDebug() << "[EE] Client nicknames must be unique! "
                                      "Please change the duplicate nickname(s)";
        return false;
    }

    qDebug() << "[II] Loaded" << m_clients.size() << "clients";
    qDebug() << "[II] All client nicknames are unique";
    return true;
}

void Daemon::reloadClientsFromFile(){
    loadClientsFromFile(m_clientsFile);
    emit clientsReloadedFromFile();
}

QVector<int> Daemon::getActiveClientId(const QVector<int> activeDays){
    QVector<int> activeClientId;

    for (int i = 0; i < m_clients.size(); ++i){
        for (int j = 0; j < activeDays.size(); ++j){
            // День уведомления клиента совпал c одним из дней
            // в списке активных дней (текущий день + возможный хвост)
            if (m_clients[i].reminderDay == activeDays[j] &&
                                                          m_clients[i].enabled){
                // Клиентам, пользующимся сервисом на безвозмездной основе,
                // которым уже было отправлено приветственное сообщение
                // не требуется отправлять сообщения, в противном случае,
                // например, для клиентов со статусом "new" требуется отправка
                int clientPayAmount = getClientPayAmount(m_clients[i].id);
                if (clientPayAmount == 0){
                    // Статус безвозмездного клиента
                    QString clientStatus = getClientStatus(m_clients[i].id);
                    if ( clientStatus != m_clientRegularStatus ){
                        activeClientId.push_back(m_clients[i].id);
                    }
                }
                else {
                    activeClientId.push_back(m_clients[i].id);
                }
            }
        }
    }

    return activeClientId;
}

QMap<int, QString> Daemon::getClientStatusMap(QVector<int> activeClientId){

    // Ensure /var/lib/pingfinder/msgd exists, create if necessary
    if (!ensureWorkDirectory()){
        qDebug() << "[EE] Application exit";
        exit(1);
    }

    // Ensure clientStatus.json exists, create if necessary
    if (!ensureClientStatusJsonFile()){
        qDebug() << "[EE] Application exit";
        exit(1);
    }

    // Если файл со статусами изменился, то обновляем внутренний кеш
    QFileInfo fileInfo(m_clientStatusFile);
    QDateTime realCientStatusFileLastModified = fileInfo.lastModified();
    if (m_clientStatusFileLastModified != realCientStatusFileLastModified){
        m_clientStatusesCache.clear();
        m_clientStatusesCache = loadClientStatusFromJsonFile();
        m_clientStatusFileLastModified = realCientStatusFileLastModified;
    }

    // Загрузка всех статусов из файла
    QMap<int, QString> allClientStatus = m_clientStatusesCache;

    bool isNeedToUpdateClientStatusJsonFile = false;
    // Очистка списка статусов от клиентов, отсутствующих в списке клиентов
    if (removeOrphanClientStatuses(allClientStatus)){
        qDebug() << "[II] Remove orphan client status records";
        qDebug() << "[II] JSON file status count excluding orphan records:"
                                                      << allClientStatus.size();
        isNeedToUpdateClientStatusJsonFile = true;
    };

    // Добавление статусов для отсуствующих (т.е. новых) клиентов
    // В activeClientId попадают и клиенты состатусом по умолчанию "unknown"
    if (addClientStatusIfMissing(allClientStatus, activeClientId)){
        qDebug() << "[II] Updated list of client status:"
                                                      << allClientStatus.size();
        isNeedToUpdateClientStatusJsonFile = true;
    };

    if (isNeedToUpdateClientStatusJsonFile){
        // Сохранение обновлений файла со статусами в файл, сохраняется только
        // в случае удаления неактуальных или вставки новых данных.
        updateClientStatusJsonFile(allClientStatus);
    }

    // TODO Такое ощущение, что возврат только активного списка особо и не нужен
    //      все данные будут подтягиваться из файла перед отправкой смс, чтобы
    //      обеспечить актуальность используемых данных
    // Фильтрация для возврата статусов только активных клиентов
    QMap<int, QString> res;

    for (int i = 0; i < activeClientId.size(); ++i){
        int id = activeClientId[i];
        if (allClientStatus.contains(id)){
            res.insert(id, allClientStatus.value(id));
        }
    }

    return res;
}

bool Daemon::ensureWorkDirectory(){
    bool res = false;

    QFileInfo fInfo(m_clientStatusFile);
    QDir varLibDir;
    if (!varLibDir.exists()){
        if (!varLibDir.mkpath(fInfo.absolutePath())){
            qDebug() << "[EE] Error create" << fInfo.absolutePath() << "dir";
        }
        else {
            // Каталог не существовал, но его удалось создать
            qDebug() << "[II] The directory was successfully created";
            res = true;
        }
    }
    else {
        res = true;
    }

    return res;
}

bool Daemon::ensureClientStatusJsonFile(){
    bool res = false;

    QFileInfo fInfo(m_clientStatusFile);
    if (fInfo.exists() && fInfo.isFile()) {
        // Файл существует, действий не требуется
        res = true;
    }
    else {
        // Формирование шаблона clientStatus.json
        QJsonObject aboutObj;
        aboutObj["description"] = m_clientStatusFileDescription;

        QJsonObject root;
        root["about"] = aboutObj;
        root["statuses"] = QJsonArray();

        QJsonDocument doc(root);

        // Создание файла clientStatus.json
        QFile jsonFile(m_clientStatusFile);
        if (!jsonFile.open(QIODevice::WriteOnly)){
            // Ошибка создания файла
            qDebug() << "[EE] Error creating" << m_clientStatusFile;
            res = false;
        }
        else {
            jsonFile.write(doc.toJson(QJsonDocument::Indented));
            jsonFile.close();
            res = true;
        }
    }

    return res;
}

QMap<int, QString> Daemon::loadClientStatusFromJsonFile(){
    QMap<int, QString> res;

    QFile clientStatus(m_clientStatusFile);
    if (!clientStatus.open(QIODevice::ReadOnly)){
        qDebug() << "[II] Error opening" << m_clientStatusFile;
        exit(1);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(clientStatus.readAll(), &error);
    clientStatus.close();

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject root = doc.object();
    QJsonArray  statuses = root["statuses"].toArray();

    for (int i = 0; i < statuses.size(); ++i){
        QJsonObject obj = statuses[i].toObject();
        int id = obj["client_id"].toInt();
        QString status = obj["status"].toString();
        res.insert(id, status);
    }
    return res;
}

bool Daemon::addClientStatusIfMissing(QMap<int, QString>& allClientStatus,
                                            const QVector<int>& activeClientId){
    bool isModified = false;
    for (int i = 0; i < activeClientId.size(); ++i){
        // Если активный id не содержится в общем списке статусов,
        // то необходимо добавить дефолтный статус для текущего активного id
        if (!allClientStatus.contains(activeClientId.at(i))){
            allClientStatus.insert(activeClientId.at(i), m_clientNewStatus);
            isModified = true;
        }
    }
    return isModified;
}

bool Daemon::removeOrphanClientStatuses(QMap<int, QString>& allClientStatus){
    bool isModified = false;

    // Выделение множества id для удобного поиска вхождений с помощью contains()
    QSet<int> clientIds;
    for (int i = 0; i < m_clients.size(); ++i){
        clientIds.insert(m_clients[i].id);
    }

    // Для всех ключей из файла со статусами проводится следующая проверка:
    // Если id из списка статусов отсутствует в списке клиентов,
    // то необходимо удалить данную запись из списка статусов
    for (auto it = allClientStatus.begin(); it != allClientStatus.end(); ) {
        if (!clientIds.contains(it.key())) {
            it = allClientStatus.erase(it);
            isModified = true;
        } else {
            ++it;
        }
    }

    return isModified;
}

bool Daemon::removeOrphanClientsFromSchedule(const QSet<int>& orphanClientsId){
    bool isRemoved = false;
    bool needUpdateExecTime = false;
    for (const int clientId : orphanClientsId){
        // Если удаляется хотя бы одно задание из запланированных,
        // то после всех удалений необходимо пересчитать время
        // выполнения заданий во избежание окошек в расписании
        QString taskStatus = getTaskStatusByClientId(clientId);
        if (taskStatus == m_taskNewStatus){
            needUpdateExecTime = true;
        }

        qDebug() << "[II]" << "Removed client id:" << clientId
                                    << "Task status:" << taskStatus;
        for (int i = 0; i < m_schedule.tasks.size(); ++i){
            ScheduleTask task = m_schedule.tasks[i];
        }

        removeClientById(clientId);

        for (int i = 0; i < m_schedule.tasks.size(); ++i){
            ScheduleTask task = m_schedule.tasks[i];
        }
        isRemoved = true;
    }

    if (needUpdateExecTime){
        qDebug() << "[II] Update execution times";

        // 1. Получить массив uuid задач со статусом "pending"
        QStringList pendingTasksUuid = getPendingTasksUuidInSchedule();

        // 2. Сгенерировать вектор с временем запуска задач по их числу
        QVector<QTime> updatedTaskExecTimes;
        updatedTaskExecTimes = getExecutionTimes(pendingTasksUuid.size());

        // 3. Обновить execTime в расписании по uuid задач
        updTaskExecTimes(pendingTasksUuid, updatedTaskExecTimes);
    }

    return isRemoved;
}

void Daemon::updateClientStatusJsonFile(const QMap<int, QString>& statusMap){
    // Формирование шаблона clientStatus.json
    QJsonObject aboutObj;
    aboutObj["description"] = m_clientStatusFileDescription;

    QJsonObject root;
    root["about"] = aboutObj;
    root["statuses"] = QJsonArray();

    QJsonArray statuses;
    for (auto it = statusMap.cbegin(); it != statusMap.cend(); ++it){
        QJsonObject statusObj;
        statusObj["client_id"] = it.key();
        statusObj["status"] = it.value();

        statuses.append(statusObj);
    }
    root["statuses"] = statuses;

    QJsonDocument doc(root);

    // Запись обновлённия статусов в clientStatus.json
    QFile jsonFile(m_clientStatusFile);
    if (!jsonFile.open(QIODevice::WriteOnly)){
        qDebug() << "[EE] Error writing" << m_clientStatusFile;
        exit(1);
    }
    else {
        jsonFile.write(doc.toJson(QJsonDocument::Indented));
        jsonFile.close();
    }
}

QVector<ScheduleTask> Daemon::makeScheduleTasks(const QVector<int>&
                                                                activeClientId){
    QVector<ScheduleTask> res;

    // Число заданий равно числу активных клиентов т.е., одна смс в день клиенту
    int taskCount = activeClientId.size();

    QVector<QTime> execTimeVector = getExecutionTimes(taskCount);

    for (int i = 0; i < taskCount; ++i){
        ScheduleTask task;
        task.clientId = activeClientId[i];
        task.uuid = generateUuid(activeClientId[i]);
        task.execTime = generateExecTime(activeClientId[i],
                                           execTimeVector[i].toString("HH:mm"));
        task.providedBy = m_applicationName;

        res.push_back(task);
    }

    return res;
}

QString Daemon::generateUuid(const int& clientId, const bool updTask){

    for (int i = 0; i < m_loadedTasks.size(); ++i){
        if (m_loadedTasks[i].clientId == clientId && updTask == false){
            return m_loadedTasks[i].uuid;
        }
    }

    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString Daemon::generateExecTime(const int& clientId,
                                                    const QString& rndExecTime){

    for (int i = 0; i < m_loadedTasks.size(); ++i){
        if (m_loadedTasks[i].clientId == clientId){
            return m_loadedTasks[i].execTime;
        }
    }

    return rndExecTime;
}

QVector<QTime> Daemon::getExecutionTimes(const int& taskCount){
    QVector<QTime> res;

    if (taskCount == 0){
        return res;
    }

    QTime currentTime(QTime::currentTime());
    QTime beginTime(m_scheduleStartHH, m_scheduleStartMin);

    if (currentTime > beginTime){
        beginTime = currentTime;
    }

    QTime endTime(m_scheduleStopHH,    m_scheduleStopMin);
    long int secOfDayTimeInterall = beginTime.secsTo(endTime);
    // Дробь будет неявно преобразована к целому числу,
    // что в данном случае подходит
    long int secOfTaskTimeInterall = secOfDayTimeInterall / taskCount;

    for (int i = 0; i < taskCount; ++i){
        QTime deterministicTime = beginTime.addSecs(i * secOfTaskTimeInterall);
        QTime resultTime = deterministicTime.addSecs(
                                QRandomGenerator::global()->bounded(-600, 601));
        res.push_back(resultTime);
    }

    return res;
}

void Daemon::updateScheduleJsonFile(const Schedule& schedule){

    // Если дело дошло до сохранения расписания, значит каталог со статусами
    // клиентов уже существует, он же рабочий каталог. Можно создавать файл

    QFile scheduleFile(m_scheduleFile);

    if (!scheduleFile.open(QIODevice::WriteOnly)){
        qDebug() << "[EE] Error write schedule";
        exit(1);
    }

    QJsonObject scheduleObj;
    QJsonObject aboutObj;

    aboutObj["description"] = "Schedule to send sms from PingFinder service";
    aboutObj["date"] = schedule.date.toString("yyyy-MM-dd");
    scheduleObj["about"] = aboutObj;
    scheduleObj["tasks"] = QJsonArray();

    QJsonArray tasks;
    for (int i = 0; i < schedule.tasks.size(); ++i){
        QJsonObject taskObj;
        taskObj["client_id"] = schedule.tasks[i].clientId;
        taskObj["uuid"] = schedule.tasks[i].uuid;
        taskObj["exec_time"] = schedule.tasks[i].execTime;
        taskObj["provided_by"] = schedule.tasks[i].providedBy;

        tasks.append(taskObj);
    }

    scheduleObj["tasks"] = tasks;

    QJsonDocument doc(scheduleObj);

    scheduleFile.write(doc.toJson(QJsonDocument::Indented));
    scheduleFile.close();
}

void Daemon::updateTaskStatusJsonFile(const Schedule& schedule){
    qDebug() << "[II] Update task status file";

    // I. Проверка на существование файла. Если файла нет,
    //    то создание файла со статусом нового задания
    //    и нулевым счётчиком неудачных попыток отправки сообщений
    if (!QFile::exists(m_taskStatusFile)){
        // Файла со статусом выполнения задач нет,
        // надо создать с изначальными статусами задач
        QFile taskStatus(m_taskStatusFile);
        if (!taskStatus.open(QIODevice::WriteOnly)){
            qDebug() << "[EE] Error creating" << m_taskStatusFile;
            exit(1);
        }

        QJsonObject taskStatusesObj;
        QJsonObject aboutObj;
        aboutObj["description"] = "Task statuses file";
        aboutObj["date"] = schedule.date.toString("yyyy-MM-dd");

        taskStatusesObj["about"] = aboutObj;
        taskStatusesObj["statuses"] = QJsonArray();

        QJsonArray statuses;

        for (int i = 0; i < schedule.tasks.size(); ++i){
            QJsonObject taskStatusObj;
            taskStatusObj["task_uuid"] = schedule.tasks[i].uuid;
            taskStatusObj["status"] = m_taskNewStatus;
            taskStatusObj["failed_attempts"] = 0;
            statuses.append(taskStatusObj);
        }

        taskStatusesObj["statuses"] = statuses;
        QJsonDocument doc(taskStatusesObj);

        taskStatus.write(doc.toJson(QJsonDocument::Indented));
        taskStatus.close();
        return;
    };

    // II. Файл со статусами после предыдущей проверки существует, далее...
    //     Если дата в статусе заданий не соответствует текущей дате расписания,
    //     то перезаписать файл новым чистым набором заданий на текущий день.
    QFile taskStatus(m_taskStatusFile);
    if (!taskStatus.open(QIODevice::ReadWrite)){
        qDebug() << "[EE] Error opening" << m_taskStatusFile;
        exit(1);
    }

    // Чтение JSON'а из файла и проверка корректности парсинга
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(taskStatus.readAll(), &error);

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject taskStatusesObj = doc.object();
    QJsonObject aboutObj = taskStatusesObj["about"].toObject();
    QString date = aboutObj["date"].toString("yyyy-MM-dd");
    qDebug() << "[II] Date of the task status file:" << date;

    if (date != schedule.date.toString("yyyy-MM-dd")){
        qDebug() << "[II] Schedule date and date of task statuses is different";
        // Файл со статусами не актуален, его необходимо перезаписать новым
        // чистым набором заданий, актуальных для текущего расписания / дня
        // c последующим выходом из данного метода
        // 1. Установка актуальной даты файла статусов
        aboutObj["date"] = schedule.date.toString("yyyy-MM-dd");
        taskStatusesObj["about"] = aboutObj;
        // 2. Очистка существующего списка статусов
        taskStatusesObj["statuses"] = QJsonArray();
        // 3. Генерация новых статусов, соответствующих расписанию
        QJsonArray statuses;
        for (int i = 0; i < schedule.tasks.size(); ++i){
            QJsonObject taskStatusObj;
            taskStatusObj["task_uuid"] = schedule.tasks[i].uuid;
            taskStatusObj["status"] = m_taskNewStatus;
            taskStatusObj["failed_attempts"] = 0;
            statuses.append(taskStatusObj);
        }

        taskStatusesObj["statuses"] = statuses;
        // 4. Актуализация JSON документа, очистка файла и запись в него
        doc.setObject(taskStatusesObj);
        taskStatus.resize(0);
        taskStatus.write(doc.toJson(QJsonDocument::Indented));
        taskStatus.close();
        return;
    }

    // III. Файл со статусами задач существует, дата в файле статусов актуальна.
    //      Отработка сценария, когда добавили/удалили клиента и перезапустили
    //      сервис. Алгоритм обработки расписания учитывает момент изменения
    //      числа клиентов, внося правки в файл расписания т.е., расписание
    //      всегда актуальное.
    //      Файл существует, дата совпадает с датой расписания, загрузка текущих
    //      статусов, проверка наличия всех заданий из расписания, добавление
    //      при необходимости, удаление тех заданий, которых нет в расписании.
    //      (удаление можно проигнорировать т.к., файл обновляется ежедневно)
    QJsonArray taskStatusesArray = taskStatusesObj["statuses"].toArray();

    // Сбор uuid из файла со статусами задач для удобного и быстрого поиска
    // вхождений через contains()
    QSet<QString> taskUuids;
    for (int i = 0; i < taskStatusesArray.size(); ++i){
        QJsonObject taskObj = taskStatusesArray[i].toObject();
        taskUuids.insert(taskObj["task_uuid"].toString());
    }

    for (int i = 0; i < schedule.tasks.size(); ++i){
        // Если uuid из расписания содержится во множестве tasks uuid,
        // то действий не требуется, задача из расписания под наблюдением.
        // Если uuid расписания в списке tasks uuid отсутствует,
        // то его необходимо добавить в мониторинг статусов.
        QString scheduleTaskUuid = schedule.tasks[i].uuid;

        if (!taskUuids.contains(scheduleTaskUuid)){
            QJsonObject taskStatusObj;
            taskStatusObj["task_uuid"] = schedule.tasks[i].uuid;
            taskStatusObj["status"] = m_taskNewStatus;
            taskStatusObj["failed_attempts"] = 0;
            taskStatusesArray.append(taskStatusObj);
        }
    }

    // Удаление лишних статусов задач, uuid которых уже нет в расписании,
    // например, удалили клиента из списка, обновлённое расписание уже не
    // содержит соответсвовашей ему задачи;

    // > Сбор uuid задач из расписания для удобного и быстрого поиска
    QSet<QString> scheduleTaskUuids;
    for (int i = 0; i < m_schedule.tasks.size(); ++i){
        scheduleTaskUuids.insert(m_schedule.tasks[i].uuid);
    }

    // > Удаление через создание нового массива статусов задач
    QJsonArray cleanTaskStatusesArray;
    for (int i = 0; i < taskStatusesArray.size(); ++i){
        QString uuidStatTask;
        uuidStatTask = taskStatusesArray[i].toObject()["task_uuid"].toString();
        // Если в расписании имеется uuid из файла состояний заданий, то оно
        // переходит в новую версию файла, если такового uuid нет в расписании,
        // то приоисходит его отбрасывание
        if (scheduleTaskUuids.contains(uuidStatTask)){
            QJsonObject cleanTaskStatusObj;
            cleanTaskStatusObj = taskStatusesArray[i].toObject();
            cleanTaskStatusesArray.append(cleanTaskStatusObj);
        }
    }

    taskStatusesObj["statuses"] = cleanTaskStatusesArray;

    doc.setObject(taskStatusesObj);
    taskStatus.resize(0);
    taskStatus.write(doc.toJson(QJsonDocument::Indented));
    taskStatus.close();
}


void Daemon::incremetalFailedAttempts(const QString& taskUuid, int& fails){

    qDebug() << "[II] [incremetalFailedAttempts]";

    QFile taskStatus(m_taskStatusFile);
    if (!taskStatus.open(QIODevice::ReadWrite)){
        qDebug() << "[EE] Error opening" << m_taskStatusFile;
        exit(1);
    }

    // Чтение JSON'а из файла и проверка корректности парсинга
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(taskStatus.readAll(), &error);

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject taskStatusesObj = doc.object();

    QJsonArray taskStatusesArray = taskStatusesObj["statuses"].toArray();

    for (int i = 0; i < taskStatusesArray.size(); ++i){
        QJsonObject taskStatusObj = taskStatusesArray[i].toObject();
        if (taskStatusObj["task_uuid"].toString() == taskUuid){
            const int failedAttempts = taskStatusObj["failed_attempts"].toInt();
            fails = failedAttempts + 1;
            taskStatusObj["failed_attempts"] = fails;

            taskStatusesArray[i] = taskStatusObj;

            break;
        }
    }

    taskStatusesObj["statuses"] = taskStatusesArray;

    doc.setObject(taskStatusesObj);

    taskStatus.resize(0);
    taskStatus.seek(0);

    taskStatus.write(doc.toJson(QJsonDocument::Indented));
    taskStatus.close();
}

void Daemon::updateTaskIpcInfJsonFile(const QVector<ScheduleTask>& tasks,
                            const QString& messageType, const QString& message){

    qDebug() << "[II] [updateTaskStatusJsonFile]";

    QFile taskIpcInfFile(m_taskIpcInfFile);
    const bool existed = taskIpcInfFile.exists();

    if (!taskIpcInfFile.open(QIODevice::ReadWrite)){
        qDebug() << "[EE] Error write opening" << m_taskIpcInfFile;
        exit(1);
    }

    QJsonDocument doc;

    if (existed){
        qDebug() << "[II] Loading JSON data from" << m_taskIpcInfFile;
        QJsonParseError err;
        doc = QJsonDocument::fromJson(taskIpcInfFile.readAll(), &err);

        if (err.error != QJsonParseError::NoError){
            qDebug() << "[EE] JSON parse error:" << err.errorString();
        }

        // Будет обновлён существующий файл
        QJsonObject root = doc.object();
        // Замена шапки (актуализация даты)
        QJsonObject aboutObj;
        aboutObj["date"] = QDate::currentDate().toString("yyyy-MM-dd");
        aboutObj["description"] = "Storage of raw message data received via IPC";

        QJsonArray dataJsonArray = root["data"].toArray();
        for (int i = 0; i < tasks.size(); ++i){
            QJsonObject task;
            task["source"] = tasks[i].providedBy;
            task["uuid"]   = tasks[i].uuid;
            task["type"]   = messageType;
            // Cообщения разных типов могут иметь дополнительные поля
            if (messageType == "notice"){
                task["note"]   = message;
            }
            dataJsonArray.push_back(task);
        }

        root["about"] = aboutObj;
        root["data"]  = dataJsonArray;

        QJsonDocument updDoc(root);

        taskIpcInfFile.resize(0);
        taskIpcInfFile.seek(0);
        taskIpcInfFile.write(updDoc.toJson(QJsonDocument::Indented));

    } else {
        // Будет создан новый файл "taskIpcInf.json"
        QJsonObject root;

        // Шапка JSON'файла
        QJsonObject aboutObj;
        aboutObj["date"] = QDate::currentDate().toString("yyyy-MM-dd");
        aboutObj["description"] = "Storage of raw message data received via IPC";

        // Основной блок данных
        QJsonArray dataJsonArray;
        for (int i = 0; i < tasks.size(); ++i){
            QJsonObject task;
            task["source"] = tasks[i].providedBy;
            task["uuid"]   = tasks[i].uuid;
            task["type"]   = messageType;
            if (messageType == "notice"){
                task["note"]   = message;
            }
            dataJsonArray.push_back(task);
        }

        root["about"] = aboutObj;
        root["data"]  = dataJsonArray;

        QJsonDocument doc(root);
        taskIpcInfFile.write(doc.toJson(QJsonDocument::Indented));
    }

    taskIpcInfFile.close();
}

void Daemon::loadActualTaskListIfExists(const QDate& date){
    // Проверка на наличие файла с расписанием
    QFile scheduleFile(m_scheduleFile);
    if (!scheduleFile.exists()){
        qDebug() << "[II] No schedule file. Skip loading tasks";
        return;
    }

    if (!scheduleFile.open(QIODevice::ReadOnly)){
        qDebug() << "[EE] Error opening schedule file to read";
        exit(1);
    }
    qDebug() << "[II] Open schedule file to read";

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(scheduleFile.readAll(), &error);

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject scheduleObj = doc.object();

    QJsonArray tasksArray = scheduleObj["tasks"].toArray();

    m_loadedTasks.clear();
    m_loadedTasks.reserve(tasksArray.size());

    for (int i = 0; i < tasksArray.size(); ++i){
        ScheduleTask task;
        QJsonObject taskObj = tasksArray[i].toObject();
        task.clientId = taskObj["client_id"].toInt();
        task.uuid = taskObj["uuid"].toString();
        task.execTime = taskObj["exec_time"].toString();
        task.providedBy = taskObj["provided_by"].toString();

        m_loadedTasks.push_back(task);
    }

    // Формирование расписания
    // > Считывание даты расписания из файла в переменную m_schedule
    QJsonObject aboutObj = scheduleObj["about"].toObject();
    m_schedule.date = QDate::fromString(aboutObj["date"].toString(),
                                                                  "yyyy-MM-dd");

    // Установка массива заданий в переменную с расписанием на текущий день
    m_schedule.tasks = m_loadedTasks;

    qDebug() << "[II] Load task list completed";
}

void Daemon::runTaskAction(){
    for (int i = 0; i < m_schedule.tasks.size(); ++i){
        bool taskDue = false;
        taskDue = isTaskDue(m_schedule.tasks[i]);
        if (taskDue){
            // Необходимо начинать выполнение задачи, однопоточный характер
            // выполнения задачи заблокирует последующие тики таймера
            qDebug() << "[II] Execute task of client with id"
                                               <<  m_schedule.tasks[i].clientId;
            executeTask(m_schedule.tasks[i]);
            break;
        }
    }
}

bool Daemon::isTaskDue(const ScheduleTask& task){
    bool res = false;

    QFile taskStatus(m_taskStatusFile);
    if (!taskStatus.open(QIODevice::ReadOnly)){
        qDebug() << "[EE] Error opening" << m_taskStatusFile;
        exit(1);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(taskStatus.readAll(), &error);

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject taskStatusObj= doc.object();
    QJsonArray taskStatusesArray = taskStatusObj["statuses"].toArray();

    QTime taskTime = QTime::fromString(task.execTime, "HH:mm");
    QTime hostTime = QTime::currentTime();
    // QTime hostTime = QTime::fromString("17:00", "HH:mm");

    if (taskTime <= hostTime){
        // Проверка статуса задачи
        QString actualTaskStatus;
        for (int i = 0; i < taskStatusesArray.size(); ++i){
            QJsonObject actualStatusInfo = taskStatusesArray[i].toObject();

            QString iTaskUuid = actualStatusInfo["task_uuid"].toString();
            if (iTaskUuid == task.uuid){
                actualTaskStatus = actualStatusInfo["status"].toString();
                break;
            }
        }

        if (actualTaskStatus == m_taskNewStatus){
            res = true;
        }
    }

    return res;
}

void Daemon::executeTask(const ScheduleTask& task){
    // Запуск выполнения задачи:

    // Формирование типа и адреса назанчения;
    MsgDestination xmppAdminDest  = buildXmppAdminDestination();
    MsgDestination smsDestination = buildSmsDestination(task.clientId);

    // Текст отправляемого сообщеня
    QString sendMessage = buildSendMessage(task);

    Message xmppMsg, phoneMsg;

    // Поскольку попыток отправить смс сообщение может быть несколько,
    // то на jabber достаточно отправить только первую попытку (спам не нужен)
    int failedAttempts = getTaskFailedAttempts(task.uuid);
    qDebug() << "[II] [executeTask] [failedAttempts]" << failedAttempts;
    if (failedAttempts == 0 || failedAttempts == m_maxFailedAttempts){
        xmppMsg.uuid = task.uuid;
        xmppMsg.destination = xmppAdminDest;
        QString xmppText = addSendStatusMarker(task.uuid, sendMessage);
        xmppMsg.text = xmppText;

        m_sender.send(xmppMsg);
    }

    phoneMsg.uuid = task.uuid;
    phoneMsg.destination = smsDestination;
    phoneMsg.text = sendMessage;

    qDebug() << "[II] Client ID" << task.clientId
                                    << "Mobile" << phoneMsg.destination.address;
    m_sender.send(phoneMsg);
}

bool Daemon::initSendTransports(){

    if (!isSecureConfigFile(m_transportFile)){
        return false;
    }

    MsgTransport* xmpp = new TransportXmpp(this, m_transportFile);
    MsgTransport* sms  = new TransportZteMF825(this, m_transportFile);

    // QObject::connect(xmpp, &MsgTransport::msgSent,  this, &Daemon::taskDone);
    // QObject::connect(xmpp, &MsgTransport::msgFail, this, &Daemon::taskSendFail);

    // Включить в дальнейшем
    QObject::connect(sms, &MsgTransport::msgSent,  this, &Daemon::taskDone);
    QObject::connect(sms, &MsgTransport::msgFail,  this, &Daemon::taskSendFail);

    // MsgSender::addTransport() ждёт указатель на базовый класс MsgTransport(),
    // (void addTransport(MsgTransport* transport), но в него также можно
    // передать и указатель на класс наследник, в котором реализуется
    // низкоуровневая функциональность конкретного транспорта.
    // В итоге MsgSender знает только то, что транспорты могут быть разные,
    // хранит указатели на транспорты, но ничего не знает про их реализацию.
    // Добавление нового транспорта очень простое, нужно его создать в куче
    // и добавить в объект класса MsgSender, в данном случае m_sender
    m_sender.addTransport(xmpp);
    m_sender.addTransport(sms);

    return true;
}

MsgDestination Daemon::buildXmppAdminDestination(){
    MsgDestination dest;

    dest.type = DestinationType::XmppRoom;

    // Адрес назначения - это jabber(xmmp) комната администратора,
    // в которую идёт дублирование отправленных сообщений
    dest.address = getAdminXmppDestination();

    return dest;
}

MsgDestination Daemon::buildSmsDestination(const int& clientId){
    MsgDestination dest;

    dest.type = DestinationType::MobilePhone;
    dest.address = getClientInfo(clientId).phoneNumber;

    return dest;
}

QString Daemon::buildSendMessage(const ScheduleTask& task){
    QString res;

    // Для сообщений генерируемых самим приложением, реализована внутренняя
    // генерация: "buildInternalMessage()";
    // Сообщения, полученные через внешние приложения загружают данные
    // из сохранённого json'a "taskIpcInf.json" buildExternalMessage();
    if (task.providedBy == m_applicationName){
        qDebug() << "[II] [buildSendMessage] [internal]";
        res = getInternalMessage(task);
    }
    else {
        qDebug() << "[II] [buildSendMessage] [external]";
        res = getExternalMessage(task);
    }

    return res;
}

QString Daemon::getInternalMessage(const ScheduleTask& task){
    QString res;

    int clientId = task.clientId;
    QString clientStatus = getClientStatus(clientId);
    QString clientNick   = getClientNick(clientId);
           clientNick[0] = clientNick[0].toUpper();

    res = "[II] send message for '" + clientNick
                                        + "' with '" + clientStatus +"' status";

    // Для новых клиентов отправляется приветственное сообщение
    if (clientStatus == m_clientNewStatus){
        res = clientNick + ", спасибо за выбор сервиса " + m_serviceName;
    }

    // Для регулярных клиентов отправляется напоминание о поддержке сервиса
    // Регулярные клиенты с нулевой оплатой не включаются в расписание
    if (clientStatus == m_clientRegularStatus){
        int clientPayAmount = getClientPayAmount(clientId);
        res = clientNick + ", " + m_serviceName
                            + " работает благодаря Вам. Поддержка: "
                                     + QString::number(clientPayAmount) + " р.";
    }

    return res;
}

QString Daemon::getExternalMessage(const ScheduleTask& task){
    QString res;
    QString clientNick = getClientNick(task.clientId);
         clientNick[0] = clientNick[0].toUpper();

    // Открыть файл с информацией по пришедших сообщениях через IPC, загрузить
    // его содержимое в QJsonObject

    QFile taskIpcInfFile(m_taskIpcInfFile);

    if (!taskIpcInfFile.open(QIODevice::ReadOnly)){
        qDebug() << "[EE] Error opening" << m_taskIpcInfFile;
        return QString("IPC text problem");
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(taskIpcInfFile.readAll(), &err);
    QJsonObject root = doc.object();
    QJsonArray dataJsonArray = root["data"].toArray();

    // 1. Получить тип прилетевшего сообщения, от которого зависит,
    //    какие данные будут необходимы для генерации текста сообщения
    QString taskType;
    for (int i = 0; i < dataJsonArray.size(); ++i){
        QJsonObject taskObj = dataJsonArray[i].toObject();
        if (task.uuid == taskObj["uuid"].toString()){
            taskType = taskObj["type"].toString();
        }
    }

    qDebug() << "[II] [Daemon] [getExternalMessage] taskType" << taskType;

    // 2. Обработка типа сообщения "notice", получение значения поля "note"
    if (taskType == "notice"){
        QString note;
        for (int i = 0; i < dataJsonArray.size(); ++i){
            QJsonObject taskObj = dataJsonArray[i].toObject();
            if (task.uuid == taskObj["uuid"].toString()){
                note = taskObj["note"].toString();
            }
        }
        res = clientNick + ", внимание. " + note;
    }

    if (taskType == "hello"){
        res = clientNick + ", спасибо за выбор сервиса " + m_serviceName;
    }

    taskIpcInfFile.close();
    return res;
}

QString Daemon::getClientStatus(const int& clientId){

    QString res = "unknown";
    QFileInfo fileInfo(m_clientStatusFile);

    // Внутренне значение даты и времени модификации файла со статусами
    // клиентов не совпадает cо значением даты и времени модификации файла
    QDateTime realCientStatusFileLastModified = fileInfo.lastModified();
    if (m_clientStatusFileLastModified != realCientStatusFileLastModified){
        qDebug() << "[II] Reload client statuses from" << m_clientStatusFile;
        m_clientStatusesCache.clear();
        m_clientStatusesCache = loadClientStatusFromJsonFile();
        m_clientStatusFileLastModified = realCientStatusFileLastModified;
    }

    if (m_clientStatusesCache.contains(clientId)) {
        res = m_clientStatusesCache[clientId];
    }

    return res;
}

Client Daemon::getClientInfo(const int& clientId){
    Client res;

    for (int i = 0; i < m_clients.size(); ++i){
        if (m_clients[i].id == clientId){
            res = m_clients[i];
            break;
        }
    }

    return res;
}

QString Daemon::getClientNick(const int& clientId){

    QString nickname = getClientInfo(clientId).nickname;

    if (nickname.isEmpty()){
        qDebug() << "[EE] Unknown client nick with id" << clientId << "Exit";
        exit(1);
    }

    return nickname;
}

int Daemon::getClientPayAmount(const int& clientId){
    return getClientInfo(clientId).payAmount;
}

QString Daemon::getAdminXmppDestination(){
    QString res;

    QFile transport(m_transportFile);

    if (!transport.open(QIODevice::ReadOnly)){
        qDebug() << "[II] Error opening file" << m_transportFile;
        exit(1);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(transport.readAll(), &error);
    transport.close();

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject root = doc.object();
    QJsonArray transports = root["transports"].toArray();

    for (int i = 0; i < transports.size(); ++i){
        if (transports[i].toObject()["type"].toString() == "xmpp"){
            res = transports[i].toObject()["admin_logroom"].toString();
            break;
        }
    }

    return res;
}

void Daemon::taskDone(const QString& taskUuid){
    qDebug() << "[II] [Task done]" << taskUuid;
    // 0. Задача успешно отправлена, дублирование сообщения на jabber,
    //    но тело сообщения должно соответствовать предыдущему статусу клиента.
    //    Например, для клиента "new" программа должна генерировать сообщение
    //    "hello", а для "regular" клиента должно быть сообщение о поддержке,
    //    поэтому сначала необходимо сгенерировать текст сообщения, а затем
    //    менять статусы задачи, клиента. Статус задачи влияет на statusMarker.
    ScheduleTask okTask = getTaskByUuid(taskUuid);
    QString sendMessage = buildSendMessage(okTask);

    // 1. После успешно отправленной задачи необходимо изменить статус задачи
    //    в файле  m_taskStatusFile (/var/lib/pingfinder/msgd/taskStatus.json),
    // 2. а также проверить и при необходимости изменить статус клиента в файле
    //    m_clientStatusFile (/var/lib/pingfinder/msgd/clientStatus.json)
    setTaskStatus(m_taskDoneStatus, taskUuid);
    setClientStatus(m_clientRegularStatus, getClientIdByTaskUuid(taskUuid));

    // 3. "Сообщение доставлено". Дублирование в Jabber
    int clientId = getClientIdByTaskUuid(taskUuid);
    QString nick = getClientNick(clientId);
    MsgDestination xmppAdminDest  = buildXmppAdminDestination();

    Message xmppMsg;
    xmppMsg.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    xmppMsg.destination = xmppAdminDest;
    xmppMsg.text = addSendStatusMarker(taskUuid, sendMessage);

    m_sender.send(xmppMsg);

    // 4. Удалить информацию о выполненной задаче
    removeDoneTaskIpcInfo(taskUuid);
}

void Daemon::setTaskStatus(const QString& taskStatus, const QString& taskUuid){
    // Запись нового статуса задачи в файле m_taskStatusFile

    QFile taskFile(m_taskStatusFile);
    if (!taskFile.open(QIODevice::ReadWrite)){
        qDebug() << "[EE] Error opening" << m_taskStatusFile;
        exit(1);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(taskFile.readAll(), &error);

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
    }

    QJsonObject root = doc.object();

    QJsonArray statuses = root["statuses"].toArray();

    for (int i = 0; i < statuses.size(); ++i){
        QJsonObject taskInfoObj = statuses[i].toObject();
        if (taskInfoObj["task_uuid"].toString() == taskUuid){
            taskInfoObj["status"] = taskStatus;
            statuses[i] = taskInfoObj;

            root["statuses"] = statuses;

            QJsonDocument docOut(root);

            taskFile.resize(0);
            taskFile.seek(0);
            taskFile.write(docOut.toJson(QJsonDocument::Indented));
            taskFile.close();

            break;
        }
    }
}

void Daemon::removeDoneTaskIpcInfo(const QString& taskUuid){
    // Если в файле "taskIpcInf.json" есть выполненная задача, то надо её убрать
    QFile taskIpcInfFile(m_taskIpcInfFile);

    // При отсутствии файла не создавать новый пустой
    if (!taskIpcInfFile.open(QIODevice::ReadWrite | QIODevice::ExistingOnly)){
        // Тихий пропуск ситуации, когда файл отстствует,
        // например, по причине того, что ещё не было запросов через IPC
        // и файл просто не был создан из-за отсутствия необходимости в нём
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(taskIpcInfFile.readAll(), &err);

    if (err.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << err.errorString();
        taskIpcInfFile.close();
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray prevDataJsonArray = root["data"].toArray();
    QJsonArray nextDataJsonArray;

    for (int i = 0; i < prevDataJsonArray.size(); ++i){
        QJsonObject taskObj = prevDataJsonArray[i].toObject();
        if (taskObj["uuid"] != taskUuid){
            nextDataJsonArray.push_back(taskObj);
        }
    }

    // Если число задач изменилось т.е., была выполнена одна из задач,
    // данные которой хранились в этом файле и более не требуются,
    // то в новый файл переносятся все данные, кроме данных выполненной задачи
    if (prevDataJsonArray.size() != nextDataJsonArray.size()){
        root["data"] = nextDataJsonArray;

        QJsonDocument updDoc(root);
        taskIpcInfFile.resize(0);
        taskIpcInfFile.seek(0);
        taskIpcInfFile.write(updDoc.toJson(QJsonDocument::Indented));
    }

    taskIpcInfFile.close();
}

int Daemon::getClientIdByTaskUuid(const QString& taskUuid){
    int res;

    for (int i = 0; i < m_schedule.tasks.size(); ++i){
        if (m_schedule.tasks[i].uuid == taskUuid){
            res = m_schedule.tasks[i].clientId;
            break;
        }
    }

    return res;
}

int Daemon::getClientIdByNick(const QString& nick){
    int clientId;

    for (int i = 0; i < m_clients.size(); ++i){
        if (nick.toLower() == m_clients[i].nickname.toLower()){
            clientId = m_clients[i].id;
            break;
        }
    }

    return clientId;
}

void Daemon::setClientStatus(const QString& clientStatus, const int& clientId){
    QFile statusFile(m_clientStatusFile);

    if (!statusFile.open(QIODevice::ReadWrite)){
        qDebug() << "[EE] Error opening" << m_clientStatusFile;
        exit(1);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(statusFile.readAll(), &error);

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] Error parse JSON:" << error.errorString();
    }

    QJsonObject root = doc.object();

    QJsonArray statuses = root["statuses"].toArray();

    for (int i = 0; i < statuses.size(); ++i){
        QJsonObject statusObj;
        if (statuses[i].toObject()["client_id"].toInt() == clientId){
            statusObj["client_id"] = clientId;
            statusObj["status"] = clientStatus;
            statuses[i] = statusObj;

            root["statuses"] = statuses;

            QJsonDocument docOut(root);

            statusFile.resize(0);
            statusFile.seek(0);
            statusFile.write(docOut.toJson(QJsonDocument::Indented));
            statusFile.close();

            break;
        }
    }
}

void Daemon::taskSendFail(const QString& taskUuid){

    // Инкремент числа неудачных попыток отправки сообщения
    // с сохранением в файл
    int failCounter;
    incremetalFailedAttempts(taskUuid, failCounter);

    if (failCounter >= m_maxFailedAttempts){
        setTaskStatus(m_taskFailStatus, taskUuid);

        // 3. "Сообщение недоставлено". Отправка сообщения в Jabber
        ScheduleTask failTask = getTaskByUuid(taskUuid);

        // int clientId = getClientIdByTaskUuid(taskUuid);
        // QString nick = getClientNick(clientId);
        MsgDestination xmppAdminDest  = buildXmppAdminDestination();

        Message xmppMsg;
        QString sendMessage = buildSendMessage(failTask);

        xmppMsg.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        xmppMsg.destination = xmppAdminDest;
        xmppMsg.text = addSendStatusMarker(taskUuid, sendMessage);

        m_sender.send(xmppMsg);
    }
}

void Daemon::onMessagePushRequested(const MessagePushRequest& inMsgPushRequest){

    // Работа с локальной переменной из-за того, что из входящего набора,
    // необходимо вырезать дубли для клиентов со статусом "new"
    MessagePushRequest msgPushRequest = inMsgPushRequest;

    qDebug() << "[II] [IPC input] [daemon.cpp] [onMessagePushRequested]:";

    qDebug() << "     > requestId:      " << msgPushRequest.requestId;
    qDebug() << "     > provided_by     " << msgPushRequest.providedBy;
    qDebug() << "     > message-type:   " << msgPushRequest.messageType;
    if (!msgPushRequest.message.isEmpty()){
        qDebug() << "     > message:        " << msgPushRequest.message;
    }
    qDebug() << "     > ident-type:     " << msgPushRequest.identType;
    qDebug() << "     > idents:         " << msgPushRequest.idents.join(" ");

    // Блок проверок входящего запроса:
    // Часть №0
    // - Сообщения могут быть отправлены только в рабочее время штатной отправки
    // Часть №1
    // - сообщения "hello" могут получать только пользователи со статусом "new"
    //   * Если hello предназначено для клиента со статусом отличным от "new",
    //     то данный клиент/получатель заносится в список проблемных
    //     пользователей с указанием проблемы ("idents", "reason").
    // - сообщения "notice" не доступны клиентам со статусом "new"
    //   * Аналогично формируется/пополняется массив проблемных клиентов;
    // - Внесение изменений в расписание текущего дня не производится, если есть
    //   случаи некорректного запроса. Необходимо, чтобы от пользователя утилиты
    //   pingfinder-push пришёл корректный запрос.
    // - Перед проверкой доступности необходимого числа временных слотов
    //   и при наличии проблемных запросов, необходимо отправить сообщение
    //   в сокет, для информирования пользователя утилиты pingfinder-push
    //   о проблеме (некорректном запросе)
    //
    // Часть №2
    // - Проверка числа сообщений, отправка смс 3 мин "m_taskDurationMinutes"
    //   * Общая длительность отправки сообщений "scheduleCapacityMinutes"
    //     Кстати, начало надо отсчитывать от текущего времени, а не от полного.
    //     m_startTimeHH:m_startTimeMin -- m_startTimeHH:m_startTimeMin;
    //   * Занятое время штатным расписанием "usedScheduleMinutes":
    //     taskCount*m_taskDurationMinutes;
    //   * Доступное время отправки сообщений "freeScheduleMinutes":
    //     разница между общим временем и уже занятым
    //   * Доступное число мест для новых задач "availableTaskSlots"
    //     round(freeScheduleMinutes/m_taskDurationMinutes)
    //   * Число получателей должно быть меньше, чем получателей idents'size();

    QMap<int, QString> failClients;
    QString failureReport;

    if (!validateIpcPushMessageAllowedTime(failureReport)){
        QString failureType = "message_time_not_allowed";
        emit ipcPushRequestValidationFailed(msgPushRequest.requestId,
                                       failureType, failureReport, failClients);
        qDebug() << "[II] [onMessagePushRequested] Requests time not allowed. "
                                                                         "Skip";
        return;
    }

    // 1-я часть, проверка типа и содержания сообщения
    failClients.clear();
    if (!validateIpcPushMessageType(msgPushRequest.messageType,
                                    msgPushRequest.identType,
                                    msgPushRequest.idents, failClients,
                                                                failureReport)){
        QString failureType = "message_type_mismatch";
        emit ipcPushRequestValidationFailed(msgPushRequest.requestId,
                                       failureType, failureReport, failClients);
        qDebug() << "[II] [onMessagePushRequested] Requests problem. Skip";
        return;
    }

    // Проблема. Если на текущий день в расписании есть задание для клиента
    // со статусом "new" и тут же прилетает ещё одно задание с типом "hello",
    // в обоих случаях должно генерироваться одно и тоже приветственное
    // сообщение, но нюанс в том, что после первого "hello", второе "hello"
    // теряет актуальность. Клиент уже будет иметь статус "regular" и к нему
    // улетит от программы не приветственное сообщение, а сообщение о донате.
    // Решение. Если 1. "message-type" == "hello"
    //               2. ipcClientId присутствует в расписании сегодняшнего дня.
    //               3. Тип клиента с данным id "new", то его просто пропускаем
    // из общей обработки и даже не пытаемся добавить в расписание т.к., он уже
    // и так там присутствует;
    //
    // Для обатной связи необходимо сохранить список проигнорированных клиентов,
    // сформировать ответ на базе реальной информации из расписания (exec_time,
    // uuid) и добавить к сформированному на базе других запросов ответу

    // Список числовых идентификаторов пользователей, "прилетевших" через IPC
    QVector<int> inClientIds = getClientIdsFromIpcIdents(msgPushRequest.idents,
                                                      msgPushRequest.identType);

    // 1. "message-type" == "hello"
    // Множество идентификаторов присутствующих и в расписании, и в списке IPC
    QVector<int> existsNewClientsIds;
    if (msgPushRequest.messageType == "hello"){
        for (auto scheduleIt = m_schedule.tasks.begin();
                                         scheduleIt != m_schedule.tasks.end();){
            int scheduleId = (*scheduleIt).clientId;
            for (auto it = inClientIds.begin(); it != inClientIds.end();){
                int ipcClientId = *it;

                // 2. ipcClientId присутствует в расписании сегодняшнего дня,
                //    запоминаем id и удаляем из списка идентификаторов.
                if (ipcClientId  == scheduleId){
                    existsNewClientsIds.push_back(ipcClientId);
                    it = inClientIds.erase(it);
                }
                else {
                    it++;
                }
            }
            scheduleIt++;
        }

        if (!existsNewClientsIds.isEmpty()){
            msgPushRequest.identType = QString("id");
            QStringList uniqueIdents;
            for (int i = 0; i < inClientIds.size(); ++i){
                uniqueIdents.push_back(QString::number(inClientIds[i]));
            }
            msgPushRequest.idents = uniqueIdents;
        }
    }

    // 2-я часть, проверка наличия достаточного времени для всех запросов
    failClients.clear();
    if (!validateIpcPushMessageEnoughtTime(msgPushRequest.identType,
                                    msgPushRequest.idents, failClients,
                                                                failureReport)){
        QString failureType = "message_not_enough_time";
        emit ipcPushRequestValidationFailed(msgPushRequest.requestId,
                                       failureType, failureReport, failClients);
        return;
    }

    qDebug() << "[II] Input data is correct";
    qDebug() << "[II] - - -";

    int count = msgPushRequest.idents.size();
    QVector<QTime> pushMsgExecutionTimes= makePushMessagesExecutionTimes(count);

    // Ассоциативный массив для параметров клиента id и nickname
    QMap<int, QString> clientNicknames;
    for (int i = 0; i < inClientIds.size(); ++i){
        int clientId = inClientIds[i];
        clientNicknames.insert(clientId, getClientNick(clientId));
    }

    // По рассчитанным временам выполнения входящего запроса теперь можно
    // создать соответствующие входяещему запросу задачи и их вектор
    QVector<ScheduleTask> pushRequestedTasks;

    for (int i = 0; i < pushMsgExecutionTimes.size(); ++i){
        int clientId = inClientIds[i];
        ScheduleTask newTask;

        newTask.clientId   = clientId;
        newTask.uuid       = generateUuid(clientId, true);
        newTask.execTime   = pushMsgExecutionTimes[i].toString("hh:mm");
        newTask.providedBy = msgPushRequest.providedBy;

        pushRequestedTasks.push_back(newTask);
    }

    // Дебаг сформированного списка задач, полученных через ipc
    for (int i = 0; i < pushMsgExecutionTimes.size(); ++i){
        QString newTaskNumber = QString::number(i + 1);
        qDebug().noquote() << "[II] New task №" + newTaskNumber;
        qDebug() << "     clientId:" << pushRequestedTasks[i].clientId;
        qDebug() << "         uuid:" << pushRequestedTasks[i].uuid;
        qDebug() << "     execTime:" << pushRequestedTasks[i].execTime;
        qDebug() << "     provided:" << pushRequestedTasks[i].providedBy;
    }

    // Информация по клиентам со статусом "new", уже находящимся в расписании
    if (!existsNewClientsIds.isEmpty()){
        for (int i = 0; i < existsNewClientsIds.size(); ++i){
            int existsClientId = existsNewClientsIds[i];
            QString existsTaskNumber = QString::number(i + 1);
            ScheduleTask task = getTaskByClientId(existsClientId);
            qDebug().noquote() << "[II] Old task №" + existsTaskNumber;
            qDebug() << "     clientId:" << task.clientId;
            qDebug() << "         uuid:" << task.uuid;
            qDebug() << "     execTime:" << task.execTime;
            qDebug() << "     provided:" << task.providedBy;
        }
    }

    // Добавление новых задач во внутренний реестр задач
    pushTasksToSchedule(pushRequestedTasks);

    // Обновление файла расписания текущего дня
    updateScheduleJsonFile(m_schedule);

    // Cоздание/перезапись файла статуса задач (статус задач "pending")
    updateTaskStatusJsonFile(m_schedule);

    // Создание/обновление файла с данными о внешней задаче
    updateTaskIpcInfJsonFile(pushRequestedTasks, msgPushRequest.messageType,
                                                        msgPushRequest.message);

    // Отправка сигнала по факту успешного добавления входящего IPC запроса
    // в очередь расписания текущего дня
    QString resultType = "message_push_scheduled";
    QString resultReport = "Request successfully scheduled";

    // Добавление информации о существующих new клиентах для возврата через IPC
    if (!existsNewClientsIds.isEmpty()){
        for (int i = 0; i < existsNewClientsIds.size(); ++i){
            int existsNewClientsId = existsNewClientsIds[i];
            clientNicknames.insert(existsNewClientsId,
                                             getClientNick(existsNewClientsId));
            pushRequestedTasks.push_back(getTaskByClientId(existsNewClientsId));
        }
    }

    emit ipcMessagesScheduled(msgPushRequest.requestId, resultType,
                             resultReport, clientNicknames, pushRequestedTasks);

    qDebug() << "[II] [onMessagePushRequested] All clients OK";
}

void Daemon::onClientsListRequested(const ClientsListRequest& request){

    qDebug() << "[II] Clients list requested via IPC by" << request.providedBy;

    QString requestId    = request.requestId;
    QString resultType   = "clients_request_completed";
    QString resultReport = "Clients list successfully created";

    QVector<int> listOfEnClinetsId;
    QStringList  listOfEnClientsNick;
    for (int i = 0; i < m_clients.size(); ++i){
        if (m_clients[i].enabled){
            listOfEnClinetsId.push_back(m_clients[i].id);
            listOfEnClientsNick.push_back(m_clients[i].nickname);
        }
    }

    emit ipcClientsListCreated(request.requestId, resultType, resultReport,
                                        listOfEnClinetsId, listOfEnClientsNick);
}

bool Daemon::validateIpcPushMessageType(const QString& messageType,
                                        const QString& identType,
                                        const QStringList& idents,
                                        QMap<int, QString>& problemClients,
                                                       QString& failureReport){
    bool result = true;
    failureReport.clear();

    // Список числовых идентификаторов пользователей, "прилетевших" через IPC
    QVector<int> incomeClientIds = getClientIdsFromIpcIdents(idents, identType);

    // Ассоциативный контейнер <id, status> для валидируемых клиентов
    QMap<int, QString> clientStatusMap = getClientStatusMap(incomeClientIds);

    // - сообщения "hello" могут получать только пользователи со статусом "new"
    //   * Если hello предназначено для клиента со статусом отличным от "new",
    //     то данный клиент/получатель заносится в список проблемных
    //     пользователей с указанием проблемы ("idents", "reason").
    if (messageType == "hello"){
        for (int i = 0; i < incomeClientIds.size(); ++i){

            QString nick;
            if (identType == "nick"){
                nick = idents[i];
            }
            if (identType == "id"){
                nick = getClientNick(idents[i].toInt());
                if (!nick.isEmpty()){
                    nick[0] = nick[0].toUpper();
                }
            }

            int id = incomeClientIds[i];
            if (clientStatusMap[id] != m_clientNewStatus){
                problemClients[id] = nick;
                if (failureReport.isEmpty()){
                    failureReport = "Hello message can only be sent "
                                                               "to new clients";
                }
                result = false;
            }
        }
    }

    // - сообщения "notice" не доступны клиентам со статусом "new"
    //   * Аналогично формируется/пополняется массив проблемных клиентов;
    if (messageType == "notice"){
        for (int i = 0; i < incomeClientIds.size(); ++i){

            QString nick;
            if (identType == "nick"){
                nick = idents[i];
            }
            if (identType == "id"){
                nick = getClientNick(idents[i].toInt());
                if (!nick.isEmpty()){
                    nick[0] = nick[0].toUpper();
                }
            }

            int id = incomeClientIds[i];
            if (clientStatusMap[id] == m_clientNewStatus){
                problemClients[id] = nick;
                if (failureReport.isEmpty()){
                    failureReport = "Notice message cannot be "
                                                          "sent to new clients";
                }
                result = false;
            }
        }
    }

    if (!problemClients.isEmpty()){
        qDebug() << "[II] Exclude client IDs" << problemClients;
    }

    return result;
}

bool Daemon::validateIpcPushMessageEnoughtTime(const QString& identType,
                                        const QStringList& idents,
                                        QMap<int, QString>& problemClients,
                                        QString& failureReport){
    bool result = true;
    // Часть №2
    // - Проверка числа сообщений, отправка смс 3 мин "m_taskDurationMinutes"
    //   * Общая длительность отправки сообщений "scheduleCapacityMin"
    //     Начало отсчитывается от текущего времени (времени запроса)
    //     до времени окончания отправки сообщений на текущий день;
    //   * Общая ёмкость на отправку сообщений "scheduleCapacityMsg"
    //   * Занятая ёмкость штатным расписанием "schedulePendingTask";
    //   * Максимальное число новых задач "freeTaskSlot"
    //   * Число получателей должно быть меньше, чем получателей idents'size();

    // Время прихода запроса на рассылку сообщений
    QTime pushMessageIncomeTime = QTime::currentTime();
    // Время окончания рассылки сообщений
    QTime scheduleEndTime(m_scheduleStopHH, m_scheduleStopMin);

    int scheduleCapacityMin = pushMessageIncomeTime.secsTo(scheduleEndTime)/60;

    // Результат будет усекаться до целого числа т.е., в меньшую сторону,
    // что в рассматриваемой задаче подходит (гланое не превышение)
    int scheduleCapacityMsg = scheduleCapacityMin / m_taskDurationMin;
    qDebug() << "[II] [Schedule capacity]" << scheduleCapacityMin << "min;"
                                           << scheduleCapacityMsg << "messages";
    int schedulePendingTask = getSchedulePendingTask();
    int freeTaskSlot  = scheduleCapacityMsg - schedulePendingTask;
    qDebug() << "[II] Pending task:" << schedulePendingTask
                                        << "|" << "free slots:" << freeTaskSlot;

    if (freeTaskSlot < idents.size()){
        qDebug().nospace() << "[EE] Too many clients specified: "
                                        << idents.size() << "/" << freeTaskSlot;
        failureReport = QString("Too many clients specified: %1 / %2")
                                          .arg(idents.size()).arg(freeTaskSlot);
        // Список числовых идентификаторов клиентов, "прилетевших" через IPC
       QVector<int> incomeClientIds = getClientIdsFromIpcIdents(idents,
                                                                     identType);

        // Список клиентов, которые не помещаются в очередь рассылки сообщений
        for (int i = 0; i < idents.size(); ++i){
            if (i >= freeTaskSlot){
                QString nick;
                if (identType == "nick"){
                    nick = idents[i];
                }
                if (identType == "id"){
                    nick = getClientNick(idents[i].toInt());
                }
                problemClients[incomeClientIds[i]] = nick;
            }
        }
        result = false;
    }

    return result;
}

bool Daemon::validateIpcPushMessageAllowedTime(QString& failureReport){
    bool result = true;

    // Время прихода запроса на рассылку сообщений
    QTime pushMessageIncomeTime = QTime::currentTime();

    // Время начала рассылки сообщений
    QTime scheduleBeginTime(m_scheduleStartHH, m_scheduleStartMin);

    // Прошло времени с момента начала выполнения расписания и текущим моментом
    int scheduleLeftTimeMin =scheduleBeginTime.secsTo(pushMessageIncomeTime)/60;

    // Если с начала открытия работы расписания прошло меньше m_taskDurationMin
    // времени, тем более, если время отрицательное, то значит ещё рано для msg
    if (scheduleLeftTimeMin < m_taskDurationMin){
        qDebug() << "[EE] Not allowed time to send message";
        failureReport = QString("The message sending window is not open yet "
            "(allowed: %1:%2–%3:%4)").arg(m_scheduleStartHH,  2, 10, QChar('0'))
                                     .arg(m_scheduleStartMin, 2, 10, QChar('0'))
                                     .arg(m_scheduleStopHH, 2, 10, QChar('0'))
                                     .arg(m_scheduleStopMin, 2, 10, QChar('0'));
        result = false;

    }

    // Время окончания рассылки сообщений
    QTime scheduleEndTime(m_scheduleStopHH, m_scheduleStopMin);

    int scheduleCapacityMin = pushMessageIncomeTime.secsTo(scheduleEndTime)/60;

    // Время отправки сообщений на сегодня истекло
    if (scheduleCapacityMin <= m_taskDurationMin){
        qDebug() << "[EE] Not allowed time to send message";
        failureReport = QString("The message sending window has expired "
            "(allowed: %1:%2–%3:%4)").arg(m_scheduleStartHH,  2, 10, QChar('0'))
                                     .arg(m_scheduleStartMin, 2, 10, QChar('0'))
                                     .arg(m_scheduleStopHH, 2, 10, QChar('0'))
                                     .arg(m_scheduleStopMin, 2, 10, QChar('0'));
        result = false;
    }

    return result;
}

QVector<int> Daemon::getClientIdsFromIpcIdents(const QStringList& idents,
                                                      const QString& identType){

    QVector<int> incomeClientIds;

    // Генерация списка идентификаторов пользователей,
    // которым планируется совершить рассылку сообщений
    if (identType == "nick"){
        // Получение пользовательских идентификаторов из ников
        for (int i = 0; i < idents.size(); ++i){
            incomeClientIds.push_back(getClientIdByNick(idents[i]));
        }
    }

    if (identType == "id"){
        // Через IpcServer приходят идентификаторы в виде строк,
        // приведение к целочисленному типу
        for (int i = 0; i < idents.size(); ++i){
            incomeClientIds.push_back(idents[i].toInt());
        }
    }

    return incomeClientIds;
}

int Daemon::getSchedulePendingTask(){
    int result = 0;

    QFile tSFile(m_taskStatusFile);

    if (tSFile.open(QIODevice::ReadOnly)){
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(tSFile.readAll(), &error);

        if (error.error != QJsonParseError::NoError){
            qDebug() << "[EE] JSON parse error:" << error.errorString();
        }

        QJsonObject root = doc.object();

        QJsonArray statuses = root["statuses"].toArray();

        for (int i = 0; i < statuses.size(); ++i){
            QJsonObject taskInfoObj = statuses[i].toObject();
            QString status = taskInfoObj["status"].toString();
            if (status == m_taskNewStatus){
                result++;
            }
        }
    }

    return result;
}

bool Daemon::isUniqueNicknames(const QVector<Client>& clients){
    bool result = true;

    QHash<QString, QVector<int>> data;

    // Для каждого ключа "nickname" пушим значение id в QVector<int>
    for (int i = 0; i < clients.size(); ++i){
        QString nickname = clients[i].nickname;
        int id = clients[i].id;
        data[nickname].push_back(id);
    }

    for ( auto it = data.cbegin(); it != data.cend(); ++it ){
        if (it.value().size() > 1){
            qDebug() << "[EE] Duplicate nickname:" << it.key()
                                                        << "Ids:" << it.value();
            result = false;
        }
    }

    return result;
}

QVector<QTime> Daemon::getPendingTaskExecutionTimes(){
    QVector<QTime> result;

    // Получение списка индентификаторов новых /ожидающих выполнения/ задач
    QStringList pendingTasksUuid = getPendingTasksUuid();

    // Получение времени выполнения с выборкой только ожидающих выполнения задач
    for (int i = 0; i < m_schedule.tasks.size(); ++i){
        ScheduleTask task = m_schedule.tasks[i];
        QTime execTime = QTime::fromString(task.execTime, "HH:mm");
        QString uuid = m_schedule.tasks[i].uuid;
        if (pendingTasksUuid.contains(uuid)){
            result.push_back(execTime);
        }
    }

    std::sort(result.begin(), result.end());

    return result;
}

QStringList Daemon::getPendingTasksUuid(){

    QStringList pendingTasksUuid;

    QFile tSFile(m_taskStatusFile);

    if (tSFile.open(QIODevice::ReadOnly)){
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(tSFile.readAll(), &error);

        if (error.error != QJsonParseError::NoError){
            qDebug() << "[EE] JSON parse error:" << error.errorString();
        }

        QJsonObject root = doc.object();

        QJsonArray statuses = root["statuses"].toArray();

        for (int i = 0; i < statuses.size(); ++i){
            QJsonObject taskInfoObj = statuses[i].toObject();
            QString status = taskInfoObj["status"].toString();
            if (status == m_taskNewStatus){
                QString uuid = taskInfoObj["task_uuid"].toString();
                pendingTasksUuid.push_back(uuid);
            }
        }
    }

    return pendingTasksUuid;
}

QStringList Daemon::getPendingTasksUuidInSchedule(){
    QStringList pendingTasksUuid = getPendingTasksUuid();

    for (auto it = pendingTasksUuid.begin(); it != pendingTasksUuid.end();){
        bool found = false;

        for (int i = 0; i < m_schedule.tasks.size(); ++i){
            if (m_schedule.tasks[i].uuid == *it){
                found = true;
                break;
            }
        }

        if (!found){
            it = pendingTasksUuid.erase(it);
        }
        else {
            ++it;
        }
    }

    return pendingTasksUuid;
}

void Daemon::cleanupObsoleteExecutionTimes(QVector<QTime>& pendingTaskExecTimes,
                                                        const QTime& startTime){

    // Идиома erase-remove для удаления элементов по условию

    // 1. std::remove_if(begin, end, predicate);
    // auto is "QVector<QTime>::iterator"
    auto newEnd = std::remove_if(pendingTaskExecTimes.begin(),
                                 pendingTaskExecTimes.end(),
                                 [&](QTime& time){
                                    return time <= startTime;
                                 });
    pendingTaskExecTimes.erase(newEnd, pendingTaskExecTimes.end());
}

QVector<QTime> Daemon::makePushMessagesExecutionTimes(const int& count){
    // Вектор c временем запуска "прилетевших" через IPC новых задач
    QVector<QTime> pushMessagesExecutionTimes;

    // Общий список запланированных (новых) задач на сегодня
    QVector<QTime> pendingTaskExecutionTimes = getPendingTaskExecutionTimes();

    // Инициализация начального времени выполнения сообщений из IPC
    QTime execTime = QTime::currentTime();
    execTime = QTime(execTime.hour(), execTime.minute());

    // Очистка списка невыполненных задач от тех, что запланированы ранее
    // текущего момента времени. Вставка новых задач будет идти по текущему
    // времени, а невыполненные задачи должны быть выполнены до IPC сообщений.
    // В целом, при нормальной работе системы таких сообщений быть не должно
    cleanupObsoleteExecutionTimes(pendingTaskExecutionTimes, execTime);

    // На каждого получателя по задаче;
    // pendingTimeIdx - индекс ближайшего по времени, запланированного задания
    int pendingTimeIdx = 0;

    while (pushMessagesExecutionTimes.size() < count){

        QTime nextTimeBarrier;

        // Если последняя запланированная задача из списка закончилась,
        // то следущий барьер устремялется вдаль (23:59), чтобы можно было
        // добавлять задачи без ограничений (иначе не завершится цикл).
        // Ограничения рассчитаны ранее по числу допустимых к выполнению задач
        if (pendingTimeIdx < pendingTaskExecutionTimes.size()){
            nextTimeBarrier = pendingTaskExecutionTimes[pendingTimeIdx];
        }
        else {
            nextTimeBarrier = QTime::fromString("23:59", "HH:mm");
        }

        // Если дельта по времени между ближайшим запланированным заданием
        // (nextTimeBarrier) и текущим временем меньше времени выполнения
        // задачи, то на текущее время можно ставить задачу
        if (execTime.secsTo(nextTimeBarrier) >= m_taskDurationMin*60){
            pushMessagesExecutionTimes.push_back(execTime);
            execTime = execTime.addSecs(m_taskDurationMin*60);
        }
        else {
            // Если до ближайшего запланированного времени нет свободного места,
            // то переход к рассмотрению следующего запланированного времени;
            //
            // Текущее время теперь отсчитвается от предыдущей отсечки,
            // путём прибавления к нему m_taskDurationMin*60 секунд
            execTime = nextTimeBarrier.addSecs(m_taskDurationMin*60);

            // Переход к новой планке/барьеру
            pendingTimeIdx++;
        }
    }

    return pushMessagesExecutionTimes;
}

void Daemon::pushTasksToSchedule(const QVector<ScheduleTask>& extTasks){
    for (int i = 0; i < extTasks.size(); ++i){
        m_schedule.tasks.push_back(extTasks[i]);
    }
}

ScheduleTask Daemon::getTaskByUuid(const QString& taskUuid){
    ScheduleTask task;

    for (int i = 0; i < m_schedule.tasks.size(); ++i){
        ScheduleTask nTask = m_schedule.tasks[i];
        if (nTask.uuid == taskUuid){
            task = nTask;
            break;
        }
    }

    return task;
}

ScheduleTask Daemon::getTaskByClientId(const int& clientId){
    ScheduleTask task;

    for (int i = 0; i < m_schedule.tasks.size(); ++i){
        ScheduleTask nTask = m_schedule.tasks[i];
        if (nTask.clientId == clientId){
            task = nTask;
            break;
        }
    }

    return task;
}

QString Daemon::getTaskStatusByUuid(const QString& taskUuid){
    QString status;

    QFile taskStatus(m_taskStatusFile);
    if (!taskStatus.open(QIODevice::ReadOnly)){
        qDebug() << "[EE] Error opening" << m_taskStatusFile;
        exit(1);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(taskStatus.readAll(), &error);

    QJsonObject statusObj = doc.object();
    QJsonArray taskStatusArray = statusObj["statuses"].toArray();

    for (int i = 0; i < taskStatusArray.size(); ++i){
        QJsonObject statusObj = taskStatusArray[i].toObject();

        if (statusObj["task_uuid"].toString() == taskUuid){
            status = statusObj["status"].toString();
            break;
        }
    }

    return status;
}

QString Daemon::getTaskStatusByClientId(const int& clientId){

    QString taskUuid;

    for (int i = 0; i < m_schedule.tasks.size(); ++i){
        if (m_schedule.tasks[i].clientId == clientId){
            taskUuid = m_schedule.tasks[i].uuid;
        }
    }

    return getTaskStatusByUuid(taskUuid);
}

QString Daemon::addSendStatusMarker(const QString& taskUuid,const QString& msg){
    QString markedMessage;
    // Статус задачи после успешной отправки меняется на "done",
    // что используется для кастомизации отправляемых сообщений
    QString taskStatus = getTaskStatusByUuid(taskUuid);
    QString space(" ");

    if (taskStatus == m_taskNewStatus){
        markedMessage ="> ○ <" + space + msg;
    } else {
        if (taskStatus == m_taskDoneStatus){
            markedMessage ="> ● <" + space + msg;
        }
        else {
            if (taskStatus == m_taskFailStatus){
                markedMessage ="> × <" + space + msg;
            }
        }
    }
    return markedMessage;
}

int Daemon::getTaskFailedAttempts(const QString& taskUuid){
    int failedAttempts = 0;

    QFile taskStatus(m_taskStatusFile);
    if (!taskStatus.open(QIODevice::ReadWrite)){
        qDebug() << "[EE] Error opening" << m_taskStatusFile;
        exit(1);
    }

    // Чтение JSON'а из файла и проверка корректности парсинга
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(taskStatus.readAll(), &error);

    if (error.error != QJsonParseError::NoError){
        qDebug() << "[EE] JSON parse error:" << error.errorString();
        exit(1);
    }

    QJsonObject taskStatusesObj = doc.object();

    QJsonArray taskStatusesArray = taskStatusesObj["statuses"].toArray();

    for (int i = 0; i < taskStatusesArray.size(); ++i){
        QJsonObject taskStatusObj = taskStatusesArray[i].toObject();
        if (taskStatusObj["task_uuid"].toString() == taskUuid){
            failedAttempts = taskStatusObj["failed_attempts"].toInt();
            break;
        }
    }

    taskStatus.close();

    return failedAttempts;
}

void Daemon::removeClientById(const int& id){
    for (auto it = m_schedule.tasks.begin(); it != m_schedule.tasks.end();){
        if (it->clientId == id){
            // Теория: erase() возвращает итератор на элемент,
            // который стал следующим после удалённого
            it = m_schedule.tasks.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Daemon::updTaskExecTimes(const QStringList& taskUuids,
                                               const QVector<QTime>& execTimes){
    for (int i = 0; i < taskUuids.size(); ++i){
        qDebug() << taskUuids[i];
        for (int j = 0; j < m_schedule.tasks.size(); ++j){
            // В расписании найдена задача по uuid,
            // у которой надо изменить время выполнения
            if (m_schedule.tasks[j].uuid == taskUuids[i]){
                // Индексы taskUuids и execTimes соответственные
                m_schedule.tasks[j].execTime = execTimes[i].toString("HH:mm");
                continue;
            }
        }
    }
}

bool Daemon::pushNewClientsToSchedule(const QSet<int>& newClientsId){
    bool inserted = false;

    // Расчёт времени выполнения для новых клиентов
    QVector<QTime> execTimes;
    execTimes = makePushMessagesExecutionTimes(newClientsId.size());

    // По рассчитанным временам выполнения задач можно
    // создать соответствующие задачи, a также вектор из них
    QVector<ScheduleTask> newClientTasks;

    auto it = newClientsId.begin();
    for (int i = 0; i < execTimes.size(); ++i){
        int clientId = *it;
        ++it; // Число элементов в execTimes и newClientsId равное
        ScheduleTask newTask;

        newTask.clientId   = clientId;
        newTask.uuid       = generateUuid(clientId);
        newTask.execTime   = execTimes[i].toString("hh:mm");
        newTask.providedBy = m_applicationName;

        newClientTasks.push_back(newTask);
    }

    // Добавление новых задач в расписание
    pushTasksToSchedule(newClientTasks);

    // Заглушка, если сюда программа дошла, значит вставка состоялась
    inserted = true;

    return inserted;
}

bool Daemon::isSecureConfigFile(const QString& fileName){

    QFileInfo fileInfo(fileName);

    // 1. Файл существует и является обычным файлом
    if (!fileInfo.isFile()){
        qDebug() << "[II] Not a regular file:" << fileName;
        return false;
    }

    // 2. Владелец файла и текущий пользователь процесса совпадают
    if (fileInfo.ownerId() != static_cast<uint>(geteuid())) {
        qDebug() << "[EE] File is not owned by the current user:" << fileName;
        return false;
    }

    // 3. Права не позволяют группе чтение, запись и исполнение файла
    const QFileDevice::Permissions groupPermissions = QFileDevice::ReadGroup |
                                                      QFileDevice::WriteGroup |
                                                      QFileDevice::ExeGroup;
    if ((fileInfo.permissions() & groupPermissions) != 0){
        qDebug() << "[EE] Group has access permissions:" << fileName;
        return false;
    }

    // 4. Права не позволяют чтение, запись и исполнение файла остальным
    const QFileDevice::Permissions otherPermissions = QFileDevice::ReadOther |
                                                      QFileDevice::WriteOther |
                                                      QFileDevice::ExeOther;
    if ((fileInfo.permissions() & otherPermissions) != 0){
        qDebug() << "[EE] Other users has access permissions:" << fileName;
        return false;
    }

    // Все ограничения пройдены успешно
    return true;
}

// End daemon.cpp
