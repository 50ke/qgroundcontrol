#ifndef JOYSTICKLINK_H
#define JOYSTICKLINK_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>

class MqttManager;

class JoystickLink : public QObject
{
    Q_OBJECT
public:
    void detectJoystick();
    bool isJoystick(QSerialPort &serialPort);
    void sendJoystickCmd();
private:
    QSerialPort *mSerialPort;
};

#endif // JOYSTICKLINK_H
