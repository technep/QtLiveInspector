// QtLiveInspector probe — injection entry points.
//
// This single shared library supports three ways into the target process:
//
//  1) LD_PRELOAD (Linux) / DYLD_INSERT_LIBRARIES (macOS):
//     the constructor below boots the probe before main() runs.
//
//  2) Qt style plugin (all platforms, incl. Windows):
//     copy the library as  <plugindir>/styles/libqtinspectorprobe.so
//     and run the target with:
//         QT_PLUGIN_PATH=<plugindir> QT_STYLE_OVERRIDE=qtinspectorprobe ./app
//     Qt loads the "style" very early; the returned style is plain Fusion,
//     so the application looks and behaves unchanged.
//
//  3) Explicit init from source:
//     extern "C" void qtinspector_init();  // link against the probe
//     qtinspector_init();                  // anywhere in main()

#include "ProbeController.h"

#include <QStyleFactory>
#include <QStylePlugin>

#if defined(Q_OS_UNIX)
__attribute__((constructor)) static void qtinspector_auto_boot()
{
    ProbeController::boot();
}
#endif

// Public C entry point for source-level integration.
extern "C" Q_DECL_EXPORT void qtinspector_init()
{
    ProbeController::boot();
}

class QtInspectorStylePlugin : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface")
public:
    QStyle *create(const QString &key) override
    {
        if (key.compare(QLatin1String("qtinspectorprobe"), Qt::CaseInsensitive) == 0) {
            ProbeController::boot();
            return QStyleFactory::create(QStringLiteral("Fusion"));
        }
        return nullptr;
    }
};

#include "probe_main.moc"
