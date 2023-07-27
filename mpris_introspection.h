#ifndef MPRIS_INTROSPECTION_H
#define MPRIS_INTROSPECTION_H

static char *mpris_introspection = "\
<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\
\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\
<node>\
  <interface name=\"org.mpris.MediaPlayer2\">\
    <method name=\"Raise\"/>\
    <method name=\"Quit\"/>\
    <property name=\"CanQuit\" type=\"b\" access=\"read\"/>\
    <property name=\"CanRaise\" type=\"b\" access=\"read\"/>\
    <property name=\"HasTrackList\" type=\"b\" access=\"read\"/>\
    <property name=\"Identity\" type=\"s\" access=\"read\"/>\
    <property name=\"DesktopEntry\" type=\"s\" access=\"read\"/>\
    <property name=\"SupportedUriSchemes\" type=\"as\" access=\"read\"/>\
    <property name=\"SupportedMimeTypes\" type=\"as\" access=\"read\"/>\
  </interface>\
	<interface name='org.mpris.MediaPlayer2.Player'>\
		<method name='Next'/>\
		<method name='Previous'/>\
		<method name='Pause'/>\
		<method name='PlayPause'/>\
		<method name='Stop'/>\
		<method name='Play'/>\
		<method name='Seek'>\
			<arg direction='in' name='Offset' type='x'/>\
		</method>\
		<method name='SetPosition'>\
			<arg direction='in' name='TrackId' type='o'/>\
			<arg direction='in' name='Position' type='x'/>\
		</method>\
		<method name='OpenUri'>\
			<arg direction='in' name='Uri' type='s'/>\
		</method>\
		<signal name='Seeked'>\
			<arg name='Position' type='x'/>\
		</signal>\
		<property name='PlaybackStatus' type='s' access='read'/>\
		<property name='LoopStatus' type='s' access='readwrite'/>\
		<property name='Rate' type='d' access='readwrite'/>\
		<property name='Shuffle' type='b' access='readwrite'/>\
		<property name='Metadata' type='a{sv}' access='read'>\
			<annotation name=\"org.qtproject.QtDBus.QtTypeName\" value=\"QVariantMap\"/>\
		</property>\
		<property name='Volume' type='d' access='read'/>\
		<method name='SetVolume'>\
      <arg name='volume' type='d' direction='in' />\
    </method>\
		<property name='Position' type='x' access='read'/>\
		<property name='MinimumRate' type='d' access='read'/>\
		<property name='MaximumRate' type='d' access='read'/>\
		<property name='CanGoNext' type='b' access='read'/>\
		<property name='CanGoPrevious' type='b' access='read'/>\
		<property name='CanPlay' type='b' access='read'/>\
		<property name='CanPause' type='b' access='read'/>\
		<property name='CanSeek' type='b' access='read'/>\
		<property name='CanControl' type='b' access='read'/>\
	</interface>\
</node>\
";

#endif
