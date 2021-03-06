#include "SubtitleFilter.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>

SubtitleFilter::SubtitleFilter(QObject *parent)
    :  LibAVFilter(parent)
    , m_auto(true)
    , m_player(0)
{
    connect(this, SIGNAL(statusChanged()), SLOT(onStatusChanged()));
}

void SubtitleFilter::setPlayer(AVPlayer *player)
{
    if (m_player == player)
        return;
    uninstall();
    if (m_player) {
        disconnect(this);
    }
    m_player = player;
    if (!player)
        return;
    player->installVideoFilter(this);
    if (m_auto) {
//        connect(player, SIGNAL(fileChanged(QString)), SLOT(findAndSetFile(QString)));
        connect(player, SIGNAL(started()), SLOT(onPlayerStart()));
    }
}

bool SubtitleFilter::setFile(const QString &filePath)
{
    setOptions("");
    if (m_file != filePath) {
        emit fileChanged(filePath);
        // DO NOT return here because option is null now
    }
    m_file = filePath;
    QString u = m_u8_files[filePath];
    if (!u.isEmpty() && !QFile(u).exists())
        u = QString();
    if (u.isEmpty()) {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning("open '%s' error: %s", filePath.toUtf8().constData(), f.errorString().toUtf8().constData());
            return false;
        }
        QTextStream ts(&f);
        ts.setAutoDetectUnicode(true);
        QString s = ts.readAll();
        QString tmp = setContent(s);
        if (!tmp.isEmpty()) {
            u = tmp;
            m_u8_files[filePath] = u;
        } else {
            // read the origin file
            qWarning("open cache file '%s' error, originanl subtitle file will be used", filePath.toUtf8().constData());
        }
    }
    if (u.isEmpty())
        u = filePath;
    // filter_name=argument. use ' to quote the argument, use \ to escaping chars within quoted text. on windows, path can be C:/a/b/c, ":" must be escaped
    u.replace(":", "\\:");
    setOptions("subtitles='" + u + "'");
    qDebug("subtitle loaded: %s", filePath.toUtf8().constData());
    return true;
}

QString SubtitleFilter::file() const
{
    return m_file;
}

QString SubtitleFilter::setContent(const QString &doc)
{
    QString name = QFileInfo(m_file).fileName();
    if (name.isEmpty())
        name = "QtAV_u8_sub_cache";
    QFile w(QDir::temp().absoluteFilePath(name));
    if (w.open(QIODevice::WriteOnly)) {
        w.write(doc.toUtf8());
        w.close();
    } else {
        return QString();
    }
    return w.fileName();
}

void SubtitleFilter::setAutoLoad(bool value)
{
    if (m_auto == value)
        return;
    m_auto = value;
    emit autoLoadChanged(value);
    if (!m_player || !m_auto)
        return;
    connect(m_player, SIGNAL(started()), SLOT(onPlayerStart()));
}

bool SubtitleFilter::autoLoad() const
{
    return m_auto;
}

void SubtitleFilter::findAndSetFile(const QString &path)
{
    QFileInfo fi(path);
    QDir dir(fi.dir());
    QString name = fi.completeBaseName(); // video suffix has only 1 dot
    QStringList list = dir.entryList(QStringList() << name + "*.ass" << name + "*.ssa", QDir::Files);
    list.append(dir.entryList(QStringList() << "*.srt", QDir::Files));
    foreach (QString f, list) {
        // why it happens?
        if (!f.startsWith(name))
            continue;
        if (setFile(dir.absoluteFilePath(f)))
            break;
    }
}

void SubtitleFilter::onPlayerStart()
{
    setOptions("");
    if (!autoLoad())
        return;
    findAndSetFile(m_player->file());
}

void SubtitleFilter::onStatusChanged()
{
    if (status() == ConfigreOk) {
        emit loaded();
    } else if (status() == ConfigureFailed) {
        if (options().isEmpty())
            return;
        emit loadError();
    }
}
