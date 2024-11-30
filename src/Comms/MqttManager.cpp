#include "MqttManager.h"
#include "SettingsManager.h"
#include "QGCApplication.h"

MqttManager::MqttManager(QGCApplication *app, QGCToolbox *toolbox) : QGCTool(app, toolbox){
    mMqttLink = new MqttLink("mqtt://110.187.226.202:1883", "QGC");
}

MqttManager::~MqttManager(){
    mMqttLinkWorkThread.quit();
    mMqttLinkWorkThread.wait();
}

void MqttManager::start(){
    mMqttLink->moveToThread(&mMqttLinkWorkThread);
    connect(&mMqttLinkWorkThread, &QThread::started, mMqttLink, &MqttLink::start);
    connect(&mMqttLinkWorkThread, &QThread::finished, mMqttLink, &QObject::deleteLater);
    connect(mMqttLink, &MqttLink::notifyMessage, this, &MqttManager::handleMessage);
    mMqttLinkWorkThread.start();
}

void MqttManager::handleMessage(const QVariantMap message){
    // const QString hostName = _toolbox->settingsManager()->appSettings()->forwardMavlinkAPMSupportHostName()->rawValue().toString();
    // qDebug() << "============================: " << hostName;
    emit updateMessage(message);
}
