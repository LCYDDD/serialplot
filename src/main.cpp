#include <QApplication>        // 引入 Qt 应用程序类头文件
#include <QtGlobal>            // 引入 Qt 全局定义头文件
#include <QIcon>               // 引入图标类头文件
#include <iostream>            // 引入输入输出流类

#include "mainwindow.h"        // 引入主窗口类头文件
#include "tooltipfilter.h"     // 引入工具提示过滤器头文件
#include "version.h"           // 引入版本信息头文件

MainWindow* pMainWindow = nullptr;  // 声明一个指向 MainWindow 的全局指针，初始化为空指针


// 自定义的日志处理函数，用于处理不同类型的 Qt 消息
void messageHandler(QtMsgType type, const QMessageLogContext &context,
                    const QString &msg)
{
    QString logString;  // 用于存储格式化后的日志消息

    switch (type)
    {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 5, 0))  // 判断 Qt 版本是否大于或等于 5.5
        case QtInfoMsg:  // 处理信息类型的消息
            logString = "[Info] " + msg;  // 格式化日志消息为 [Info] 前缀
            break;
#endif
        case QtDebugMsg:  // 处理调试类型的消息
            logString = "[Debug] " + msg;  // 格式化日志消息为 [Debug] 前缀
            break;
        case QtWarningMsg:  // 处理警告类型的消息
            logString = "[Warning] " + msg;  // 格式化日志消息为 [Warning] 前缀
            break;
        case QtCriticalMsg:  // 处理错误类型的消息
            logString = "[Error] " + msg;  // 格式化日志消息为 [Error] 前缀
            break;
        case QtFatalMsg:  // 处理致命错误类型的消息
            logString = "[Fatal] " + msg;  // 格式化日志消息为 [Fatal] 前缀
            break;
    }

    std::cerr << logString.toStdString() << std::endl;  // 将格式化后的日志输出到标准错误流

    if (pMainWindow != nullptr)  // 如果主窗口对象不为空
    {
        // TODO: 如果窗口已经销毁，则不调用 MainWindow::messageHandler
        pMainWindow->messageHandler(type, logString, msg);  // 调用主窗口的消息处理函数，传递日志类型和消息
    }

    if (type == QtFatalMsg)  // 如果是致命错误消息
    {
        __builtin_trap();  // 触发程序崩溃，用于终止程序
    }
}

int main(int argc, char *argv[])
{

    QApplication a(argc, argv);  // 创建一个 QApplication 对象，传递命令行参数
    QApplication::setApplicationName(PROGRAM_NAME);  // 设置应用程序的名称
    QApplication::setApplicationVersion(VERSION_STRING);  // 设置应用程序的版本

#ifdef Q_OS_WIN  // 如果当前操作系统是 Windows
    QIcon::setFallbackSearchPaths(QIcon::fallbackSearchPaths() << ":icons");  // 设置图标搜索路径
    QIcon::setThemeName("tango");  // 设置图标主题为 "tango"
#endif

    qInstallMessageHandler(messageHandler);  // 告知QT安装使用自定义的消息处理函数
    MainWindow w;  // 创建 MainWindow 对象
    pMainWindow = &w;  // 将全局指针 pMainWindow 指向当前的 MainWindow 对象

    ToolTipFilter ttf;  // 创建工具提示过滤器对象
    a.installEventFilter(&ttf);  // 安装事件过滤器，用于拦截事件

    // 打印应用程序的相关信息到调试输出
    qDebug() << "SerialPlot" << VERSION_STRING;  // 打印应用程序名称和版本
    qDebug() << "Revision" << VERSION_REVISION;  // 打印修订号

    w.show();  // 显示主窗口

    return a.exec();  // 进入应用程序的事件循环，等待用户操作
}
