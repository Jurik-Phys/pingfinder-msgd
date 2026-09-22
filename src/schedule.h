// Begin schedule.h

#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <QDate>
#include <QVector>

struct ScheduleTask {
    int     clientId;
    QString uuid;
    QString execTime;
    QString providedBy; // pingfinder-msgd | pingfinder-push | other-app
};

struct Schedule {
    QDate date;
    QVector<ScheduleTask> tasks;
};

#endif
// End schedule.h
