#include "JoystickSerialPortManager.h"

JoystickSerialPortManager::JoystickSerialPortManager(QGCApplication *app, QGCToolbox *toolbox) : QGCTool(app, toolbox){}

JoystickSerialPortManager::~JoystickSerialPortManager(){
    mJoystickSerialPortReadThread.quit();
    mJoystickSerialPortReadThread.wait();
    mJoystickSerialPortDetectThread.quit();
    mJoystickSerialPortDetectThread.wait();
}

void JoystickSerialPortManager::autoDetect(){
    const auto serialPortInfos = QSerialPortInfo::availablePorts();
    QSerialPort serialPort;
    for (const QSerialPortInfo &portInfo : serialPortInfos) {
        qDebug() << "[JoystickSerialPortManager]\n"
                 << "Port:" << portInfo.portName() << "\n"
                 << "Location:" << portInfo.systemLocation() << "\n"
                 << "Description:" << portInfo.description() << "\n"
                 << "Manufacturer:" << portInfo.manufacturer() << "\n"
                 << "Serial number:" << portInfo.serialNumber() << "\n"
                 << "Vendor Identifier:"
                 << (portInfo.hasVendorIdentifier()
                         ? QByteArray::number(portInfo.vendorIdentifier(), 16)
                         : QByteArray()) << "\n"
                 << "Product Identifier:"
                 << (portInfo.hasProductIdentifier()
                         ? QByteArray::number(portInfo.productIdentifier(), 16)
                         : QByteArray());
        serialPort.setPort(portInfo);
        if(isJoystick(serialPort)){
            mSerialPort = &serialPort;
            // 发送串口更新信号
            QVariantMap newSerialPort;
            newSerialPort.insert("name", portInfo.portName());
            newSerialPort.insert("number", portInfo.serialNumber());
            emit updateJoystickSerialPort(newSerialPort);
            break;
        }
    }
}

bool JoystickSerialPortManager::isJoystick(QSerialPort &serialPort){
    if (!serialPort.open(QIODevice::ReadWrite)) {
        qDebug() << "[JoystickSerialPortManager]open serial port failed.";
    }
    if(serialPort.isOpen()){
        qint64 byteCount = serialPort.bytesAvailable();
        if (byteCount) {
            QByteArray buffer;
            buffer.resize(byteCount);
            serialPort.read(buffer.data(), buffer.size());
            // TODO 根据数据特征判断是否为遥控
        }
    }
    return false;
}

void JoystickSerialPortManager::doWork(){
    try {
        if(mSerialPort == nullptr){
            qDebug() << "[JoystickSerialPortManager]serial port not found.";
        }
        qint64 byteCount = mSerialPort->bytesAvailable();
        if (byteCount) {
            QByteArray buffer;
            buffer.resize(byteCount);
            mSerialPort->read(buffer.data(), buffer.size());
            _toolbox->mqttManager()->sendJoystickCmd(buffer);
        }
    } catch (...) {
        qCritical() << "ERROR: Caught unknown joystick exception";
    }
}

void JoystickSerialPortManager::start(){
    this->moveToThread(&mJoystickSerialPortDetectThread);
    connect(&mJoystickSerialPortDetectThread, &QThread::started, this, &JoystickSerialPortManager::autoDetect);
    connect(&mJoystickSerialPortDetectThread, &QThread::finished, this, &QObject::deleteLater);

    this->moveToThread(&mJoystickSerialPortReadThread);
    connect(&mJoystickSerialPortReadThread, &QThread::started, this, &JoystickSerialPortManager::doWork);
    connect(&mJoystickSerialPortReadThread, &QThread::finished, this, &QObject::deleteLater);

    mJoystickSerialPortDetectThread.start();
    mJoystickSerialPortReadThread.start();
}


