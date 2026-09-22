// Begin msgtransport.cpp

#include "msgtransport.h"

// Из-за Qt пришлось делать базовый класс, но он уже не виртуальный,
// при линковке требуется конструктор. Последний можно было бы объявить
// и в заголовочном файле, но не хочу путаться, пусть будет отдельный файл
MsgTransport::MsgTransport(QObject *parent) : QObject(parent){
}


// End msgtransport.cpp
