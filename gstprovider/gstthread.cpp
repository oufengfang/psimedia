/*
 * Copyright (C) 2008  Barracuda Networks, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 *
 */

#include "gstthread.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QMutex>
#include <QQueue>
#include <QStringList>
#include <QWaitCondition>
//#include <QStyle>
#include "gstelements/static/gstelements.h"
#include <QIcon>
#include <gst/gst.h>

namespace PsiMedia {

//----------------------------------------------------------------------------
// GstSession
//----------------------------------------------------------------------------
// converts Qt-ified commandline args back to C style
class CArgs {
public:
    int    argc;
    char **argv;

    CArgs()
    {
        argc  = 0;
        argv  = nullptr;
        count = 0;
        data  = nullptr;
    }

    ~CArgs()
    {
        if (count > 0) {
            for (int n = 0; n < count; ++n)
                delete[] data[n];
            free(argv);
            free(data);
        }
    }

    void set(const QStringList &args)
    {
        count = args.count();
        if (count == 0) {
            data = nullptr;
            argc = 0;
            argv = nullptr;
        } else {
            data = static_cast<char **>(malloc(sizeof(char *) * quintptr(count)));
            argv = static_cast<char **>(malloc(sizeof(char *) * quintptr(count)));
            for (int n = 0; n < count; ++n) {
                QByteArray cs = args[n].toLocal8Bit();
                data[n]       = static_cast<char *>(qstrdup(cs.data()));
                argv[n]       = data[n];
            }
            argc = count;
        }
    }

private:
    int    count;
    char **data;
};

static void loadPlugins(const QString &pluginPath, bool print = false)
{
    if (print)
        qDebug("Loading plugins in [%s]\n", qPrintable(pluginPath));
    QDir        dir(pluginPath);
    QStringList entryList = dir.entryList(QDir::Files);
    foreach (QString entry, entryList) {
        if (!QLibrary::isLibrary(entry))
            continue;
        QString    filePath = dir.filePath(entry);
        GError *   err      = nullptr;
        GstPlugin *plugin   = gst_plugin_load_file(filePath.toUtf8().data(), &err);
        if (!plugin) {
            if (print) {
                qDebug("**FAIL**: %s: %s\n", qPrintable(entry), err->message);
            }
            g_error_free(err);
            continue;
        }
        if (print) {
            qDebug("   OK   : %s name=[%s]\n", qPrintable(entry), gst_plugin_get_name(plugin));
        }
        gst_object_unref(plugin);
    }

    if (print)
        qDebug("\n");
}

static int compare_gst_version(uint a1, uint a2, uint a3, uint b1, uint b2, uint b3)
{
    if (a1 > b1)
        return 1;
    else if (a1 < b1)
        return -1;

    if (a2 > b2)
        return 1;
    else if (a2 < b2)
        return -1;

    if (a3 > b3)
        return 1;
    else if (a3 < b3)
        return -1;

    return 0;
}

class GstSession {
public:
    CArgs   args;
    QString version;
    bool    success;

    explicit GstSession(const QString &pluginPath = QString())
    {
        args.set(QCoreApplication::instance()->arguments());

        // ignore "system" plugins
        if (!pluginPath.isEmpty()) {
            qputenv("GST_PLUGIN_SYSTEM_PATH", pluginPath.toLocal8Bit()); // not sure about windows
            // qputenv("GST_PLUGIN_PATH", "");
        }

        // you can also use NULLs here if you don't want to pass args
        GError *error;
        if (!gst_init_check(&args.argc, &args.argv, &error)) {
            success = false;
            return;
        }

        guint major, minor, micro, nano;
        gst_version(&major, &minor, &micro, &nano);

        QString nano_str;
        if (nano == 1)
            nano_str = " (CVS)";
        else if (nano == 2)
            nano_str = " (Prerelease)";

        version = QString("%1.%2.%3%4")
                      .arg(major)
                      .arg(minor)
                      .arg(micro)
                      .arg(!nano_str.isEmpty() ? qPrintable(nano_str) : "");

        uint need_maj = 1;
        uint need_min = 4;
        uint need_mic = 0;
        if (compare_gst_version(major, minor, micro, need_maj, need_min, need_mic) < 0) {
            qDebug("Need GStreamer version %d.%d.%d\n", need_maj, need_min, need_mic);
            success = false;
            return;
        }

        // manually load plugins?
        // if(!pluginPath.isEmpty())
        //	loadPlugins(pluginPath);

        // gstcustomelements_register();
        // gstelements_register();

        QStringList reqelem = QStringList() << "opusenc"
                                            << "opusdec"
                                            << "vorbisenc"
                                            << "vorbisdec"
                                            << "theoraenc"
                                            << "theoradec"
                                            << "rtpopuspay"
                                            << "rtpopusdepay"
                                            << "rtpvorbispay"
                                            << "rtpvorbisdepay"
                                            << "rtptheorapay"
                                            << "rtptheoradepay"
                                            << "filesrc"
                                            << "decodebin"
                                            << "jpegdec"
                                            << "oggmux"
                                            << "oggdemux"
                                            << "audioconvert"
                                            << "audioresample"
                                            << "volume"
                                            << "level"
                                            << "videoconvert"
                                            << "videorate"
                                            << "videoscale"
                                            << "rtpjitterbuffer"
                                            << "audiomixer"
                                            << "appsink";

#if defined(Q_OS_MAC)
        reqelem << "osxaudiosrc"
                << "osxaudiosink";
#ifdef HAVE_OSXVIDIO
        reqelem << "osxvideosrc";
#endif
#elif defined(Q_OS_LINUX)
        reqelem << "v4l2src";
#elif defined(Q_OS_UNIX)
        reqelem << "osssrc"
                << "osssink";
#elif defined(Q_OS_WIN)
        reqelem << "directsoundsrc"
                << "directsoundsink"
                << "ksvideosrc";
#endif

        foreach (const QString &name, reqelem) {
            GstElement *e = gst_element_factory_make(name.toLatin1().data(), nullptr);
            if (!e) {
                qDebug("Unable to load element '%s'.\n", qPrintable(name));
                success = false;
                return;
            }

            g_object_unref(G_OBJECT(e));
        }

        success = true;
    }

    ~GstSession()
    {
        // docs say to not bother with gst_deinit, but we'll do it
        //   anyway in case there's an issue with plugin unloading
        // update: there could be other gstreamer users, so we
        //   probably shouldn't call this.  also, it appears to crash
        //   on mac for at least one user..   maybe the function is
        //   not very well tested.
        // gst_deinit();
    }
};

//----------------------------------------------------------------------------
// GstMainLoop
//----------------------------------------------------------------------------

class GstMainLoop::Private {
public:
    typedef struct {
        GSource               parent;
        GstMainLoop::Private *d;
    } BridgeQueueSource;

    GstMainLoop *                                       q;
    QString                                             pluginPath;
    GstSession *                                        gstSession;
    bool                                                success;
    GMainContext *                                      mainContext;
    GMainLoop *                                         mainLoop;
    QMutex                                              m;
    QWaitCondition                                      w;
    BridgeQueueSource *                                 bridgeSource;
    guint                                               bridgeId;
    QQueue<QPair<GstMainLoop::ContextCallback, void *>> bridgeQueue;

    Private(GstMainLoop *q) : q(q), gstSession(nullptr), success(false), mainContext(nullptr), mainLoop(nullptr) {}

    static gboolean cb_loop_started(gpointer data) { return static_cast<Private *>(data)->loop_started(); }

    gboolean loop_started()
    {
        w.wakeOne();
        m.unlock();
        emit q->started();
        return FALSE;
    }

    static gboolean bridge_callback(gpointer data)
    {
        auto d = static_cast<GstMainLoop::Private *>(data);
        while (!d->bridgeQueue.empty()) {
            d->m.lock();
            QPair<GstMainLoop::ContextCallback, void *> p;
            bool                                        exist = !d->bridgeQueue.empty();
            if (exist)
                p = d->bridgeQueue.dequeue();
            d->m.unlock();
            if (exist)
                p.first(p.second);
        }

        return d->mainLoop == nullptr ? FALSE : TRUE;
    }

    static gboolean bridge_prepare(GSource *source, gint *timeout_)
    {
        *timeout_      = -1;
        auto         d = reinterpret_cast<Private::BridgeQueueSource *>(source)->d;
        QMutexLocker locker(&d->m);
        return !d->bridgeQueue.empty() ? TRUE : FALSE;
    }

    static gboolean bridge_check(GSource *source)
    {
        auto         d = reinterpret_cast<Private::BridgeQueueSource *>(source)->d;
        QMutexLocker locker(&d->m);
        return !d->bridgeQueue.empty() ? TRUE : FALSE;
    }

    static gboolean bridge_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
    {
        Q_UNUSED(source)
        if (callback(user_data))
            return TRUE;
        else
            return FALSE;
    }
};

GstMainLoop::GstMainLoop(const QString &resPath) : QObject()
{
    d             = new Private(this);
    d->pluginPath = resPath;

    // create a variable of type GSourceFuncs
    static GSourceFuncs bridgeFuncs
        = { Private::bridge_prepare, Private::bridge_check, Private::bridge_dispatch, nullptr, nullptr, nullptr };

    // create a new source
    d->bridgeSource = reinterpret_cast<Private::BridgeQueueSource *>(
        g_source_new(&bridgeFuncs, sizeof(Private::BridgeQueueSource)));
    d->bridgeSource->d = d;

    // HACK: if gstreamer initializes before certain Qt internal
    //   initialization occurs, then the app becomes unstable.
    //   I don't know what exactly needs to happen, or where the
    //   bug is, but if I fiddle with the default QStyle before
    //   initializing gstreamer, then this seems to solve it.
    //   it could be a bug in QCleanlooksStyle or QGtkStyle, which
    //   may conflict with separate Gtk initialization that may
    //   occur through gstreamer plugin loading.
    //{
    //	QIcon icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical, 0, 0);
    //}
}

GstMainLoop::~GstMainLoop()
{
    stop();
    delete d;
}

void GstMainLoop::stop()
{
    bool stopped = execInContext([this](void *) { g_main_loop_quit(d->mainLoop); }, this);

    if (stopped) {
        d->w.wait(&d->m);
    }
}

QString GstMainLoop::gstVersion() const
{
    QMutexLocker locker(&d->m);
    return d->gstSession->version;
}

GMainContext *GstMainLoop::mainContext()
{
    QMutexLocker locker(&d->m);
    return d->mainContext;
}

bool GstMainLoop::isInitialized() const
{
    QMutexLocker locker(&d->m);
    return d->success;
}

bool GstMainLoop::execInContext(const ContextCallback &cb, void *userData)
{
    QMutexLocker locker(&d->m);
    if (d->mainLoop) {
        d->bridgeQueue.enqueue({ cb, userData });
        g_main_context_wakeup(d->mainContext);
        return true;
    }
    return false;
}

void GstMainLoop::init()
{
    // qDebug("GStreamer thread started\n");

    // this will be unlocked as soon as the mainloop runs
    d->m.lock();

    d->gstSession = new GstSession(d->pluginPath);

    // report error
    if (!d->gstSession->success) {
        d->success = false;
        delete d->gstSession;
        d->gstSession = nullptr;
        d->w.wakeOne();
        d->m.unlock();
        // qDebug("GStreamer thread completed (error)\n");
        emit finished();
    }

    d->success = true;

    // qDebug("Using GStreamer version %s\n", qPrintable(d->gstSession->version));

    d->mainContext = g_main_context_new();
    d->mainLoop    = g_main_loop_new(d->mainContext, FALSE);

    // attach bridge source to context
    d->bridgeId = g_source_attach(&d->bridgeSource->parent, d->mainContext);
    g_source_set_callback(&d->bridgeSource->parent, GstMainLoop::Private::bridge_callback, d, nullptr);

    // deferred call to loop_started()
    GSource *timer = g_timeout_source_new(0);
    g_source_attach(timer, d->mainContext);
    g_source_set_callback(timer, GstMainLoop::Private::cb_loop_started, d, nullptr);
    d->m.unlock();
    emit initialized();
}

void GstMainLoop::start()
{
    // kick off the event loop
    g_main_loop_run(d->mainLoop);

    QMutexLocker locker(&d->m);
    g_main_loop_unref(d->mainLoop);
    d->mainLoop = nullptr;
    g_main_context_unref(d->mainContext);
    d->mainContext = nullptr;
    delete d->gstSession;
    d->gstSession = nullptr;

    d->w.wakeOne();
    emit finished();
    // qDebug("GStreamer thread completed\n");
}

}
