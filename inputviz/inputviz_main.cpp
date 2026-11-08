// inputviz_main.cpp — standalone touch/mouse input visualization as an
// LD_PRELOAD library. Where the full probe (libqtinspectorprobe) brings the
// whole inspection server, this library does ONE thing: visualize presses,
// press-and-hold and releases with the lightweight InputVisualizer overlay.
// Ideal for demos and embedded targets that want touch feedback without any
// network surface.
//
// Usage (Linux):
//   LD_PRELOAD=/path/to/libqtinspectorinputviz.so ./your-qt6-app
//
// Configuration (environment of the TARGET):
//   QLI_INPUT_VIZ=0            disable (default: enabled)
//   QLI_INPUT_VIZ_MULTITOUCH=1 track all fingers (default: first touch only)
//   QLI_INPUT_VIZ_MONKEY=1     monkey icon instead of the circle (fun mode
//                              for qli_monkey.py stress runs)
//
// For platforms without LD_PRELOAD, call qtinspector_inputviz_init() before
// the QApplication event loop starts (and link this library).
#include "InputVisualizer.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <chrono>
#include <mutex>
#include <thread>

namespace {

std::once_flag once;

void startWhenAppExists()
{
    std::call_once(once, [] {
        // The constructor of an LD_PRELOADed library runs before main(), so
        // wait on a helper thread until the QCoreApplication exists, then
        // continue in the GUI thread.
        std::thread([] {
            while (!QCoreApplication::instance())
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [] {
                    auto *viz =
                        new InputVisualizer(QCoreApplication::instance());
                    viz->setMultiTouch(
                        qgetenv("QLI_INPUT_VIZ_MULTITOUCH") == "1");
                    viz->setMonkeyMode(
                        qgetenv("QLI_INPUT_VIZ_MONKEY") == "1");
                    viz->setEnabled(qgetenv("QLI_INPUT_VIZ") != "0");
                },
                Qt::QueuedConnection);
        }).detach();
    });
}

} // namespace

#if defined(__GNUC__)
__attribute__((constructor)) static void inputviz_boot()
{
    startWhenAppExists();
}
#endif

extern "C" void qtinspector_inputviz_init()
{
    startWhenAppExists();
}
