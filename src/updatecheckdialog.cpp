#include "setting_defines.h"
#include "updatecheckdialog.h"
#include "ui_updatecheckdialog.h"

// UpdateCheckDialog 构造函数
UpdateCheckDialog::UpdateCheckDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UpdateCheckDialog) // 初始化 UI
{
    ui->setupUi(this); // 设置 UI 界面

    // 默认情况下设置上次检查时间为昨天，确保第一次启动时检查更新
    lastCheck = QDate::currentDate().addDays(-1);

    // 连接更新检查失败信号
    connect(&updateChecker, &UpdateChecker::checkFailed,
            [this](QString errorMessage) {
                lastCheck = QDate::currentDate(); // 设置上次检查为今天
                // 显示错误信息
                ui->label->setText(QString("Update check failed.\n") + errorMessage);
                qCritical() << "Update error:" << errorMessage; // 打印错误到日志
            });

    // 连接更新检查完成信号
    connect(&updateChecker, &UpdateChecker::checkFinished,
            [this](bool found, QString newVersion, QString downloadUrl) {
                QString text;
                // 如果没有找到更新
                if (!found)
                {
                    text = "There is no update yet."; // 显示没有更新
                }
                else // 如果找到了更新
                {
                    show(); // 显示更新对话框
#ifdef UPDATE_TYPE_PKGMAN
                    // 如果使用的是包管理器更新
                    text = QString("There is a new version: %1. "
                                   "Use your package manager to update"
                                   " or click to <a href=\"%2\">download</a>.")\
                        .arg(newVersion).arg(downloadUrl);
#else
                    // 如果不是包管理器更新，显示直接下载链接
                    text = QString("Found update to version %1. Click to <a href=\"%2\">download</a>.")\
                        .arg(newVersion).arg(downloadUrl);
#endif
                }

                lastCheck = QDate::currentDate(); // 更新上次检查日期
                ui->label->setText(text); // 设置更新状态文本
            });
}

// 析构函数，销毁 UI
UpdateCheckDialog::~UpdateCheckDialog()
{
    delete ui;
}

// 在对话框显示时触发更新检查
void UpdateCheckDialog::showEvent(QShowEvent *event)
{
    updateChecker.checkUpdate(); // 调用更新检查器执行更新检查
    ui->label->setText("Checking update..."); // 显示“检查更新”文本
}

// 在对话框关闭时，如果有正在进行的更新检查，则取消检查
void UpdateCheckDialog::closeEvent(QShowEvent *event)
{
    if (updateChecker.isChecking()) updateChecker.cancelCheck(); // 取消检查
}

// 保存设置到 QSettings 中
void UpdateCheckDialog::saveSettings(QSettings* settings)
{
    settings->beginGroup(SettingGroup_UpdateCheck); // 设置保存的组名
    settings->setValue(SG_UpdateCheck_Periodic, ui->cbPeriodic->isChecked()); // 保存是否启用周期性检查
    settings->setValue(SG_UpdateCheck_LastCheck, lastCheck.toString(Qt::ISODate)); // 保存上次检查时间
    settings->endGroup(); // 结束保存组
}

// 从 QSettings 中加载设置
void UpdateCheckDialog::loadSettings(QSettings* settings)
{
    settings->beginGroup(SettingGroup_UpdateCheck); // 设置加载的组名
    // 加载周期性检查的选项
    ui->cbPeriodic->setChecked(settings->value(SG_UpdateCheck_Periodic,
                                               ui->cbPeriodic->isChecked()).toBool());
    // 加载上次检查的日期
    auto lastCheckS = settings->value(SG_UpdateCheck_LastCheck, lastCheck.toString(Qt::ISODate)).toString();
    lastCheck = QDate::fromString(lastCheckS, Qt::ISODate); // 将字符串转回日期
    settings->endGroup(); // 结束加载组

    // 如果启用了周期性更新检查且上次检查日期早于今天，则开始检查更新
    if (ui->cbPeriodic->isChecked() && lastCheck < QDate::currentDate())
    {
        updateChecker.checkUpdate(); // 执行更新检查
    }
}
