#include <QRectF>
#include <QKeySequence>
#include <QColor>
#include <qwt_symbol.h>
#include <qwt_plot_curve.h>
#include <qwt_scale_map.h>
#include <math.h>
#include <algorithm>

#include "plot.h"
#include "utils.h"

// 常量，表示在某些情况下显示符号（数据点的表示）时的尺寸
static const int SYMBOL_SHOW_AT_WIDTH = 5;  // 符号显示的最小宽度
static const int SYMBOL_SIZE_MAX = 7;       // 符号的最大尺寸

// Plot 构造函数
Plot::Plot(QWidget* parent) :
    QwtPlot(parent),  // 调用父类构造函数，初始化绘图控件
    zoomer(this->canvas(), false),  // 初始化缩放器
    sZoomer(this, &zoomer)  // 初始化辅助缩放器
{
    isAutoScaled = true;  // 是否自动缩放
    symbolSize = 0;       // 初始化符号大小
    numOfSamples = 1;     // 初始化样本数量
    plotWidth = 1;        // 初始化绘图宽度
    showSymbols = Plot::ShowSymbolsAuto;  // 自动显示符号

    // 连接缩放器信号与槽
    QObject::connect(&zoomer, &Zoomer::unzoomed, this, &Plot::unzoomed);

    zoomer.setZoomBase();  // 设置缩放器的基本状态
    grid.attach(this);     // 将网格附加到当前绘图
    legend.attach(this);   // 将图例附加到当前绘图

    showGrid(false);       // 默认不显示网格
    darkBackground(false); // 默认使用浅色背景

    snapshotOverlay = NULL;  // 初始化快照叠加层为空

    // 当缩放区域发生变化时，更新横轴的标度
    connect(&zoomer, &QwtPlotZoomer::zoomed,
            [this](const QRectF &rect)
            {
                onXScaleChanged();
            });

    // 当绘图项附加到图表时，更新符号（数据点表示）
    connect(this, &QwtPlot::itemAttached,
            [this](QwtPlotItem *plotItem, bool on)
            {
                if (symbolSize) updateSymbols();
            });

    // 初始化“DEMO运行”标识
    QwtText demoText(" DEMO RUNNING ");
    demoText.setColor(QColor("white"));
    demoText.setBackgroundBrush(Qt::darkRed);
    demoText.setBorderRadius(4);
    demoText.setRenderFlags(Qt::AlignLeft | Qt::AlignBottom);
    demoIndicator.setText(demoText);
    demoIndicator.hide();
    demoIndicator.attach(this);

    // 初始化“无可见通道”标识
    QwtText noChannelText(" No Visible Channels ");
    noChannelText.setColor(QColor("white"));
    noChannelText.setBackgroundBrush(Qt::darkBlue);
    noChannelText.setBorderRadius(4);
    noChannelText.setRenderFlags(Qt::AlignHCenter | Qt::AlignVCenter);
    noChannelIndicator.setText(noChannelText);
    noChannelIndicator.hide();
    noChannelIndicator.attach(this);
}

// Plot 析构函数
Plot::~Plot()
{
    if (snapshotOverlay != NULL) delete snapshotOverlay;
}

// 设置显示的通道
void Plot::setDispChannels(QVector<const StreamChannel*> channels)
{
    zoomer.setDispChannels(channels);
}

// 设置 Y 轴的缩放范围
void Plot::setYAxis(bool autoScaled, double yAxisMin, double yAxisMax)
{
    this->isAutoScaled = autoScaled;

    if (!autoScaled)
    {
        yMin = yAxisMin;
        yMax = yAxisMax;
    }

    zoomer.zoom(0);  // 取消缩放
    resetAxes();     // 重置坐标轴
}

// 设置 X 轴的最小值和最大值
void Plot::setXAxis(double xMin, double xMax)
{
    _xMin = xMin;
    _xMax = xMax;

    zoomer.setXLimits(xMin, xMax);  // 设置 X 轴的缩放范围
    zoomer.zoom(0);  // 取消缩放

    replot();  // 重新绘制图表

    onXScaleChanged();
}

// 重置坐标轴
void Plot::resetAxes()
{
    // 重置 Y 轴
    if (isAutoScaled)
    {
        setAxisAutoScale(QwtPlot::yLeft);  // 自动缩放 Y 轴
    }
    else
    {
        setAxisScale(QwtPlot::yLeft, yMin, yMax);  // 设置 Y 轴的缩放范围
    }

    zoomer.setZoomBase();  // 重置缩放基准
    replot();  // 重新绘制图表
}

// 缩放已取消时的槽函数
void Plot::unzoomed()
{
    resetAxes();  // 重置坐标轴
    onXScaleChanged();
}

// 显示或隐藏网格
void Plot::showGrid(bool show)
{
    grid.enableX(show);  // 启用或禁用 X 轴网格
    grid.enableY(show);  // 启用或禁用 Y 轴网格
    replot();  // 重新绘制图表
}

// 显示或隐藏辅助网格
void Plot::showMinorGrid(bool show)
{
    grid.enableXMin(show);  // 启用或禁用 X 轴辅助网格
    grid.enableYMin(show);  // 启用或禁用 Y 轴辅助网格
    replot();  // 重新绘制图表
}

// 显示或隐藏图例
void Plot::showLegend(bool show)
{
    legend.setVisible(show);  // 设置图例可见性
    replot();  // 重新绘制图表
}

// 显示或隐藏“DEMO运行”标识
void Plot::showDemoIndicator(bool show)
{
    demoIndicator.setVisible(show);  // 设置 DEMO 标识可见性
    replot();  // 重新绘制图表
}

// 显示或隐藏“无可见通道”标识
void Plot::showNoChannel(bool show)
{
    noChannelIndicator.setVisible(show);  // 设置无可见通道标识可见性
    replot();  // 重新绘制图表
}

// 取消缩放
void Plot::unzoom()
{
    zoomer.zoom(0);  // 取消缩放
}

// 设置背景颜色为黑色或白色
void Plot::darkBackground(bool enabled)
{
    QColor gridColor;
    if (enabled)
    {
        setCanvasBackground(QBrush(Qt::black));  // 设置黑色背景

        gridColor.setHsvF(0, 0, 0.30);  // 设置网格颜色
        grid.setMajorPen(gridColor);     // 设置主要网格线的颜色
        gridColor.setHsvF(0, 0, 0.15);
        grid.setMinorPen(gridColor);     // 设置辅助网格线的颜色

        zoomer.setRubberBandPen(QPen(Qt::white));  // 设置缩放带的颜色
        zoomer.setTrackerPen(QPen(Qt::white));    // 设置追踪器颜色
        sZoomer.setPickerPen(QPen(Qt::white));    // 设置选择器颜色

        legend.setTextPen(QPen(Qt::white));  // 设置图例的文本颜色
    }
    else
    {
        setCanvasBackground(QBrush(Qt::white));  // 设置白色背景

        gridColor.setHsvF(0, 0, 0.75);  // 设置网格颜色
        grid.setMajorPen(gridColor);     // 设置主要网格线的颜色
        gridColor.setHsvF(0, 0, 0.90);
        grid.setMinorPen(gridColor);     // 设置辅助网格线的颜色

        zoomer.setRubberBandPen(QPen(Qt::black));  // 设置缩放带的颜色
        zoomer.setTrackerPen(QPen(Qt::black));    // 设置追踪器颜色
        sZoomer.setPickerPen(QPen(Qt::black));    // 设置选择器颜色

        legend.setTextPen(QPen(Qt::black));  // 设置图例的文本颜色
    }
    updateSymbols();  // 更新符号
    replot();  // 重新绘制图表
}

// 快照叠加层，显示或隐藏
void Plot::flashSnapshotOverlay(bool light)
{
    if (snapshotOverlay != NULL) delete snapshotOverlay;  // 删除现有的快照叠加层

    QColor color;
    if(light)
    {
        color = QColor(Qt::white);  // 设置白色背景
    }
    else
    {
        color = QColor(Qt::black);  // 设置黑色背景
    }

    snapshotOverlay = new PlotSnapshotOverlay(this->canvas(), color);  // 创建新的快照叠加层
    connect(snapshotOverlay, &PlotSnapshotOverlay::done,
            [this](){  // 当快照完成时，删除快照叠加层
                delete snapshotOverlay;
                snapshotOverlay = NULL;
            });
}

// 设置符号的显示方式（自动、显示、隐藏）
void Plot::setSymbols(ShowSymbols shown)
{
    showSymbols = shown;

    if (showSymbols == Plot::ShowSymbolsAuto)
    {
        calcSymbolSize();  // 根据当前视图计算符号大小
    }
    else if (showSymbols == Plot::ShowSymbolsShow)
    {
        symbolSize = SYMBOL_SIZE_MAX;  // 设置符号大小为最大值
    }
    else
    {
        symbolSize = 0;  // 隐藏符号
    }

    updateSymbols();  // 更新符号显示
    replot();  // 重新绘制图表
}

// 设置图例的位置
void Plot::setLegendPosition(Qt::AlignmentFlag alignment)
{
    legend.setAlignment(alignment);  // 设置图例的位置
    replot();  // 重新绘制图表
}

// 当 X 轴的标度发生变化时调用
void Plot::onXScaleChanged()
{
    if (showSymbols == Plot::ShowSymbolsAuto)
    {
        calcSymbolSize();  // 重新计算符号大小
        updateSymbols();   // 更新符号显示
    }
}

// 计算符号大小
void Plot::calcSymbolSize()
{
    auto sw = axisWidget(QwtPlot::xBottom);  // 获取 X 轴的坐标轴小部件
    auto paintDist = sw->scaleDraw()->scaleMap().pDist();  // 获取绘制距离
    auto scaleDist = sw->scaleDraw()->scaleMap().sDist();  // 获取标度距离
    auto fullScaleDist = zoomer.zoomBase().width();  // 获取全范围缩放距离
    auto zoomRate = fullScaleDist / scaleDist;  // 计算缩放比例
    float plotWidthNumSamp = abs(numOfSamples * plotWidth / (_xMax - _xMin));  // 绘图宽度对应的样本数
    float samplesInView = plotWidthNumSamp / zoomRate;  // 视图中显示的样本数
    int symDisPx = round(paintDist / samplesInView);  // 计算符号间距的像素值

    // 如果符号间距小于阈值，则不显示符号
    if (symDisPx < SYMBOL_SHOW_AT_WIDTH)
    {
        symbolSize = 0;
    }
    else
    {
        symbolSize = std::min(SYMBOL_SIZE_MAX, symDisPx-SYMBOL_SHOW_AT_WIDTH+1);  // 设置符号的最大尺寸
    }
}

// 更新符号的显示
void Plot::updateSymbols()
{
    const QwtPlotItemList curves = itemList( QwtPlotItem::Rtti_PlotCurve );  // 获取所有曲线项

    if (curves.size() > 0)
    {
        for (int i = 0; i < curves.size(); i++)
        {
            QwtSymbol* symbol = NULL;
            QwtPlotCurve* curve = static_cast<QwtPlotCurve*>(curves[i]);  // 获取每个曲线

            if (symbolSize)  // 如果符号大小大于0，创建符号
            {
                symbol = new QwtSymbol(QwtSymbol::Ellipse,  // 使用圆形符号
                                       canvasBackground(),  // 设置背景颜色
                                       curve->pen(),         // 设置曲线的笔颜色
                                       QSize(symbolSize, symbolSize));  // 设置符号的大小
            }
            curve->setSymbol(symbol);  // 设置曲线的符号
        }
    }
}

// 重写尺寸改变事件处理函数
void Plot::resizeEvent(QResizeEvent * event)
{
    QwtPlot::resizeEvent(event);  // 调用父类的处理函数
    onXScaleChanged();  // 在尺寸改变时更新 X 轴的标度
}

// 设置样本数量
void Plot::setNumOfSamples(unsigned value)
{
    numOfSamples = value;  // 设置样本数量
    onXScaleChanged();     // 更新 X 轴标度
}

// 设置绘图宽度
void Plot::setPlotWidth(double width)
{
    plotWidth = width;  // 设置绘图宽度
    zoomer.setHViewSize(width);  // 设置水平视图的大小
}
