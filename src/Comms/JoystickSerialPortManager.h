#ifndef JOYSTICKSERIALPORTMANAGER_H
#define JOYSTICKSERIALPORTMANAGER_H

#include <QObject>
#include <QThread>
#include "QGCToolbox.h"
#include <QDateTime>
#include <QSerialPort>
#include <QSerialPortInfo>
#include "MqttManager.h"
#include "JoystickLink.h"

class JoystickSerialPortManager : public QGCTool
{
    Q_OBJECT
public:
    explicit JoystickSerialPortManager(QGCApplication *app, QGCToolbox *toolbox);
    ~JoystickSerialPortManager();
    void start();
signals:
    void updateJoystickSerialPort(const QVariantMap newSerialPort);
private:
    JoystickLink *mJoystickLink = nullptr;
    QThread mJoystickSerialPortReadThread;
    QThread mJoystickSerialPortDetectThread;
};

#endif // JOYSTICKSERIALPORTMANAGER_H
