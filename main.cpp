#include <QApplication>
#include <QPalette>
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QVector>
#include <QScreen>
#include <unistd.h>
#include <signal.h>
#include <csignal>
#include <cstdlib>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <dirent.h>
#include <sys/stat.h>

#include "TeachingWidget.h"
#include "CustomMessageBox.h"
#include "ConfigManager.h"
#include "Spinnaker.h"

// 전역 변수
TeachingWidget* g_teachingWidget = nullptr;
QVector<QString> g_pendingLogMessages;

// USB 카메라 (FLIR) 리셋 함수
void resetUsbCameras() {
    fprintf(stderr, "[Cleanup] Scanning for FLIR USB cameras (VendorID: 1e10)...\n");
    const char* base_path = "/sys/bus/usb/devices";
    DIR* dir = opendir(base_path);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue; // ., ..

        // Vendor ID 확인
        std::string vid_path = std::string(base_path) + "/" + entry->d_name + "/idVendor";
        FILE* fp = fopen(vid_path.c_str(), "r");
        if (!fp) continue;

        char vid[16] = {0};
        if (fgets(vid, sizeof(vid), fp)) {
            // 개행 제거
            vid[strcspn(vid, "\n")] = 0;
            if (strcmp(vid, "1e10") == 0) { // FLIR Vendor ID
                // Bus Num, Dev Num 가져오기
                int busnum = 0, devnum = 0;
                
                std::string bus_path = std::string(base_path) + "/" + entry->d_name + "/busnum";
                std::string dev_path = std::string(base_path) + "/" + entry->d_name + "/devnum";
                
                FILE* f_bus = fopen(bus_path.c_str(), "r");
                FILE* f_dev = fopen(dev_path.c_str(), "r");
                
                if (f_bus && f_dev) {
                    fscanf(f_bus, "%d", &busnum);
                    fscanf(f_dev, "%d", &devnum);
                    
                    char dev_node[64];
                    sprintf(dev_node, "/dev/bus/usb/%03d/%03d", busnum, devnum);
                    
                    fprintf(stderr, "[Cleanup] Resetting USB Device: %s (Bus %03d, Dev %03d)\n", dev_node, busnum, devnum);
                    
                    int fd = open(dev_node, O_WRONLY);
                    if (fd >= 0) {
                        int rc = ioctl(fd, USBDEVFS_RESET, 0);
                        if (rc < 0) {
                            perror("[Cleanup] USB Reset failed");
                        } else {
                            fprintf(stderr, "[Cleanup] USB Reset successful\n");
                        }
                        close(fd);
                    } else {
                        perror("[Cleanup] Failed to open USB device node");
                    }
                }
                if (f_bus) fclose(f_bus);
                if (f_dev) fclose(f_dev);
            }
        }
        fclose(fp);
    }
    closedir(dir);
}

// IPC 리소스 정리 함수 (공유 메모리, 세마포어)
void cleanupIPCResources() {
    system("ipcs -m | grep $USER | awk '{print $2}' | xargs -r -I {} ipcrm -m {} 2>/dev/null");
    system("ipcs -s | grep $USER | awk '{print $2}' | xargs -r -I {} ipcrm -s {} 2>/dev/null");
    system("ipcs -q | grep $USER | awk '{print $2}' | xargs -r -I {} ipcrm -q {} 2>/dev/null");
}

// Spinnaker System 정리 함수
void cleanupSpinnaker() {
    static bool cleaned = false;
    if (cleaned) return;
    cleaned = true;
    
    cleanupIPCResources();
    
    try {
        Spinnaker::SystemPtr system = nullptr;
        try {
            system = Spinnaker::System::GetInstance();
        } catch (const Spinnaker::Exception&) {
            return;
        }
        
        if (system) {
            try {
                Spinnaker::CameraList camList = system->GetCameras();
                if (camList.GetSize() > 0) {
                    for (unsigned int i = 0; i < camList.GetSize(); i++) {
                        try {
                            Spinnaker::CameraPtr cam = camList.GetByIndex(i);
                            if (cam && cam->IsInitialized()) {
                                if (cam->IsStreaming()) {
                                    cam->EndAcquisition();
                                }
                                cam->DeInit();
                            }
                        } catch (...) {}
                    }
                    camList.Clear();
                }
            } catch (...) {}
            
            try {
                system->ReleaseInstance();
            } catch (...) {}
        }
    } catch (...) {}
}

// 시그널 핸들러
void signalHandler(int sig) {
    static volatile sig_atomic_t handling = 0;
    if (handling) {
        _exit(128 + sig);
    }
    handling = 1;
    
    fprintf(stderr, "\n[SignalHandler] Signal received: %d\n", sig);
    
    // ★ 가장 먼저 카메라 끄기 (camOff 상태로 전환)
    if (g_teachingWidget) {
        fprintf(stderr, "[SignalHandler] Forcing camOff state...\n");
        g_teachingWidget->forceCamOff();
    }
    
    cleanupSpinnaker();
    
    if (ConfigManager::instance()) {
        ConfigManager::instance()->saveConfig();
    }
    
    signal(sig, SIG_DFL);
    raise(sig);
}

void setupSignalHandlers() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGABRT, signalHandler);
}

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString formattedMessage = QString("%1 - %2").arg(timestamp).arg(msg);
    
    if (g_teachingWidget) {
        g_teachingWidget->receiveLogMessage(formattedMessage);
    } else {
        g_pendingLogMessages.append(formattedMessage);
    }
    
    QByteArray localMsg = msg.toLocal8Bit();
    fprintf(stderr, "%s\n", localMsg.constData());
}

void flushPendingLogs() {
    if (g_teachingWidget) {
        for (const QString& msg : g_pendingLogMessages) {
            g_teachingWidget->receiveLogMessage(msg);
        }
        g_pendingLogMessages.clear();
    }
}

int main(int argc, char *argv[]) {
    fprintf(stderr, "[Main] Starting Inspector\n");
    
    // 시그널 핸들러 등록
    setupSignalHandlers();
    
    // OS별 Qt 플랫폼 플러그인 설정
#ifdef Q_OS_LINUX
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    
    QApplication app(argc, argv);
    
    // 애플리케이션 전체 다크 테마 스타일 적용
    app.setStyle("Fusion");
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
    darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);
    
    app.setStyleSheet(
        "QMenuBar { background-color: rgb(53, 53, 53); color: white; } "
        "QMenuBar::item { background-color: transparent; padding: 4px 8px; } "
        "QMenuBar::item:selected { background-color: rgb(42, 130, 218); } "
        "QMenuBar::item:pressed { background-color: rgb(30, 100, 180); } "
        "QMenu { background-color: rgb(53, 53, 53); color: white; border: 1px solid rgb(80, 80, 80); } "
        "QMenu::item:selected { background-color: rgb(42, 130, 218); } "
        "QStatusBar { background-color: rgb(53, 53, 53); color: white; } "
        "QToolTip { background-color: rgb(70, 70, 70); color: white; border: 1px solid rgb(100, 100, 100); } "
    );
    
    qInstallMessageHandler(customMessageHandler);
    
    // 티칭 위젯 생성
    TeachingWidget *widget = new TeachingWidget(0, "카메라 1");
    g_teachingWidget = widget;
    
    flushPendingLogs();
    
    widget->setWindowTitle("KM Inspector");
    widget->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    widget->showMaximized();
    
    QObject::connect(&app, &QApplication::aboutToQuit, [widget]() {
        if (widget) {
            widget->forceCamOff();
        }
    });
    
    int result = app.exec();
    
    if (widget) {
        widget->forceCamOff();
    }
    cleanupSpinnaker();
    ConfigManager::instance()->saveConfig();
    
    g_teachingWidget = nullptr;
    qInstallMessageHandler(nullptr);
    
    _exit(result);
}