# FIXME: building elements in shared mode causes them to drag in the entire
#   dependencies of psimedia

include(../conf.pri)

windows {
	INCLUDEPATH += \
		c:/glib/include/glib-2.0 \
		c:/glib/lib/glib-2.0/include \
		c:/gstforwin/dxsdk/include \
		c:/gstforwin/winsdk/include \
		c:/gstforwin/include \
		c:/gstforwin/include/libxml2 \
		c:/gstforwin/gstreamer/include/gstreamer-1.0 \
		c:/gstforwin/gst-plugins-base/include/gstreamer-1.0
	LIBS += \
		-Lc:/glib/bin \
		-Lc:/gstforwin/bin \
		-Lc:/gstforwin/gstreamer/bin \
		-Lc:/gstforwin/gst-plugins-base/bin \
		-lgstreamer-1.0-0 \
		-lgthread-2.0-0 \
		-lglib-2.0-0 \
		-lgobject-2.0-0 \
		-lgstvideo-1.0-0 \
		-lgstbase-1.0-0
}
