#include "JoystickSerialPortManager.h"

JoystickSerialPortManager::JoystickSerialPortManager(QGCApplication *app, QGCToolbox *toolbox) : QGCTool(app, toolbox){
    mJoystickLink = new JoystickLink();
}

JoystickSerialPortManager::~JoystickSerialPortManager(){
    mJoystickSerialPortReadThread.quit();
    mJoystickSerialPortReadThread.wait();
    mJoystickSerialPortDetectThread.quit();
    mJoystickSerialPortDetectThread.wait();
}

void JoystickSerialPortManager::start(){
    mJoystickLink->moveToThread(&mJoystickSerialPortDetectThread);
    connect(&mJoystickSerialPortDetectThread, &QThread::started, mJoystickLink, &JoystickLink::detectJoystick);
    connect(&mJoystickSerialPortDetectThread, &QThread::finished, mJoystickLink, &QObject::deleteLater);

    mJoystickLink->moveToThread(&mJoystickSerialPortReadThread);
    connect(&mJoystickSerialPortReadThread, &QThread::started, mJoystickLink, &JoystickLink::sendJoystickCmd);
    connect(&mJoystickSerialPortReadThread, &QThread::finished, mJoystickLink, &QObject::deleteLater);

    mJoystickSerialPortDetectThread.start();
    mJoystickSerialPortReadThread.start();
}


