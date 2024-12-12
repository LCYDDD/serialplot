/*
  Copyright © 2023 Hasan Yavuz Özderya

  This file is part of serialplot.

  serialplot is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  serialplot is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with serialplot.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QVariant>
#include <QMessageBox>
#include <QCheckBox>
#include <QStyledItemDelegate>
#include <QColorDialog>

#include <math.h>

#include "plotcontrolpanel.h"
#include "ui_plotcontrolpanel.h"
#include "setting_defines.h"

/// Confirm if #samples is being set to a value greater than this
const int NUMSAMPLES_CONFIRM_AT = 1000000;
/// Precision used for channel info table numbers
const int DOUBLESP_PRECISION = 6;

/// Used for scale range selection combobox
struct Range
{
    double rmin;
    double rmax;
};

Q_DECLARE_METATYPE(Range);

/// Used for customizing double precision in tables
class SpinBoxDelegate : public QStyledItemDelegate
{
public:
    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const Q_DECL_OVERRIDE
        {
            auto w = QStyledItemDelegate::createEditor(
                parent, option, index);

            auto sp = qobject_cast<QDoubleSpinBox*>(w);
            if (sp)
            {
                sp->setDecimals(DOUBLESP_PRECISION);
            }
            return w;
        }
};

PlotControlPanel::PlotControlPanel(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PlotControlPanel),
    resetAct(tr("Reset"), this),
    resetNamesAct(tr("Reset Names"), this),
    resetColorsAct(tr("Reset Colors"), this),
    showAllAct(tr("Show All"), this),
    hideAllAct(tr("Hide All"), this),
    resetGainsAct(tr("Reset All Gain"), this),
    resetOffsetsAct(tr("Reset All Offset"), this),
    resetMenu(tr("Reset Menu"), this)
{
    ui->setupUi(this);

    delegate = new SpinBoxDelegate();
    ui->tvChannelInfo->setItemDelegate(delegate);

    warnNumOfSamples = true;    // TODO: load from settings
    _numOfSamples = ui->spNumOfSamples->value();

    // set limits for axis limit boxes
    ui->spYmin->setRange((-1) * std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::max());

    ui->spYmax->setRange((-1) * std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::max());

    ui->spXmin->setRange((-1) * std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::max());

    ui->spXmax->setRange((-1) * std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::max());

    // connect signals
    connect(ui->spNumOfSamples, SIGNAL(valueChanged(int)),
            this, SLOT(onNumOfSamples(int)));

    connect(ui->cbAutoScale, &QCheckBox::toggled,
            this, &PlotControlPanel::onAutoScaleChecked);

    connect(ui->spYmax, SIGNAL(valueChanged(double)),
            this, SLOT(onYScaleChanged()));

    connect(ui->spYmin, SIGNAL(valueChanged(double)),
            this, SLOT(onYScaleChanged()));

    connect(ui->cbIndex, &QCheckBox::toggled,
            this, &PlotControlPanel::onIndexChecked);

    connect(ui->spXmax, SIGNAL(valueChanged(double)),
            this, SLOT(onXScaleChanged()));

    connect(ui->spXmax, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            [this](double v)
            {
                // set limit just a little below
                double step = pow(10, -1 * ui->spXmin->decimals());
                ui->spXmin->setMaximum(v - step);
            });

    connect(ui->spXmin, SIGNAL(valueChanged(double)),
            this, SLOT(onXScaleChanged()));

    connect(ui->spXmin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            [this](double v)
            {
                // set limit just a little above
                double step = pow(10, -1 * ui->spXmax->decimals());
                ui->spXmax->setMinimum(v + step);
            });

    connect(ui->spPlotWidth, SIGNAL(valueChanged(int)),
            this, SLOT(onPlotWidthChanged()));

    connect(ui->spLineThickness, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int thickness)
            {
                emit lineThicknessChanged(thickness);
            });

    // init scale range preset list
    for (int nbits = 8; nbits <= 24; nbits++) // signed binary formats
    {
        int rmax = pow(2, nbits-1)-1;
        int rmin = -rmax-1;
        Range r = {double(rmin),  double(rmax)};
        ui->cbRangePresets->addItem(
            QString().sprintf("Signed %d bits %d to +%d", nbits, rmin, rmax),
            QVariant::fromValue(r));
    }
    for (int nbits = 8; nbits <= 24; nbits++) // unsigned binary formats
    {
        int rmax = pow(2, nbits)-1;
        ui->cbRangePresets->addItem(
            QString().sprintf("Unsigned %d bits %d to +%d", nbits, 0, rmax),
            QVariant::fromValue(Range{0, double(rmax)}));
    }
    ui->cbRangePresets->addItem("-1 to +1", QVariant::fromValue(Range{-1, +1}));
    ui->cbRangePresets->addItem("0 to +1", QVariant::fromValue(Range{0, +1}));
    ui->cbRangePresets->addItem("-100 to +100", QVariant::fromValue(Range{-100, +100}));
    ui->cbRangePresets->addItem("0 to +100", QVariant::fromValue(Range{0, +100}));

    QObject::connect(ui->cbRangePresets, SIGNAL(activated(int)),
                     this, SLOT(onRangeSelected()));

    // color selector starts disabled until a channel is selected
    ui->pbColorSel->setDisabled(true);
    setSelectorColor(QColor(0,0,0,0));
    connect(ui->pbColorSel, &QPushButton::clicked, this, &PlotControlPanel::onColorSelect);

    // reset buttons
    resetAct.setToolTip(tr("Reset channel names and colors"));
    resetMenu.addAction(&resetNamesAct);
    resetMenu.addAction(&resetColorsAct);
    resetMenu.addAction(&resetGainsAct);
    resetMenu.addAction(&resetOffsetsAct);
    resetAct.setMenu(&resetMenu);
    ui->tbReset->setDefaultAction(&resetAct);

    showAllAct.setToolTip(tr("Show all channels"));
    hideAllAct.setToolTip(tr("Hide all channels"));
    ui->tbShowAll->setDefaultAction(&showAllAct);
    ui->tbHideAll->setDefaultAction(&hideAllAct);
}

PlotControlPanel::~PlotControlPanel()
{
    delete ui;
}

// 获取样本数量
unsigned PlotControlPanel::numOfSamples()
{
    // 返回用户界面中选择的样本数量
    return ui->spNumOfSamples->value();
}

// 当样本数量改变时触发
void PlotControlPanel::onNumOfSamples(int value)
{
    // 如果启用了警告且新值超过了设定的阈值
    if (warnNumOfSamples && value > NUMSAMPLES_CONFIRM_AT)
    {
        // 请求确认对话框
        if (!askNSConfirmation(value))
        {
            // 如果用户取消，则恢复到之前的值
            disconnect(ui->spNumOfSamples, SIGNAL(valueChanged(int)),
                       this, SLOT(onNumOfSamples(int)));

            ui->spNumOfSamples->setValue(_numOfSamples);

            connect(ui->spNumOfSamples, SIGNAL(valueChanged(int)),
                    this, SLOT(onNumOfSamples(int)));

            return;
        }
    }

    // 更新内部存储的样本数量，并发出信号通知变化
    _numOfSamples = value;
    emit numOfSamplesChanged(value);
}

// 请求确认大样本数量设置
bool PlotControlPanel::askNSConfirmation(int value)
{
    // 构建警告信息文本
    auto text = tr("Setting number of samples to a too big value "
                   "(>%1) can seriously impact the performance of "
                   "the application and cause freezes. Are you sure you "
                   "want to change the number of samples to %2?")
        .arg(QString::number(NUMSAMPLES_CONFIRM_AT), QString::number(value));

    // 创建一个消息框，询问用户是否继续
    QMessageBox mb(QMessageBox::Warning,
                   tr("Confirm Number of Samples"),
                   text,
                   QMessageBox::Apply | QMessageBox::Cancel,
                   this);

    // 添加复选框，允许用户不再显示此警告
    auto cb = new QCheckBox("Don't show this again.");
    connect(cb, &QCheckBox::stateChanged, [this](int state)
            {
                warnNumOfSamples = (state == Qt::Unchecked);
            });

    mb.setCheckBox(cb);

    // 返回用户的选择（Apply 或 Cancel）
    return mb.exec() == QMessageBox::Apply;
}

// 设置颜色选择器的颜色
void PlotControlPanel::setSelectorColor(QColor color)
{
    // 设置颜色选择按钮的背景色
    ui->pbColorSel->setStyleSheet(QString("background-color: %1;").arg(color.name()));
}

// 当选择颜色时触发
void PlotControlPanel::onColorSelect()
{
    // 获取当前选择的行索引
    auto selection = ui->tvChannelInfo->selectionModel()->currentIndex();
    // 没有选择则直接返回
    if (!selection.isValid()) return;

    // 从模型中获取当前颜色
    auto model = ui->tvChannelInfo->model();
    QColor color = model->data(selection, Qt::ForegroundRole).value<QColor>();

    // 显示颜色选择对话框
    color = QColorDialog::getColor(color, this);

    // 如果用户选择了有效颜色，则更新模型中的颜色
    if (color.isValid())
    {
        ui->tvChannelInfo->model()->setData(selection, color, Qt::ForegroundRole);
    }
}

// 当自动缩放选项被选中或取消时触发
void PlotControlPanel::onAutoScaleChecked(bool checked)
{
    if (checked)
    {
        // 禁用Y轴范围控件并发出自动缩放信号
        ui->lYmin->setEnabled(false);
        ui->lYmax->setEnabled(false);
        ui->spYmin->setEnabled(false);
        ui->spYmax->setEnabled(false);

        emit yScaleChanged(true); // autoscale
    }
    else
    {
        // 启用Y轴范围控件并发出手动缩放信号
        ui->lYmin->setEnabled(true);
        ui->lYmax->setEnabled(true);
        ui->spYmin->setEnabled(true);
        ui->spYmax->setEnabled(true);

        emit yScaleChanged(false, ui->spYmin->value(), ui->spYmax->value());
    }
}

// 当Y轴范围改变时触发
void PlotControlPanel::onYScaleChanged()
{
    // 如果不是自动缩放，则发出新的Y轴范围信号
    if (!autoScale())
    {
        emit yScaleChanged(false, ui->spYmin->value(), ui->spYmax->value());
    }
}

// 检查是否启用了自动缩放
bool PlotControlPanel::autoScale() const
{
    return ui->cbAutoScale->isChecked();
}

// 获取Y轴最大值
double PlotControlPanel::yMax() const
{
    return ui->spYmax->value();
}

// 获取Y轴最小值
double PlotControlPanel::yMin() const
{
    return ui->spYmin->value();
}

// 检查是否使用索引作为X轴
bool PlotControlPanel::xAxisAsIndex() const
{
    return ui->cbIndex->isChecked();
}

// 获取X轴最大值
double PlotControlPanel::xMax() const
{
    return ui->spXmax->value();
}

// 获取X轴最小值
double PlotControlPanel::xMin() const
{
    return ui->spXmin->value();
}

// 当预设范围被选择时触发
void PlotControlPanel::onRangeSelected()
{
    // 从组合框中获取选定的范围，并更新Y轴范围控件
    Range r = ui->cbRangePresets->currentData().value<Range>();
    ui->spYmin->setValue(r.rmin);
    ui->spYmax->setValue(r.rmax);
    ui->cbAutoScale->setChecked(false);
}

// 当索引作为X轴选项被选中或取消时触发
void PlotControlPanel::onIndexChecked(bool checked)
{
    if (checked)
    {
        // 禁用X轴范围控件并发出使用索引信号
        ui->lXmin->setEnabled(false);
        ui->lXmax->setEnabled(false);
        ui->spXmin->setEnabled(false);
        ui->spXmax->setEnabled(false);

        emit xScaleChanged(true); // use index
    }
    else
    {
        // 启用X轴范围控件并发出手动缩放信号
        ui->lXmin->setEnabled(true);
        ui->lXmax->setEnabled(true);
        ui->spXmin->setEnabled(true);
        ui->spXmax->setEnabled(true);

        emit xScaleChanged(false, ui->spXmin->value(), ui->spXmax->value());
    }
    emit plotWidthChanged(plotWidth());
}

// 当X轴范围改变时触发
void PlotControlPanel::onXScaleChanged()
{
    // 如果不是使用索引作为X轴，则发出新的X轴范围信号
    if (!xAxisAsIndex())
    {
        emit xScaleChanged(false, ui->spXmin->value(), ui->spXmax->value());
        emit plotWidthChanged(plotWidth());
    }
}

// 计算绘图宽度
double PlotControlPanel::plotWidth() const
{
    double value = ui->spPlotWidth->value();
    if (!xAxisAsIndex())
    {
        // 根据X轴范围缩放绘图宽度
        auto xmax = ui->spXmax->value();
        auto xmin = ui->spXmin->value();
        double scale = (xmax - xmin) / _numOfSamples;
        value *= scale;
    }
    return value;
}

// 当绘图宽度改变时触发
void PlotControlPanel::onPlotWidthChanged()
{
    emit plotWidthChanged(plotWidth());
}

// 设置通道信息模型
void PlotControlPanel::setChannelInfoModel(ChannelInfoModel* model)
{
    ui->tvChannelInfo->setModel(model);

    // 连接选择模型的信号与槽，用于更新颜色选择器
    connect(ui->tvChannelInfo->selectionModel(), &QItemSelectionModel::currentRowChanged,
            [this](const QModelIndex &current, const QModelIndex &previous)
            {
                QColor color(0,0,0,0); // transparent

                if (current.isValid())
                {
                    ui->pbColorSel->setEnabled(true);
                    auto model = ui->tvChannelInfo->model();
                    color = model->data(current, Qt::ForegroundRole).value<QColor>();
                }
                else
                {
                    ui->pbColorSel->setDisabled(true);
                }

                setSelectorColor(color);
            });

    connect(ui->tvChannelInfo->selectionModel(), &QItemSelectionModel::selectionChanged,
            [this](const QItemSelection & selected, const QItemSelection & deselected)
            {
                if (!selected.length())
                {
                    ui->pbColorSel->setDisabled(true);
                    setSelectorColor(QColor(0,0,0,0));
                }
            });

    // 当模型数据更改时，更新颜色选择器
    connect(model, &QAbstractItemModel::dataChanged,
            [this](const QModelIndex & topLeft, const QModelIndex & bottomRight, const QVector<int> & roles = QVector<int> ())
            {
                auto current = ui->tvChannelInfo->selectionModel()->currentIndex();

                // no current selection
                if (!current.isValid()) return;

                auto mod = ui->tvChannelInfo->model();
                QColor color = mod->data(current, Qt::ForegroundRole).value<QColor>();
                setSelectorColor(color);
            });

    // 连接重置动作与模型方法
    connect(&resetAct, &QAction::triggered, model, &ChannelInfoModel::resetInfos);
    connect(&resetNamesAct, &QAction::triggered, model, &ChannelInfoModel::resetNames);
    connect(&resetColorsAct, &QAction::triggered, model, &ChannelInfoModel::resetColors);
    connect(&resetGainsAct, &QAction::triggered, model, &ChannelInfoModel::resetGains);
    connect(&resetOffsetsAct, &QAction::triggered, model, &ChannelInfoModel::resetOffsets);
    connect(&showAllAct, &QAction::triggered, [model]{model->resetVisibility(true);});
    connect(&hideAllAct, &QAction::triggered, [model]{model->resetVisibility(false);});
}

// 保存设置到QSettings对象
void PlotControlPanel::saveSettings(QSettings* settings)
{
    settings->beginGroup(SettingGroup_Plot);
    settings->setValue(SG_Plot_NumOfSamples, numOfSamples());
    settings->setValue(SG_Plot_PlotWidth, ui->spPlotWidth->value());
    settings->setValue(SG_Plot_IndexAsX, xAxisAsIndex());
    settings->setValue(SG_Plot_XMax, xMax());
    settings->setValue(SG_Plot_XMin, xMin());
    settings->setValue(SG_Plot_AutoScale, autoScale());
    settings->setValue(SG_Plot_YMax, yMax());
    settings->setValue(SG_Plot_YMin, yMin());
    settings->setValue(SG_Plot_LineThickness, ui->spLineThickness->value());
    settings->endGroup();
}

// 从QSettings对象加载设置
void PlotControlPanel::loadSettings(QSettings* settings)
{
    settings->beginGroup(SettingGroup_Plot);
    ui->spNumOfSamples->setValue(
        settings->value(SG_Plot_NumOfSamples, numOfSamples()).toInt());
    ui->spPlotWidth->setValue(
        settings->value(SG_Plot_PlotWidth, ui->spPlotWidth->value()).toInt());
    ui->cbIndex->setChecked(
        settings->value(SG_Plot_IndexAsX, xAxisAsIndex()).toBool());
    ui->spXmax->setValue(settings->value(SG_Plot_XMax, xMax()).toDouble());
    ui->spXmin->setValue(settings->value(SG_Plot_XMin, xMin()).toDouble());
    ui->cbAutoScale->setChecked(
        settings->value(SG_Plot_AutoScale, autoScale()).toBool());
    ui->spYmax->setValue(settings->value(SG_Plot_YMax, yMax()).toDouble());
    ui->spYmin->setValue(settings->value(SG_Plot_YMin, yMin()).toDouble());
    ui->spLineThickness->setValue(
        settings->value(SG_Plot_LineThickness, ui->spLineThickness->value()).toInt());
    settings->endGroup();
}
