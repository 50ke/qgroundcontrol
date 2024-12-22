#include "mdkplayer.h"
#include <QDebug>

class VideoRendererInternal : public QQuickFramebufferObject::Renderer
{
public:
    VideoRendererInternal(MDK_NS::QmlMDKPlayer *r) {
        this->r = r;
    }

    void render() override {
        r->renderVideo();
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        r->setVideoSurfaceSize(size.width(), size.height());
        return new QOpenGLFramebufferObject(size);
    }

    MDK_NS::QmlMDKPlayer *r;
};


MDK_NS::QmlMDKPlayer::QmlMDKPlayer(QQuickItem *parent):
    QQuickFramebufferObject(parent),
    internal_player(new Player)
{
    qDebug() << "初始化流媒体播放器";
    setMirrorVertically(true);
    internal_player->setDecoders(MediaType::Video, {"MFT:d3d=11", "D3D11", "CUDA", "hap", "FFmpeg", "dav1d","dxva2","d3d11va"});
    internal_player->setProperty("avformat.fflags", "+nobuffer");
    internal_player->setProperty("avformat.fpsprobesize", "0");
}

MDK_NS::QmlMDKPlayer::~QmlMDKPlayer()
{
    delete internal_player;
}

QQuickFramebufferObject::Renderer *MDK_NS::QmlMDKPlayer::createRenderer() const
{
    return new VideoRendererInternal(const_cast<QmlMDKPlayer*>(this));
}

void MDK_NS::QmlMDKPlayer::play()
{
    qDebug() << "开启流媒体播放器===>" << m_source;
    internal_player->set(PlaybackState::Playing);
    internal_player->setRenderCallback([=](void *){
        QMetaObject::invokeMethod(this, "update");
    });
}

void MDK_NS::QmlMDKPlayer::destroy()
{
    delete internal_player;
}

void MDK_NS::QmlMDKPlayer::destroyPlayer()
{
    qDebug() << "销毁流媒体播放器===>" << m_source;
    delete internal_player;
}

void MDK_NS::QmlMDKPlayer::setPlaybackRate(float rate)
{
    internal_player->setPlaybackRate(rate);
}

void MDK_NS::QmlMDKPlayer::setVideoSurfaceSize(int width, int height)
{
    internal_player->setVideoSurfaceSize(width, height);
}

void MDK_NS::QmlMDKPlayer::renderVideo()
{
    internal_player->renderVideo();
}
