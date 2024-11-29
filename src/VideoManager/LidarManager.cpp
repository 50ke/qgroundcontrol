/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "LidarManager.h"
#include "MultiVehicleManager.h"
#include "QGCApplication.h"
#include "QGCCameraManager.h"
#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "QGCToolbox.h"
#include "SettingsManager.h"
#include "SubtitleWriter.h"
#include "Vehicle.h"
#include "VideoReceiver.h"
#include "VideoSettings.h"
#ifdef QGC_GST_STREAMING
#include "GStreamer.h"
#else
#include "GLVideoItemStub.h"
#endif
#ifdef QGC_QT_STREAMING
#include "QtMultimediaReceiver.h"
// #include "UVCReceiver.h"
#endif

#ifndef QGC_DISABLE_UVC
#include <QtCore/QPermissions>
#include <QtMultimedia/QCameraDevice>
#include <QtMultimedia/QMediaDevices>
#include <QtQuick/QQuickWindow>
#endif

#include <QtCore/qapplicationstatic.h>
#include <QtCore/QDir>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>

#include "mdkplayer.h"

QGC_LOGGING_CATEGORY(LidarManagerLog, "qgc.lidarmanager.lidarmanager")

static constexpr const char *kFileExtension[VideoReceiver::FILE_FORMAT_MAX - VideoReceiver::FILE_FORMAT_MIN] = {
    "mkv",
    "mov",
    "mp4"
};

Q_APPLICATION_STATIC(LidarManager, _lidarManagerInstance);

LidarManager::LidarManager(QObject *parent)
    : QObject(parent)
    , _subtitleWriter(new SubtitleWriter(this))
    , _videoSettings(qgcApp()->toolbox()->settingsManager()->videoSettings())
{
    // qCDebug(LidarManagerLog) << Q_FUNC_INFO << this;

    if (qgcApp()->runningUnitTests()) {
        return;
    }

    (void) qmlRegisterUncreatableType<LidarManager> ("QGroundControl.LidarManager", 1, 0, "LidarManager", "Reference only");
    (void) qmlRegisterUncreatableType<VideoReceiver>("QGroundControl", 1, 0, "VideoReceiver","Reference only");
    (void) qmlRegisterType<MDK_NS::QmlMDKPlayer>("MDKPlayer", 1, 0, "MDKPlayer");

#ifdef QGC_GST_STREAMING
    GStreamer::initialize();
    GStreamer::blacklist(static_cast<GStreamer::VideoDecoderOptions>(_videoSettings->forceVideoDecoder()->rawValue().toInt()));
#else
    (void) qmlRegisterType<GLVideoItemStub>("org.freedesktop.gstreamer.Qt6GLVideoItem", 1, 0, "GstGLQt6VideoItem");
#endif
}

LidarManager::~LidarManager()
{
    for (VideoReceiverData &videoReceiver : _videoReceiverData) {
        if (videoReceiver.receiver != nullptr) {
            delete videoReceiver.receiver;
            videoReceiver.receiver = nullptr;
        }

        if (videoReceiver.sink != nullptr) {
            // qgcApp()->toolbox()->corePlugin()->releaseVideoSink(videoReceiver.sink);
#ifdef QGC_GST_STREAMING
            // FIXME: AV: we need some interaface for video sink with .release() call
            // Currently LidarManager is destroyed after corePlugin() and we are crashing on app exit
            // calling _toolbox->corePlugin()->releaseVideoSink(_videoSink[i]);
            // As for now let's call GStreamer::releaseVideoSink() directly
            GStreamer::releaseVideoSink(videoReceiver.sink);
#elif defined(QGC_QT_STREAMING)
            QtMultimediaReceiver::releaseVideoSink(videoReceiver.sink);
#endif
        }
    }

    // qCDebug(LidarManagerLog) << Q_FUNC_INFO << this;
}

LidarManager *LidarManager::instance()
{
    return _lidarManagerInstance();
}

void LidarManager::init()
{
    if (_initialized) {
        return;
    }

    // TODO: Those connections should be Per Video, not per LidarManager.
    (void) connect(_videoSettings->videoSource(), &Fact::rawValueChanged, this, &LidarManager::_videoSourceChanged);
    (void) connect(_videoSettings->udpPort(), &Fact::rawValueChanged, this, &LidarManager::_videoSourceChanged);
    (void) connect(_videoSettings->rtspUrl(), &Fact::rawValueChanged, this, &LidarManager::_videoSourceChanged);
    (void) connect(_videoSettings->tcpUrl(), &Fact::rawValueChanged, this, &LidarManager::_videoSourceChanged);
    (void) connect(_videoSettings->aspectRatio(), &Fact::rawValueChanged, this, &LidarManager::aspectRatioChanged);
    (void) connect(_videoSettings->lowLatencyMode(), &Fact::rawValueChanged, this, &LidarManager::_lowLatencyModeChanged);
    (void) connect(qgcApp()->toolbox()->multiVehicleManager(), &MultiVehicleManager::activeVehicleChanged, this, &LidarManager::_setActiveVehicle);

    int index = 0;
    for (VideoReceiverData &videoReceiver : _videoReceiverData) {
        videoReceiver.index = index++;
        videoReceiver.receiver = qgcApp()->toolbox()->corePlugin()->createVideoReceiver(this);
        if (!videoReceiver.receiver) {
            continue;
        }

        (void) connect(videoReceiver.receiver, &VideoReceiver::onStartComplete, this, [this, &videoReceiver](VideoReceiver::STATUS status) {
            qCDebug(LidarManagerLog) << "Video" << videoReceiver.index << "Start complete, status:" << status;
            switch (status) {
            case VideoReceiver::STATUS_OK:
                videoReceiver.started = true;
                if (videoReceiver.sink != nullptr) {
                    videoReceiver.receiver->startDecoding(videoReceiver.sink);
                }
                break;
            case VideoReceiver::STATUS_INVALID_URL:
            case VideoReceiver::STATUS_INVALID_STATE:
                break;
            default:
                _restartVideo(videoReceiver.index);
                break;
            }
        });

        (void) connect(videoReceiver.receiver, &VideoReceiver::onStopComplete, this, [this, &videoReceiver](VideoReceiver::STATUS status) {
            qCDebug(LidarManagerLog) << "Video" << videoReceiver.index << "Stop complete, status:" << status;
            videoReceiver.started = false;
            if (status == VideoReceiver::STATUS_INVALID_URL) {
                qCDebug(LidarManagerLog) << "Invalid video URL. Not restarting";
            } else {
                _startReceiver(videoReceiver.index);
            }
        });

        // TODO: Create status variables for each receiver in VideoReceiverData
        (void) connect(videoReceiver.receiver, &VideoReceiver::streamingChanged, this, [this, &videoReceiver](bool active) {
            qCDebug(LidarManagerLog) << "Video" << videoReceiver.index << "streaming changed, active:" << (active ? "yes" : "no");
            if (videoReceiver.index == 0) {
                _streaming = active;
                emit streamingChanged();
            }
        });

        (void) connect(videoReceiver.receiver, &VideoReceiver::decodingChanged, this, [this, &videoReceiver](bool active) {
            qCDebug(LidarManagerLog) << "Video" << videoReceiver.index << "decoding changed, active:" << (active ? "yes" : "no");
            if (videoReceiver.index == 0) {
                _decoding = active;
                emit decodingChanged();
            }
        });

        (void) connect(videoReceiver.receiver, &VideoReceiver::recordingChanged, this, [this, &videoReceiver](bool active) {
            qCDebug(LidarManagerLog) << "Video" << videoReceiver.index << "recording changed, active:" << (active ? "yes" : "no");
            if (videoReceiver.index == 0) {
                _recording = active;
                if (!active) {
                    _subtitleWriter->stopCapturingTelemetry();
                }
                emit recordingChanged();
            }
        });

        (void) connect(videoReceiver.receiver, &VideoReceiver::recordingStarted, this, [this, &videoReceiver]() {
            qCDebug(LidarManagerLog) << "Video" << videoReceiver.index << "recording started";
            if (videoReceiver.index == 0) {
                _subtitleWriter->startCapturingTelemetry(_videoFile);
            }
        });

        (void) connect(videoReceiver.receiver, &VideoReceiver::videoSizeChanged, this, [this, &videoReceiver](QSize size) {
            qCDebug(LidarManagerLog) << "Video" << videoReceiver.index << "resized. New resolution:" << size.width() << "x" << size.height();
            if (videoReceiver.index == 0) {
                _videoSize = (static_cast<quint32>(size.width()) << 16) | static_cast<quint32>(size.height());
                emit videoSizeChanged();
            }
        });

        (void) connect(videoReceiver.receiver, &VideoReceiver::onTakeScreenshotComplete, this, [this, &videoReceiver](VideoReceiver::STATUS status) {
            if (status == VideoReceiver::STATUS_OK) {
                qCDebug(LidarManagerLog) << "Video" << videoReceiver.index << "screenshot taken";
            } else {
                qCWarning(LidarManagerLog) << "Video" << videoReceiver.index << "screenshot failed";
            }
        });
    }

    _videoSourceChanged();

    startVideo();

    QQuickWindow *const rootWindow = qgcApp()->mainRootWindow();
    if (rootWindow) {
        rootWindow->scheduleRenderJob(new FinishLidarInitialization(), QQuickWindow::BeforeSynchronizingStage);
    }

    _initialized = true;
}

void LidarManager::startVideo()
{
    if (!_videoSettings->streamEnabled()->rawValue().toBool() || !hasVideo()) {
        qCDebug(LidarManagerLog) << "Stream not enabled/configured";
        return;
    }

    for (VideoReceiverData &videoReceiver : _videoReceiverData) {
        _startReceiver(videoReceiver.index);
    }
}

void LidarManager::stopVideo()
{
    for (VideoReceiverData &videoReceiver : _videoReceiverData) {
        _stopReceiver(videoReceiver.index);
    }
}

void LidarManager::startRecording(const QString &videoFile)
{
    const VideoReceiver::FILE_FORMAT fileFormat = static_cast<VideoReceiver::FILE_FORMAT>(_videoSettings->recordingFormat()->rawValue().toInt());
    if (fileFormat < VideoReceiver::FILE_FORMAT_MIN || fileFormat >= VideoReceiver::FILE_FORMAT_MAX) {
        qgcApp()->showAppMessage(tr("Invalid video format defined."));
        return;
    }

    _cleanupOldVideos();

    const QString savePath = qgcApp()->toolbox()->settingsManager()->appSettings()->videoSavePath();
    if (savePath.isEmpty()) {
        qgcApp()->showAppMessage(tr("Unabled to record video. Video save path must be specified in Settings."));
        return;
    }

    const QString videoFileUrl = videoFile.isEmpty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd_hh.mm.ss") : videoFile;
    const QString ext = kFileExtension[fileFormat - VideoReceiver::FILE_FORMAT_MIN];

    const QString videoFile1 = savePath + "/" + videoFileUrl + "." + ext;
    const QString videoFile2 = savePath + "/" + videoFileUrl + ".2." + ext;

    _videoFile = videoFile1;

    const QStringList videoFiles = {videoFile1, videoFile2};
    for (VideoReceiverData &videoReceiver : _videoReceiverData) {
        if (videoReceiver.receiver && videoReceiver.started) {
            videoReceiver.receiver->startRecording(videoFiles.at(videoReceiver.index), fileFormat);
        } else {
            qCDebug(LidarManagerLog) << "Video receiver is not ready.";
        }
    }
}

void LidarManager::stopRecording()
{
    for (VideoReceiverData &videoReceiver : _videoReceiverData) {
        videoReceiver.receiver->stopRecording();
    }
}

void LidarManager::grabImage(const QString &imageFile)
{
    if (imageFile.isEmpty()) {
        _imageFile = qgcApp()->toolbox()->settingsManager()->appSettings()->photoSavePath();
        _imageFile += "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh.mm.ss.zzz") + ".jpg";
    } else {
        _imageFile = imageFile;
    }

    emit imageFileChanged();

    _videoReceiverData[0].receiver->takeScreenshot(_imageFile);
}

double LidarManager::aspectRatio() const
{
    if (_activeVehicle && _activeVehicle->cameraManager()) {
        const QGCVideoStreamInfo* const pInfo = _activeVehicle->cameraManager()->currentStreamInstance();
        if (pInfo) {
            qCDebug(LidarManagerLog) << "Primary AR:" << pInfo->aspectRatio();
            return pInfo->aspectRatio();
        }
    }

    // FIXME: AV: use _videoReceiver->videoSize() to calculate AR (if AR is not specified in the settings?)
    return _videoSettings->aspectRatio()->rawValue().toDouble();
}

double LidarManager::thermalAspectRatio() const
{
    if (_activeVehicle && _activeVehicle->cameraManager()) {
        const QGCVideoStreamInfo* const pInfo = _activeVehicle->cameraManager()->thermalStreamInstance();
        if (pInfo) {
            qCDebug(LidarManagerLog) << "Thermal AR:" << pInfo->aspectRatio();
            return pInfo->aspectRatio();
        }
    }

    return 1.0;
}

double LidarManager::hfov() const
{
    if (_activeVehicle && _activeVehicle->cameraManager()) {
        const QGCVideoStreamInfo* const pInfo = _activeVehicle->cameraManager()->currentStreamInstance();
        if (pInfo) {
            return pInfo->hfov();
        }
    }

    return 1.0;
}

double LidarManager::thermalHfov() const
{
    if (_activeVehicle && _activeVehicle->cameraManager()) {
        const QGCVideoStreamInfo* const pInfo = _activeVehicle->cameraManager()->thermalStreamInstance();
        if (pInfo) {
            return pInfo->aspectRatio();
        }
    }

    return _videoSettings->aspectRatio()->rawValue().toDouble();
}

bool LidarManager::hasThermal() const
{
    if (_activeVehicle && _activeVehicle->cameraManager()) {
        const QGCVideoStreamInfo* const pInfo = _activeVehicle->cameraManager()->thermalStreamInstance();
        if (pInfo) {
            return true;
        }
    }

    return false;
}

bool LidarManager::autoStreamConfigured() const
{
    if (_activeVehicle && _activeVehicle->cameraManager()) {
        const QGCVideoStreamInfo* const pInfo = _activeVehicle->cameraManager()->currentStreamInstance();
        if (pInfo) {
            return !pInfo->uri().isEmpty();
        }
    }

    return false;
}

bool LidarManager::hasVideo() const
{
    return (autoStreamConfigured() || _videoSettings->streamConfigured());
}

bool LidarManager::isStreamSource() const
{
    static const QStringList videoSourceList = {
        VideoSettings::videoSourceUDPH264,
        VideoSettings::videoSourceUDPH265,
        VideoSettings::videoSourceRTSP,
        VideoSettings::videoSourceTCP,
        VideoSettings::videoSourceMPEGTS,
        VideoSettings::videoSource3DRSolo,
        VideoSettings::videoSourceParrotDiscovery,
        VideoSettings::videoSourceYuneecMantisG,
        VideoSettings::videoSourceHerelinkAirUnit,
        VideoSettings::videoSourceHerelinkHotspot,
    };
    const QString videoSource = _videoSettings->videoSource()->rawValue().toString();
    return videoSourceList.contains(videoSource) || autoStreamConfigured();
}

bool LidarManager::isUvc() const
{
    return (uvcEnabled() && (hasVideo() && !_uvcVideoSourceID.isEmpty()));
}

bool LidarManager::gstreamerEnabled() const
{
#ifdef QGC_GST_STREAMING
    return true;
#else
    return false;
#endif
}

bool LidarManager::uvcEnabled() const
{
#ifndef QGC_DISABLE_UVC
    return !QMediaDevices::videoInputs().isEmpty();
#else
    return false;
#endif
}

bool LidarManager::qtmultimediaEnabled() const
{
#ifdef QGC_QT_STREAMING
    return true;
#else
    return false;
#endif
}

void LidarManager::setfullScreen(bool on)
{
    if (on) {
        if (!_activeVehicle || _activeVehicle->vehicleLinkManager()->communicationLost()) {
            on = false;
        }
    }

    if (on != _fullScreen) {
        _fullScreen = on;
        emit fullScreenChanged();
    }
}

void LidarManager::_initVideo()
{
    QQuickWindow *const root = qgcApp()->mainRootWindow();
    if (root == nullptr) {
        qCDebug(LidarManagerLog) << "mainRootWindow() failed. No root window";
        return;
    }

    const QStringList widgetTypes = {"lidarContent", "lidarThermalVideo"};
    for (VideoReceiverData &videoReceiver : _videoReceiverData) {
        QQuickItem* const widget = root->findChild<QQuickItem*>(widgetTypes.at(videoReceiver.index));
        if ((widget != nullptr) && (videoReceiver.receiver != nullptr)) {
            videoReceiver.sink = qgcApp()->toolbox()->corePlugin()->createVideoSink(this, widget);
            if (videoReceiver.sink != nullptr) {
                if (videoReceiver.started) {
                    videoReceiver.receiver->startDecoding(videoReceiver.sink);
                }
            } else {
                qCDebug(LidarManagerLog) << "createVideoSink() failed" << videoReceiver.index;
            }
        } else {
            qCDebug(LidarManagerLog) << widgetTypes.at(videoReceiver.index) << "receiver disabled";
        }
    }
}

void LidarManager::_cleanupOldVideos()
{
    if (!qgcApp()->toolbox()->settingsManager()->videoSettings()->enableStorageLimit()->rawValue().toBool()) {
        return;
    }

    const QString savePath = qgcApp()->toolbox()->settingsManager()->appSettings()->videoSavePath();
    QDir videoDir = QDir(savePath);
    videoDir.setFilter(QDir::Files | QDir::Readable | QDir::NoSymLinks | QDir::Writable);
    videoDir.setSorting(QDir::Time);

    QStringList nameFilters;
    for (size_t i = 0; i < std::size(kFileExtension); i++) {
        nameFilters << QStringLiteral("*.") + kFileExtension[i];
    }

    videoDir.setNameFilters(nameFilters);
    QFileInfoList vidList = videoDir.entryInfoList();
    if (!vidList.isEmpty()) {
        uint64_t total = 0;
        for (int i = 0; i < vidList.size(); i++) {
            total += vidList[i].size();
        }

        const uint64_t maxSize = qgcApp()->toolbox()->settingsManager()->videoSettings()->maxVideoSize()->rawValue().toUInt() * qPow(1024, 2);
        while ((total >= maxSize) && !vidList.isEmpty()) {
            total -= vidList.last().size();
            qCDebug(LidarManagerLog) << "Removing old video file:" << vidList.last().filePath();
            QFile file(vidList.last().filePath());
            (void) file.remove();
            vidList.removeLast();
        }
    }
}

void LidarManager::_videoSourceChanged()
{
    for (const VideoReceiverData &videoReceiver : _videoReceiverData) {
        (void) _updateSettings(videoReceiver.index);
    }

    emit hasVideoChanged();
    emit isStreamSourceChanged();
    emit isUvcChanged();
    emit isAutoStreamChanged();

    if (hasVideo()) {
        _restartAllVideos();
    } else {
        stopVideo();
    }

    qCDebug(LidarManagerLog) << "New Video Source:" << _videoSettings->videoSource()->rawValue().toString();
}

bool LidarManager::_updateUVC()
{
    bool result = false;

#ifndef QGC_DISABLE_UVC
    const QString oldUvcVideoSrcID = _uvcVideoSourceID;
    if (!hasVideo() || isStreamSource()) {
        _uvcVideoSourceID = "";
    } else {
        const QString videoSource = _videoSettings->videoSource()->rawValue().toString();
        const QList<QCameraDevice> videoInputs = QMediaDevices::videoInputs();
        for (const auto& cameraDevice: videoInputs) {
            if (cameraDevice.description() == videoSource) {
                _uvcVideoSourceID = cameraDevice.description();
                qCDebug(LidarManagerLog) << "Found USB source:" << _uvcVideoSourceID << " Name:" << videoSource;
                break;
            }
        }
    }

    if (oldUvcVideoSrcID != _uvcVideoSourceID) {
        qCDebug(LidarManagerLog) << "UVC changed from [" << oldUvcVideoSrcID << "] to [" << _uvcVideoSourceID << "]";
        const QCameraPermission cameraPermission;
        if (qgcApp()->checkPermission(cameraPermission) == Qt::PermissionStatus::Undetermined) {
            qgcApp()->requestPermission(cameraPermission, [this](const QPermission &permission) {
                if (permission.status() == Qt::PermissionStatus::Granted) {
                    qgcApp()->showRebootAppMessage(tr("Restart application for changes to take effect."));
                }
            });
        }
        result = true;
        emit uvcVideoSourceIDChanged();
        emit isUvcChanged();
    }
#endif

    return result;
}

bool LidarManager::_updateAutoStream(unsigned id)
{
    if (!_activeVehicle || !_activeVehicle->cameraManager()) {
        return false;
    }

    const QGCVideoStreamInfo* const pInfo = _activeVehicle->cameraManager()->currentStreamInstance();
    if (!pInfo) {
        return false;
    }

    bool settingsChanged = false;
    if (id == 0) {
        qCDebug(LidarManagerLog) << "Configure primary stream:" << pInfo->uri();
        switch(pInfo->type()) {
        case VIDEO_STREAM_TYPE_RTSP:
            settingsChanged = _updateVideoUri(id, pInfo->uri());
            if (settingsChanged) {
                qgcApp()->toolbox()->settingsManager()->videoSettings()->videoSource()->setRawValue(VideoSettings::videoSourceRTSP);
            }
            break;
        case VIDEO_STREAM_TYPE_TCP_MPEG:
            settingsChanged = _updateVideoUri(id, pInfo->uri());
            if (settingsChanged) {
                qgcApp()->toolbox()->settingsManager()->videoSettings()->videoSource()->setRawValue(VideoSettings::videoSourceTCP);
            }
            break;
        case VIDEO_STREAM_TYPE_RTPUDP: {
            const QString url = pInfo->uri().contains("udp://") ? pInfo->uri() : QStringLiteral("udp://0.0.0.0:%1").arg(pInfo->uri());
            settingsChanged = _updateVideoUri(id, url);
            if (settingsChanged) {
                qgcApp()->toolbox()->settingsManager()->videoSettings()->videoSource()->setRawValue(VideoSettings::videoSourceUDPH264);
            }
            break;
        }
        case VIDEO_STREAM_TYPE_MPEG_TS:
            settingsChanged = _updateVideoUri(id, QStringLiteral("mpegts://0.0.0.0:%1").arg(pInfo->uri()));
            if (settingsChanged) {
                qgcApp()->toolbox()->settingsManager()->videoSettings()->videoSource()->setRawValue(VideoSettings::videoSourceMPEGTS);
            }
            break;
        default:
            settingsChanged = _updateVideoUri(id, pInfo->uri());
            break;
        }
    } else if (id == 1) {
        const QGCVideoStreamInfo* const pTinfo = _activeVehicle->cameraManager()->thermalStreamInstance();
        if (pTinfo) {
            qCDebug(LidarManagerLog) << "Configure secondary stream:" << pTinfo->uri();
            switch(pTinfo->type()) {
            case VIDEO_STREAM_TYPE_RTSP:
            case VIDEO_STREAM_TYPE_TCP_MPEG:
                settingsChanged = _updateVideoUri(id, pTinfo->uri());
                break;
            case VIDEO_STREAM_TYPE_RTPUDP:
                settingsChanged = _updateVideoUri(id, QStringLiteral("udp://0.0.0.0:%1").arg(pTinfo->uri()));
                break;
            case VIDEO_STREAM_TYPE_MPEG_TS:
                settingsChanged = _updateVideoUri(id, QStringLiteral("mpegts://0.0.0.0:%1").arg(pTinfo->uri()));
                break;
            default:
                settingsChanged = _updateVideoUri(id, pTinfo->uri());
                break;
            }
        }
    }

    return settingsChanged;
}

bool LidarManager::_updateSettings(unsigned id)
{
    if (!_videoSettings) {
        return false;
    }

    if (id > (_videoReceiverData.size() - 1)) {
        qCDebug(LidarManagerLog) << "Unsupported receiver id" << id;
        return false;
    }

    bool settingsChanged = false;

    const bool lowLatencyStreaming = _videoSettings->lowLatencyMode()->rawValue().toBool();
    if (lowLatencyStreaming != _videoReceiverData[id].lowLatencyStreaming) {
        _videoReceiverData[id].lowLatencyStreaming = lowLatencyStreaming;
        settingsChanged = true;
    }

    settingsChanged |= _updateUVC();

    if (_activeVehicle && _activeVehicle->cameraManager()) {
        const QGCVideoStreamInfo* const pInfo = _activeVehicle->cameraManager()->currentStreamInstance();
        if (pInfo) {
            settingsChanged |= _updateAutoStream(id);
            return settingsChanged;
        }
    }

    if (id == 0) {
        const QString source = _videoSettings->videoSource()->rawValue().toString();
        if (source == VideoSettings::videoSourceUDPH264) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("udp://0.0.0.0:%1").arg(_videoSettings->udpPort()->rawValue().toInt()));
        } else if (source == VideoSettings::videoSourceUDPH265) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("udp265://0.0.0.0:%1").arg(_videoSettings->udpPort()->rawValue().toInt()));
        } else if (source == VideoSettings::videoSourceMPEGTS) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("mpegts://0.0.0.0:%1").arg(_videoSettings->udpPort()->rawValue().toInt()));
        } else if (source == VideoSettings::videoSourceRTSP) {
            settingsChanged |= _updateVideoUri(id, _videoSettings->rtspUrl()->rawValue().toString());
        } else if (source == VideoSettings::videoSourceTCP) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("tcp://%1").arg(_videoSettings->tcpUrl()->rawValue().toString()));
        } else if (source == VideoSettings::videoSource3DRSolo) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("udp://0.0.0.0:5600"));
        } else if (source == VideoSettings::videoSourceParrotDiscovery) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("udp://0.0.0.0:8888"));
        } else if (source == VideoSettings::videoSourceYuneecMantisG) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("rtsp://192.168.42.1:554/live"));
        } else if (source == VideoSettings::videoSourceHerelinkAirUnit) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("rtsp://192.168.0.10:8554/H264Video"));
        } else if (source == VideoSettings::videoSourceHerelinkHotspot) {
            settingsChanged |= _updateVideoUri(id, QStringLiteral("rtsp://192.168.43.1:8554/fpv_stream"));
        } else if (source == VideoSettings::videoDisabled || source == VideoSettings::videoSourceNoVideo) {
            settingsChanged |= _updateVideoUri(id, "");
        } else {
            settingsChanged |= _updateVideoUri(id, "");
            if (!isUvc()) {
                qCCritical(LidarManagerLog) << "Video source URI \"" << source << "\" is not supported. Please add support!";
            }
        }
    }

    return settingsChanged;
}

bool LidarManager::_updateVideoUri(unsigned id, const QString &uri)
{
    if (id > (_videoReceiverData.size() - 1)) {
        qCDebug(LidarManagerLog) << "Unsupported receiver id" << id;
        return false;
    }

    if (uri == _videoReceiverData[id].uri) {
        return false;
    }

    qCDebug(LidarManagerLog) << "New Video URI" << uri;

    _videoReceiverData[id].uri = uri;

    return true;
}

void LidarManager::_restartVideo(unsigned id)
{
    if (id > (_videoReceiverData.size() - 1)) {
        qCDebug(LidarManagerLog) << "Unsupported receiver id" << id;
        return;
    }

    qCDebug(LidarManagerLog) << "Restart video streaming" << id;

    if (_videoReceiverData[id].started) {
        _stopReceiver(id);
    }

    _startReceiver(id);
}

void LidarManager::_restartAllVideos()
{
    for (const VideoReceiverData &videoReceiver : _videoReceiverData) {
        _restartVideo(videoReceiver.index);
    }
}

void LidarManager::_startReceiver(unsigned id)
{
    if (id > (_videoReceiverData.size() - 1)) {
        qCDebug(LidarManagerLog) << "Unsupported receiver id" << id;
        return;
    }

    if (_videoReceiverData[id].receiver == nullptr) {
        qCDebug(LidarManagerLog) << "VideoReceiver is NULL" << id;
        return;
    }

    if (_videoReceiverData[id].uri.isEmpty()) {
        qCDebug(LidarManagerLog) << "VideoUri is NULL" << id;
        return;
    }

    const QString source = _videoSettings->videoSource()->rawValue().toString();
    const unsigned rtsptimeout = _videoSettings->rtspTimeout()->rawValue().toUInt();
    /* The gstreamer rtsp source will switch to tcp if udp is not available after 5 seconds.
       So we should allow for some negotiation time for rtsp */
    const unsigned timeout = (source == VideoSettings::videoSourceRTSP ? rtsptimeout : 2);

    _videoReceiverData[id].receiver->start(_videoReceiverData[id].uri, timeout, _videoReceiverData[id].lowLatencyStreaming ? -1 : 0);
}

void LidarManager::_stopReceiver(unsigned id)
{
    if (id > (_videoReceiverData.size() - 1)) {
        qCDebug(LidarManagerLog) << "Unsupported receiver id" << id;
        return;
    }

    if (_videoReceiverData[id].receiver == nullptr) {
        qCDebug(LidarManagerLog) << "VideoReceiver is NULL" << id;
        return;
    }

    _videoReceiverData[id].receiver->stop();
}

void LidarManager::_setActiveVehicle(Vehicle *vehicle)
{
    if (_activeVehicle) {
        (void) disconnect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, &LidarManager::_communicationLostChanged);
        if (_activeVehicle->cameraManager()) {
            MavlinkCameraControl *const pCamera = _activeVehicle->cameraManager()->currentCameraInstance();
            if (pCamera) {
                pCamera->stopStream();
            }
            (void) disconnect(_activeVehicle->cameraManager(), &QGCCameraManager::streamChanged, this, &LidarManager::_restartAllVideos);
        }
    }

    _activeVehicle = vehicle;
    if (_activeVehicle) {
        (void) connect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, &LidarManager::_communicationLostChanged);
        if (_activeVehicle->cameraManager()) {
            (void) connect(_activeVehicle->cameraManager(), &QGCCameraManager::streamChanged, this, &LidarManager::_restartAllVideos);
            MavlinkCameraControl *const pCamera = _activeVehicle->cameraManager()->currentCameraInstance();
            if (pCamera) {
                pCamera->resumeStream();
            }
        }
    } else {
        setfullScreen(false);
    }

    emit autoStreamConfiguredChanged();
    _restartAllVideos();
}

void LidarManager::_communicationLostChanged(bool connectionLost)
{
    if (connectionLost) {
        setfullScreen(false);
    }
}

/*===========================================================================*/

FinishLidarInitialization::FinishLidarInitialization()
    : QRunnable()
{
    // qCDebug(LidarManagerLog) << Q_FUNC_INFO << this;
}

FinishLidarInitialization::~FinishLidarInitialization()
{
    // qCDebug(LidarManagerLog) << Q_FUNC_INFO << this;
}

void FinishLidarInitialization::run()
{
    LidarManager::instance()->_initVideo();
}
