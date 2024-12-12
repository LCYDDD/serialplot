#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QByteArray>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QMenu>
#include <QDesktopServices>
#include <QMap>
#include <QtDebug>
#include <QCommandLineParser>
#include <QFileInfo>
#include <qwt_plot.h>
#include <limits.h>
#include <cmath>
#include <iostream>
#include <cstdlib>

#include <plot.h>
#include <barplot.h>

#include "framebufferseries.h"
#include "utils.h"
#include "defines.h"
#include "version.h"
#include "setting_defines.h"

#if defined(Q_OS_WIN) && defined(QT_STATIC)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

// 面板设置的映射，保持面板顺序一致
const QMap<int, QString> panelSettingMap({
        {0, "Port"},
        {1, "DataFormat"},
        {2, "Plot"},
        {3, "Commands"},
        {4, "Record"},
        {5, "TextView"},
        {6, "Log"}
    });

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow), // 初始化 UI
    aboutDialog(this),
    portControl(&serialPort),
    secondaryPlot(NULL), // 初始化副图
    snapshotMan(this, &stream), // 快照管理器
    commandPanel(&serialPort),
    dataFormatPanel(&serialPort),
    recordPanel(&stream),
    textView(&stream),
    updateCheckDialog(this),
    bpsLabel(&portControl, &dataFormatPanel, this) // 初始化比特率标签
{
    ui->setupUi(this); // 设置 UI

    plotMan = new PlotManager(ui->plotArea, &plotMenu, &stream); // 初始化绘图管理器

    // 在选项卡中添加控制面板
    ui->tabWidget->insertTab(0, &portControl, "Port");
    ui->tabWidget->insertTab(1, &dataFormatPanel, "Data Format");
    ui->tabWidget->insertTab(2, &plotControlPanel, "Plot");
    ui->tabWidget->insertTab(3, &commandPanel, "Commands");
    ui->tabWidget->insertTab(4, &recordPanel, "Record");
    ui->tabWidget->insertTab(5, &textView, "Text View");
    ui->tabWidget->setCurrentIndex(0); // 设置默认显示面板为端口控制面板

    // 添加工具栏
    auto tbPortControl = portControl.toolBar();
    addToolBar(tbPortControl);
    addToolBar(recordPanel.toolbar());

    // 将快照和命令面板加入菜单栏
    ui->plotToolBar->addAction(snapshotMan.takeSnapshotAction());
    menuBar()->insertMenu(ui->menuHelp->menuAction(), snapshotMan.menu());
    menuBar()->insertMenu(ui->menuHelp->menuAction(), commandPanel.menu());

    // 连接命令面板的信号，切换到命令面板
    connect(&commandPanel, &CommandPanel::focusRequested, [this]()
            {
                this->ui->tabWidget->setCurrentWidget(&commandPanel);
                this->ui->tabWidget->showTabs();
            });

    tbPortControl->setObjectName("tbPortControl");
    ui->plotToolBar->setObjectName("tbPlot");

    setupAboutDialog(); // 设置关于对话框

    // 初始化视图菜单
    ui->menuBar->insertMenu(ui->menuSecondary->menuAction(), &plotMenu);
    plotMenu.addSeparator();
    QMenu* tbMenu = plotMenu.addMenu("Toolbars");
    tbMenu->addAction(ui->plotToolBar->toggleViewAction());
    tbMenu->addAction(portControl.toolBar()->toggleViewAction());

    // 初始化副绘图菜单
    auto group = new QActionGroup(this);
    group->addAction(ui->actionVertical);
    group->addAction(ui->actionHorizontal);

    // 连接信号槽

    // 副绘图菜单信号
    connect(ui->actionBarPlot, &QAction::triggered,
            this, &MainWindow::showBarPlot);

    connect(ui->actionVertical, &QAction::triggered,
            [this](bool checked)
            {
                if (checked) ui->splitter->setOrientation(Qt::Vertical); // 垂直显示
            });

    connect(ui->actionHorizontal, &QAction::triggered,
            [this](bool checked)
            {
                if (checked) ui->splitter->setOrientation(Qt::Horizontal); // 水平显示
            });

    // 帮助菜单信号
    QObject::connect(ui->actionHelpAbout, &QAction::triggered,
              &aboutDialog, &QWidget::show);

    QObject::connect(ui->actionCheckUpdate, &QAction::triggered,
              &updateCheckDialog, &QWidget::show);

    QObject::connect(ui->actionReportBug, &QAction::triggered,
                     [](){QDesktopServices::openUrl(QUrl(BUG_REPORT_URL));});

    // 文件菜单信号
    QObject::connect(ui->actionExportCsv, &QAction::triggered,
                     this, &MainWindow::onExportCsv);

    QObject::connect(ui->actionExportSvg, &QAction::triggered,
                     this, &MainWindow::onExportSvg);

    QObject::connect(ui->actionSaveSettings, &QAction::triggered,
                     this, &MainWindow::onSaveSettings);

    QObject::connect(ui->actionLoadSettings, &QAction::triggered,
                     this, &MainWindow::onLoadSettings);

    ui->actionQuit->setShortcutContext(Qt::ApplicationShortcut);

    QObject::connect(ui->actionQuit, &QAction::triggered,
                     this, &MainWindow::close);

    // 端口控制信号
    QObject::connect(&portControl, &PortControl::portToggled,
                     this, &MainWindow::onPortToggled);

    // 绘图控制信号
    connect(&plotControlPanel, &PlotControlPanel::numOfSamplesChanged,
            this, &MainWindow::onNumOfSamplesChanged);

    connect(&plotControlPanel, &PlotControlPanel::numOfSamplesChanged,
            plotMan, &PlotManager::setNumOfSamples);

    connect(&plotControlPanel, &PlotControlPanel::yScaleChanged,
            plotMan, &PlotManager::setYAxis);

    connect(&plotControlPanel, &PlotControlPanel::xScaleChanged,
            &stream, &Stream::setXAxis);

    connect(&plotControlPanel, &PlotControlPanel::xScaleChanged,
            plotMan, &PlotManager::setXAxis);

    connect(&plotControlPanel, &PlotControlPanel::plotWidthChanged,
            plotMan, &PlotManager::setPlotWidth);

    connect(&plotControlPanel, &PlotControlPanel::lineThicknessChanged,
            plotMan, &PlotManager::setLineThickness);

    // 绘图工具栏信号
    QObject::connect(ui->actionClear, SIGNAL(triggered(bool)),
                     this, SLOT(clearPlot()));

    QObject::connect(snapshotMan.takeSnapshotAction(), &QAction::triggered,
                     plotMan, &PlotManager::flashSnapshotOverlay);

    QObject::connect(ui->actionPause, &QAction::triggered,
                     &stream, &Stream::pause);

    QObject::connect(ui->actionPause, &QAction::triggered,
                     [this](bool enabled)
                     {
                         if (enabled && !recordPanel.recordPaused())
                         {
                             dataFormatPanel.pause(true);
                         }
                         else
                         {
                             dataFormatPanel.pause(false);
                         }
                     });

    QObject::connect(&recordPanel, &RecordPanel::recordPausedChanged,
                     [this](bool enabled)
                     {
                         if (ui->actionPause->isChecked() && enabled)
                         {
                             dataFormatPanel.pause(false);
                         }
                     });

    connect(&serialPort, &QIODevice::aboutToClose,
            &recordPanel, &RecordPanel::onPortClose);

    // 初始化绘图
    numOfSamples = plotControlPanel.numOfSamples();
    stream.setNumSamples(numOfSamples);
    plotControlPanel.setChannelInfoModel(stream.infoModel());

    // 初始化坐标轴
    stream.setXAxis(plotControlPanel.xAxisAsIndex(),
                    plotControlPanel.xMin(), plotControlPanel.xMax());

    plotMan->setYAxis(plotControlPanel.autoScale(),
                      plotControlPanel.yMin(), plotControlPanel.yMax());
    plotMan->setXAxis(plotControlPanel.xAxisAsIndex(),
                      plotControlPanel.xMin(), plotControlPanel.xMax());
    plotMan->setNumOfSamples(numOfSamples);
    plotMan->setPlotWidth(plotControlPanel.plotWidth());

    // 初始化比特率（bps）计数器
    ui->statusBar->addPermanentWidget(&bpsLabel);

    // 初始化每秒样本数（sps）计数器
    spsLabel.setText("0sps");
    spsLabel.setToolTip(tr("每通道每秒样本数"));
    ui->statusBar->addPermanentWidget(&spsLabel);
    connect(&sampleCounter, &SampleCounter::spsChanged,
            this, &MainWindow::onSpsChanged);


    bpsLabel.setMinimumWidth(70);
    bpsLabel.setAlignment(Qt::AlignRight);
    spsLabel.setMinimumWidth(70);
    spsLabel.setAlignment(Qt::AlignRight);

    // init demo
    QObject::connect(ui->actionDemoMode, &QAction::toggled,
                     this, &MainWindow::enableDemo);

    QObject::connect(ui->actionDemoMode, &QAction::toggled,
                     plotMan, &PlotManager::showDemoIndicator);

    // init stream connections
    connect(&dataFormatPanel, &DataFormatPanel::sourceChanged,
            this, &MainWindow::onSourceChanged);
    onSourceChanged(dataFormatPanel.activeSource());

    // load default settings
    QSettings settings(PROGRAM_NAME, PROGRAM_NAME);
    loadAllSettings(&settings);

    handleCommandLineOptions(*QApplication::instance());

    // ensure command panel has 1 command if none loaded
    if (!commandPanel.numOfCommands())
    {
        commandPanel.newCommandAction()->trigger();
    }

    // Important: This should be after newCommandAction is triggered
    // (above) we don't want user to be greeted with command panel on
    // the very first run.
    connect(commandPanel.newCommandAction(), &QAction::triggered, [this]()
            {
                this->ui->tabWidget->setCurrentWidget(&commandPanel);
                this->ui->tabWidget->showTabs();
            });
}

//析构函数，用于清理资源（如关闭串口和销毁 plotMan）并释放内存。
MainWindow::~MainWindow()
{
    if (serialPort.isOpen())
    {
        serialPort.close();
    }

    delete plotMan;

    delete ui;
    ui = NULL; // we check if ui is deleted in messageHandler
}

//重写 QCloseEvent，处理窗口关闭事件。在关闭窗口时，保存设置、检查未保存的快照、关闭串口等。
void MainWindow::closeEvent(QCloseEvent * event)
{
    // save snapshots
    if (!snapshotMan.isAllSaved())
    {
        auto clickedButton = QMessageBox::warning(
            this, "Closing SerialPlot",
            "There are un-saved snapshots. If you close you will loose the data.",
            QMessageBox::Discard, QMessageBox::Cancel);
        if (clickedButton == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
    }

    // save settings
    QSettings settings(PROGRAM_NAME, PROGRAM_NAME);
    saveAllSettings(&settings);
    settings.sync();

    if (settings.status() != QSettings::NoError)
    {
        QString errorText;

        if (settings.status() == QSettings::AccessError)
        {
            QString file = settings.fileName();
            errorText = QString("Serialplot cannot save settings due to access error. \
This happens if you have run serialplot as root (with sudo for ex.) previously. \
Try fixing the permissions of file: %1, or just delete it.").arg(file);
        }
        else
        {
            errorText = QString("Serialplot cannot save settings due to unknown error: %1").\
                arg(settings.status());
        }

        auto button = QMessageBox::critical(
            NULL,
            "Failed to save settings!", errorText,
            QMessageBox::Cancel | QMessageBox::Ok);

        if (button == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
    }

    QMainWindow::closeEvent(event);
}
//设置 "关于" 对话框，显示应用程序的版本和其他相关信息，并为 "关于 Qt" 按钮连接信号。
void MainWindow::setupAboutDialog()
{
    Ui_AboutDialog uiAboutDialog;
    uiAboutDialog.setupUi(&aboutDialog);

    QObject::connect(uiAboutDialog.pbAboutQt, &QPushButton::clicked,
                     [](){ QApplication::aboutQt();});

    QString aboutText = uiAboutDialog.lbAbout->text();
    aboutText.replace("$VERSION_STRING$", VERSION_STRING);
    aboutText.replace("$VERSION_REVISION$", VERSION_REVISION);
    uiAboutDialog.lbAbout->setText(aboutText);
}
//处理串口打开/关闭的操作。如果打开串口并启用了模拟模式，则禁用模拟模式。关闭串口时，重置 spsLabel。
void MainWindow::onPortToggled(bool open)
{
    // make sure demo mode is disabled
    if (open && isDemoRunning()) enableDemo(false);
    ui->actionDemoMode->setEnabled(!open);

    if (!open)
    {
        spsLabel.setText("0sps");
    }
}
//当数据源发生变化时调用，连接新的数据源到 stream 和 sampleCounter
void MainWindow::onSourceChanged(Source* source)
{
    source->connectSink(&stream);
    source->connectSink(&sampleCounter);
}
//清空绘图数据并重新绘制图形。
void MainWindow::clearPlot()
{
    stream.clear();
    plotMan->replot();
}
//当采样数量发生变化时更新 stream 和 plotMan 的配置。
void MainWindow::onNumOfSamplesChanged(int value)
{
    numOfSamples = value;
    stream.setNumSamples(value);
    plotMan->replot();
}
//更新每秒采样数（sps）的显示。
void MainWindow::onSpsChanged(float sps)
{
    int precision = sps < 1. ? 3 : 0;
    spsLabel.setText(QString::number(sps, 'f', precision) + "sps");
}
//判断模拟模式是否正在运行。
bool MainWindow::isDemoRunning()
{
    return ui->actionDemoMode->isChecked();
}
//启用或禁用模拟模式。如果启用模拟模式并且串口未打开，则开启模拟；如果串口已打开，则禁用模拟模式。
void MainWindow::enableDemo(bool enabled)
{
    if (enabled)
    {
        if (!serialPort.isOpen())
        {
            dataFormatPanel.enableDemo(true);
        }
        else
        {
            ui->actionDemoMode->setChecked(false);
        }
    }
    else
    {
        dataFormatPanel.enableDemo(false);
        ui->actionDemoMode->setChecked(false);
    }
}

//显示一个次要的绘图窗口（wid），并将其添加到主窗口的 splitter 中。
void MainWindow::showSecondary(QWidget* wid)
{
    if (secondaryPlot != NULL)
    {
        secondaryPlot->deleteLater();
    }

    secondaryPlot = wid;
    ui->splitter->addWidget(wid);
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 0);
}
//隐藏当前显示的次要绘图窗口，并删除该窗口。
void MainWindow::hideSecondary()
{
    if (secondaryPlot == NULL)
    {
        qFatal("Secondary plot doesn't exist!");
    }

    secondaryPlot->deleteLater();
    secondaryPlot = NULL;
}

//显示或隐藏条形图绘图窗口。如果显示，创建一个 BarPlot 实例并配置 Y 轴范围，然后调用 showSecondary() 显示它。
void MainWindow::showBarPlot(bool show)
{
    if (show)
    {
        auto plot = new BarPlot(&stream, &plotMenu);
        plot->setYAxis(plotControlPanel.autoScale(),
                       plotControlPanel.yMin(),
                       plotControlPanel.yMax());
        connect(&plotControlPanel, &PlotControlPanel::yScaleChanged,
                plot, &BarPlot::setYAxis);
        showSecondary(plot);
    }
    else
    {
        hideSecondary();
    }
}
//导出当前绘图的数据为 CSV 文件。如果绘图正在暂停，则暂停绘图，弹出保存文件对话框，保存数据为 CSV 格式。
void MainWindow::onExportCsv()
{
    bool wasPaused = ui->actionPause->isChecked();
    ui->actionPause->setChecked(true); // pause plotting

    QString fileName = QFileDialog::getSaveFileName(this, tr("Export CSV File"));

    if (fileName.isNull())  // user canceled export
    {
        ui->actionPause->setChecked(wasPaused);
    }
    else
    {
        Snapshot* snapshot = snapshotMan.makeSnapshot();
        snapshot->save(fileName);
        delete snapshot;
    }
}
//导出当前绘图为 SVG 文件。如果绘图正在暂停，则暂停绘图，弹出保存文件对话框，保存数据为 SVG 格式。
void MainWindow::onExportSvg()
{
    bool wasPaused = ui->actionPause->isChecked();
    ui->actionPause->setChecked(true); // pause plotting

    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export SVG File(s)"), QString(), "Images (*.svg)");

    if (fileName.isNull())  // user canceled export
    {
        ui->actionPause->setChecked(wasPaused);
    }
    else
    {
        plotMan->exportSvg(fileName);
    }
}

//返回当前视图的设置（用于绘图）。
PlotViewSettings MainWindow::viewSettings() const
{
    return plotMenu.viewSettings();
}

//自定义的日志处理函数，用于显示调试信息和日志消息。如果不是调试信息，则在状态栏显示该消息。
void MainWindow::messageHandler(QtMsgType type,
                                const QString &logString,
                                const QString &msg)
{
    if (ui != NULL)
        ui->ptLog->appendPlainText(logString);

    if (type != QtDebugMsg && ui != NULL)
    {
        ui->statusBar->showMessage(msg, 5000);
    }
}
//保存所有面板和设置的配置，包括窗口的大小、位置、面板状态、串口设置、绘图设置等。
void MainWindow::saveAllSettings(QSettings* settings)
{
    saveMWSettings(settings);
    portControl.saveSettings(settings);
    dataFormatPanel.saveSettings(settings);
    stream.saveSettings(settings);
    plotControlPanel.saveSettings(settings);
    plotMenu.saveSettings(settings);
    commandPanel.saveSettings(settings);
    recordPanel.saveSettings(settings);
    textView.saveSettings(settings);
    updateCheckDialog.saveSettings(settings);
}

//加载所有面板和设置的配置。
void MainWindow::loadAllSettings(QSettings* settings)
{
    loadMWSettings(settings);
    portControl.loadSettings(settings);
    dataFormatPanel.loadSettings(settings);
    stream.loadSettings(settings);
    plotControlPanel.loadSettings(settings);
    plotMenu.loadSettings(settings);
    commandPanel.loadSettings(settings);
    recordPanel.loadSettings(settings);
    textView.loadSettings(settings);
    updateCheckDialog.loadSettings(settings);
}
//保存主窗口的设置，如窗口的大小、位置、最大化状态、当前面板等。
void MainWindow::saveMWSettings(QSettings* settings)
{
    // save window geometry
    settings->beginGroup(SettingGroup_MainWindow);
    settings->setValue(SG_MainWindow_Size, size());
    settings->setValue(SG_MainWindow_Pos, pos());
    // save active panel
    settings->setValue(SG_MainWindow_ActivePanel,
                       panelSettingMap.value(ui->tabWidget->currentIndex()));
    // save panel minimization
    settings->setValue(SG_MainWindow_HidePanels,
                       ui->tabWidget->hideAction.isChecked());
    // save window maximized state
    settings->setValue(SG_MainWindow_Maximized,
                       bool(windowState() & Qt::WindowMaximized));
    // save toolbar/dockwidgets state
    settings->setValue(SG_MainWindow_State, saveState());
    settings->endGroup();
}
//加载主窗口的设置，如窗口的大小、位置、最大化状态、当前面板等。
void MainWindow::loadMWSettings(QSettings* settings)
{
    settings->beginGroup(SettingGroup_MainWindow);
    // load window geometry
    resize(settings->value(SG_MainWindow_Size, size()).toSize());
    move(settings->value(SG_MainWindow_Pos, pos()).toPoint());

    // set active panel
    QString tabSetting =
        settings->value(SG_MainWindow_ActivePanel, QString()).toString();
    ui->tabWidget->setCurrentIndex(
        panelSettingMap.key(tabSetting, ui->tabWidget->currentIndex()));

    // hide panels
    ui->tabWidget->hideAction.setChecked(
        settings->value(SG_MainWindow_HidePanels,
                        ui->tabWidget->hideAction.isChecked()).toBool());

    // maximize window
    if (settings->value(SG_MainWindow_Maximized).toBool())
    {
        showMaximized();
    }

    // load toolbar/dockwidgets state
    restoreState(settings->value(SG_MainWindow_State).toByteArray());
    settings->setValue(SG_MainWindow_State, saveState());

    settings->endGroup();
}
//打开一个文件对话框，允许用户保存当前设置到一个配置文件（INI 格式）。
void MainWindow::onSaveSettings()
{
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Save Settings"), QString(), "INI (*.ini)");

    if (!fileName.isNull()) // user canceled
    {
        QSettings settings(fileName, QSettings::IniFormat);
        saveAllSettings(&settings);
    }
}
//打开一个文件对话框，允许用户加载一个配置文件（INI 格式）并应用设置
void MainWindow::onLoadSettings()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("Load Settings"), QString(), "INI (*.ini)");

    if (!fileName.isNull()) // user canceled
    {
        QSettings settings(fileName, QSettings::IniFormat);
        loadAllSettings(&settings);
    }
}

//处理命令行选项，如加载配置文件、设置串口、波特率和打开串口。
void MainWindow::handleCommandLineOptions(const QCoreApplication &app)
{
    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsCompactedShortOptions);
    parser.setApplicationDescription("Small and simple software for plotting data from serial port in realtime.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configOpt({"c", "config"}, "Load configuration from file.", "filename");
    QCommandLineOption portOpt({"p", "port"}, "Set port name.", "port name");
    QCommandLineOption baudrateOpt({"b" ,"baudrate"}, "Set port baud rate.", "baud rate");
    QCommandLineOption openPortOpt({"o", "open"}, "Open serial port.");

    parser.addOption(configOpt);
    parser.addOption(portOpt);
    parser.addOption(baudrateOpt);
    parser.addOption(openPortOpt);

    parser.process(app);

    if (parser.isSet(configOpt))
    {
        QString fileName = parser.value(configOpt);
        QFileInfo fileInfo(fileName);

        if (fileInfo.exists() && fileInfo.isFile())
        {
            QSettings settings(fileName, QSettings::IniFormat);
            loadAllSettings(&settings);
        }
        else
        {
            qCritical() << "Configuration file not exist. Closing application.";
            std::exit(1);
        }
    }

    if (parser.isSet(portOpt))
    {
        portControl.selectPort(parser.value(portOpt));
    }

    if (parser.isSet(baudrateOpt))
    {
        portControl.selectBaudrate(parser.value(baudrateOpt));
    }

    if (parser.isSet(openPortOpt))
    {
        portControl.openPort();
    }
}
