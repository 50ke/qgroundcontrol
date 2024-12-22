#include "MqttManager.h"
#include "QGCApplication.h"

MqttManager::MqttManager(QGCApplication *app, QGCToolbox *toolbox) : QGCTool(app, toolbox){
    loadConfig();
    mMqttLink = new MqttLink(mMqttServerAddr, mMqttSubTopic);
}

MqttManager::~MqttManager(){
    mMqttLinkWorkThread.quit();
    mMqttLinkWorkThread.wait();
}

void MqttManager::loadConfig(){
    QFile file(QStringLiteral("UGC-Config.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error("Cannot read QGC-Mqtt-Config.json");
    }
    QByteArray byteArray = file.readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(byteArray);
    if (jsonDoc.isNull() || !jsonDoc.isObject()){
        throw std::runtime_error("Failed to Parse QGC-Mqtt-Config.json");
    }
    QJsonObject jsonObject = jsonDoc.object();
    this->mMqttServerAddr = jsonObject.value("server").toString();
    this->mMqttSubTopic = jsonObject.value("subTopic").toString();
    this->mMqttPubTopic = jsonObject.value("pubTopic").toString();
    this->mVideoUrl = jsonObject.value("videoUrl").toString();
}

void MqttManager::start(){
    mMqttLink->moveToThread(&mMqttLinkWorkThread);
    connect(&mMqttLinkWorkThread, &QThread::started, mMqttLink, &MqttLink::start);
    connect(&mMqttLinkWorkThread, &QThread::finished, mMqttLink, &QObject::deleteLater);
    connect(mMqttLink, &MqttLink::notifyMessage, this, &MqttManager::handleMessage);
    mMqttLinkWorkThread.start();
}

void MqttManager::handleMessage(const QVariantMap message){
    if(message.contains("Lidar")){
        emit updateLidar(message);
    }else{
        emit updateMessage(message);
    }
}

void MqttManager::changeGear(int value){
    QVariantMap cmd;
    cmd.insert("Timestamp", QDateTime::currentMSecsSinceEpoch());
    cmd.insert("Gear", value);

    QJsonObject jsonObject = QJsonObject::fromVariantMap(cmd);
    QJsonDocument jsonDocument(jsonObject);
    QString jsonString = jsonDocument.toJson();
    mMqttLink->publishedMessage(mMqttPubTopic, jsonString);
}

QString MqttManager::getVideoUrl(){
    return this->mVideoUrl;
}
