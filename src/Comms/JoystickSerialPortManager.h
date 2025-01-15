#ifndef JOYSTICKSERIALPORTMANAGER_H
#define JOYSTICKSERIALPORTMANAGER_H

#include <QObject>
#include <QThread>
#include "QGCToolbox.h"
#include <QDateTime>
#include <QSerialPort>
#include <QSerialPortInfo>
#include "MqttManager.h"

class JoystickSerialPortManager : public QGCTool
{
    Q_OBJECT
public:
    explicit JoystickSerialPortManager(QGCApplication *app, QGCToolbox *toolbox);
    ~JoystickSerialPortManager();
    void start();
    void autoDetect();
    bool isJoystick(QSerialPort &serialPort);
    void doWork();
signals:
    void updateJoystickSerialPort(const QVariantMap newSerialPort);
private:
    QThread mJoystickSerialPortReadThread;
    QThread mJoystickSerialPortDetectThread;
    QSerialPort *mSerialPort = nullptr;
};

#endif // JOYSTICKSERIALPORTMANAGER_H
