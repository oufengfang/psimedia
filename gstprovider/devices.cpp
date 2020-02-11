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

#include "devices.h"

#include "gstthread.h"
#include <QMap>
#include <QMutex>
#include <QSize>
#include <QStringList>
#include <gst/gst.h>

namespace PsiMedia {

#if !defined(Q_OS_LINUX)
// add more platforms to the ifdef when ready
// below is a default impl
QList<GstDevice> PlatformDeviceMonitor::getDevices() { return QList<GstDevice>(); }
#endif

#if 0
// for elements that we can't enumerate devices for, we need a way to ensure
//   that at least the default device works
// FIXME: why do we have both this function and test_video() ?
static bool test_element(const QString &element_name)
{
    GstElement *e = gst_element_factory_make(element_name.toLatin1().data(), nullptr);
    if(!e)
        return 0;

    gst_element_set_state(e, GST_STATE_READY);
    int ret = gst_element_get_state(e, nullptr, nullptr, GST_CLOCK_TIME_NONE);

    gst_element_set_state(e, GST_STATE_NULL);
    gst_element_get_state(e, nullptr, nullptr, GST_CLOCK_TIME_NONE);
    g_object_unref(G_OBJECT(e));

    if(ret != GST_STATE_CHANGE_SUCCESS)
        return false;

    return true;
}
#endif

// copied from gst-inspect-1.0. perfect for identifying devices
static gchar *get_launch_line(::GstDevice *device)
{
    static const char *const ignored_propnames[] = { "name", "parent", "direction", "template", "caps", nullptr };
    GString *                launch_line;
    GstElement *             element;
    GstElement *             pureelement;
    GParamSpec **            properties, *property;
    GValue                   value  = G_VALUE_INIT;
    GValue                   pvalue = G_VALUE_INIT;
    guint                    i, number_of_properties;
    GstElementFactory *      factory;

    element = gst_device_create_element(device, nullptr);

    if (!element)
        return nullptr;

    factory = gst_element_get_factory(element);
    if (!factory) {
        gst_object_unref(element);
        return nullptr;
    }

    if (!gst_plugin_feature_get_name(factory)) {
        gst_object_unref(element);
        return nullptr;
    }

    launch_line = g_string_new(gst_plugin_feature_get_name(factory));

    pureelement = gst_element_factory_create(factory, nullptr);

    /* get paramspecs and show non-default properties */
    properties = g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &number_of_properties);
    if (properties) {
        for (i = 0; i < number_of_properties; i++) {
            gint     j;
            gboolean ignore = FALSE;
            property        = properties[i];

            /* skip some properties */
            if ((property->flags & G_PARAM_READWRITE) != G_PARAM_READWRITE)
                continue;

            for (j = 0; ignored_propnames[j]; j++)
                if (!g_strcmp0(ignored_propnames[j], property->name))
                    ignore = TRUE;

            if (ignore)
                continue;

            /* Can't use _param_value_defaults () because sub-classes modify the
             * values already.
             */

            g_value_init(&value, property->value_type);
            g_value_init(&pvalue, property->value_type);
            g_object_get_property(G_OBJECT(element), property->name, &value);
            g_object_get_property(G_OBJECT(pureelement), property->name, &pvalue);
            if (gst_value_compare(&value, &pvalue) != GST_VALUE_EQUAL) {
                gchar *valuestr = gst_value_serialize(&value);

                if (!valuestr) {
                    GST_WARNING("Could not serialize property %s:%s", GST_OBJECT_NAME(element), property->name);
                    g_free(valuestr);
                    goto next;
                }

                g_string_append_printf(launch_line, " %s=%s", property->name, valuestr);
                g_free(valuestr);
            }

        next:
            g_value_unset(&value);
            g_value_unset(&pvalue);
        }
        g_free(properties);
    }

    gst_object_unref(element);
    gst_object_unref(pureelement);

    return g_string_free(launch_line, FALSE);
}

class DeviceMonitor::Private {
public:
    DeviceMonitor *          q;
    GstDeviceMonitor *       _monitor = nullptr;
    QMap<QString, GstDevice> _devices;
    PlatformDeviceMonitor *  _platform = nullptr;
    QMutex                   m;

    bool videoSrcFirst  = true;
    bool audioSrcFirst  = true;
    bool audioSinkFirst = true;

    explicit Private(DeviceMonitor *q) : q(q) {}

    static GstDevice gstDevConvert(::GstDevice *gdev)
    {
        PsiMedia::GstDevice d;

        gchar *ll = get_launch_line(gdev);
        if (ll) {
            auto e = gst_parse_launch(ll, nullptr);
            if (e) {
                d.id = QString::fromUtf8(ll);
                gst_object_unref(e);
            }
            g_free(ll);
            if (d.id.isEmpty() || d.id.endsWith(QLatin1String(".monitor"))) {
                d.id.clear();
                return d;
            }
        }

        gchar *name = gst_device_get_display_name(gdev);
        d.name      = QString::fromUtf8(name);
        g_free(name);

        if (gst_device_has_classes(gdev, "Audio/Source")) {
            d.type = PDevice::AudioIn;
        }

        if (gst_device_has_classes(gdev, "Audio/Sink")) {
            d.type = PDevice::AudioOut;
        }

        if (gst_device_has_classes(gdev, "Video/Source")) {
            d.type = PDevice::VideoIn;
        }

        return d;
    }

    static gboolean onChangeGstCB(GstBus *bus, GstMessage *message, gpointer user_data)
    {
        Q_UNUSED(bus)
        auto                monObj = reinterpret_cast<DeviceMonitor::Private *>(user_data);
        PsiMedia::GstDevice d;
        ::GstDevice *       device;

        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_DEVICE_ADDED:
            gst_message_parse_device_added(message, &device);
            d = gstDevConvert(device);
            gst_object_unref(device);
            if (!d.id.isEmpty())
                monObj->q->onDeviceAdded(d);
            break;
        case GST_MESSAGE_DEVICE_REMOVED:
            gst_message_parse_device_removed(message, &device);
            d = gstDevConvert(device);
            gst_object_unref(device);
            if (!d.id.isEmpty())
                monObj->q->onDeviceRemoved(d);
            break;
        default:
            break;
        }

        return TRUE;
    }
};

void DeviceMonitor::updateDevList()
{
    d->_devices.clear();
    GList *devs = gst_device_monitor_get_devices(d->_monitor);
    GList *dev  = devs;

    for (; dev != nullptr; dev = dev->next) {
        PsiMedia::GstDevice pdev = Private::gstDevConvert(static_cast<::GstDevice *>(dev->data));
        if (pdev.id.isEmpty())
            continue;
        d->_devices.insert(pdev.id, pdev);
    }
    g_list_free(devs);

    if (d->_platform) {
        auto l = d->_platform->getDevices();
        for (auto const &pdev : l) {
            if (!d->_devices.contains(pdev.id)) {
                d->_devices.insert(pdev.id, pdev);
            }
        }
    }

    for (auto const &pdev : d->_devices) {
        qDebug("found dev: %s (%s)", qPrintable(pdev.name), qPrintable(pdev.id));
    }
}

void DeviceMonitor::onDeviceAdded(GstDevice dev)
{
    QMutexLocker locker(&d->m);
    if (d->_devices.contains(dev.id)) {
        qWarning("Double added of device %s (%s)", qPrintable(dev.name), qPrintable(dev.id));
    } else {
        switch (dev.type) {
        case PDevice::AudioIn:
            dev.isDefault    = d->audioSrcFirst;
            d->audioSrcFirst = false;
            break;
        case PDevice::AudioOut:
            dev.isDefault     = d->audioSinkFirst;
            d->audioSinkFirst = false;
            break;
        case PDevice::VideoIn:
            dev.isDefault    = d->videoSrcFirst;
            d->videoSrcFirst = false;
            break;
        }
        d->_devices.insert(dev.id, dev);
        qDebug("added dev: %s (%s)", qPrintable(dev.name), qPrintable(dev.id));
        emit updated();
    }
}

void DeviceMonitor::onDeviceRemoved(const GstDevice &dev)
{
    QMutexLocker locker(&d->m);
    if (d->_devices.remove(dev.id)) {
        qDebug("removed dev: %s (%s)", qPrintable(dev.name), qPrintable(dev.id));
        emit updated();
    } else {
        qWarning("Double remove of device %s (%s)", qPrintable(dev.name), qPrintable(dev.id));
    }
}

DeviceMonitor::DeviceMonitor(GstMainLoop *mainLoop) : d(new Private(this))
{
    qRegisterMetaType<GstDevice>("GstDevice");
    Q_ASSERT(mainLoop->mainContext() == g_main_context_default());

    // auto context = mainLoop->mainContext();
    d->_platform = new PlatformDeviceMonitor;
    d->_monitor  = gst_device_monitor_new();

    GstBus *bus = gst_device_monitor_get_bus(d->_monitor);
    gst_bus_add_watch(bus, Private::onChangeGstCB, d);

    // GSource *source = gst_bus_create_watch(bus);
    // g_source_set_callback (source, (GSourceFunc)Private::onChangeGstCB, d, nullptr);
    // g_source_attach(source, context);
    // g_source_unref(source);

    gst_object_unref(bus);

    gst_device_monitor_add_filter(d->_monitor, "Audio/Sink", nullptr);
    gst_device_monitor_add_filter(d->_monitor, "Audio/Source", nullptr);

    GstCaps *caps;
    caps = gst_caps_new_empty_simple("video/x-raw");
    gst_device_monitor_add_filter(d->_monitor, "Video/Source", caps);
    caps = gst_caps_new_empty_simple("image/jpeg");
    gst_device_monitor_add_filter(d->_monitor, "Video/Source", caps);
    gst_caps_unref(caps);

    updateDevList();
    if (!gst_device_monitor_start(d->_monitor)) {
        qWarning("failed to start device monitor");
    }
}

DeviceMonitor::~DeviceMonitor()
{
    delete d->_platform;
    gst_device_monitor_stop(d->_monitor);
    g_object_unref(d->_monitor);
    delete d;
}

QList<GstDevice> DeviceMonitor::devices(PDevice::Type type)
{
    QList<GstDevice> ret;
    QMutexLocker     locker(&d->m);
    for (auto const &dev : d->_devices) {
        if (dev.type == type)
            ret.append(dev);
    }
    std::sort(ret.begin(), ret.end(), [](const GstDevice &a, const GstDevice &b) { return a.name < b.name; });
    return ret;
}

GstElement *devices_makeElement(const QString &id, PDevice::Type type, QSize *captureSize)
{
    Q_UNUSED(type);
    Q_UNUSED(captureSize);
    return gst_parse_launch(id.toLatin1().data(), nullptr);
    // TODO check if it correponds to passed type.
    // TODO drop captureSize
}

}
