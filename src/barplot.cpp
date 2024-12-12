#include "barplot.h"
#include "barscaledraw.h"
#include "utils.h"

// BarPlot 构造函数
BarPlot::BarPlot(Stream* stream, PlotMenu* menu, QWidget* parent) :
    QwtPlot(parent),  // 调用父类 QwtPlot 构造函数
    _menu(menu),      // 初始化菜单对象
    barChart(stream)  // 初始化条形图对象，传入数据流（Stream）
{
    _stream = stream;  // 存储传入的 Stream 对象
    barChart.attach(this);  // 将条形图附加到当前的绘图对象上

    // 设置 X 轴的次级刻度为 0（不显示次级刻度）
    setAxisMaxMinor(QwtPlot::xBottom, 0);

    // 使用自定义的 BarScaleDraw 对象来绘制 X 轴的刻度
    setAxisScaleDraw(QwtPlot::xBottom, new BarScaleDraw(stream));

    update();  // 初始化更新绘图

    // 连接信号与槽，监听 Stream 数据的变化
    connect(_stream, &Stream::dataAdded, this, &BarPlot::update);
    connect(_stream, &Stream::numChannelsChanged, this, &BarPlot::update);

    // 连接菜单中的操作，用于切换背景颜色
    connect(&menu->darkBackgroundAction, SELECT<bool>::OVERLOAD_OF(&QAction::toggled),
            this, &BarPlot::darkBackground);

    // 设置背景颜色，默认为菜单中对应动作的状态
    darkBackground(menu->darkBackgroundAction.isChecked());
}

// 更新图表
void BarPlot::update()
{
    // 设置 X 轴的缩放范围，`-0.99` 是为了避免 `numOfChannels` 为 1 时出现偏差
    setAxisScale(QwtPlot::xBottom, 0, _stream->numChannels()-0.99, 1);

    // 重新采样条形图数据
    barChart.resample();

    // 重新绘制图表
    replot();
}

// 设置 Y 轴的缩放范围
void BarPlot::setYAxis(bool autoScaled, double yMin, double yMax)
{
    if (autoScaled)
    {
        // 如果启用自动缩放，则自动调整 Y 轴的范围
        setAxisAutoScale(QwtPlot::yLeft);
    }
    else
    {
        // 否则，使用提供的最小值和最大值来设置 Y 轴的范围
        setAxisScale(QwtPlot::yLeft, yMin, yMax);
    }
}

// 切换背景色（暗色或亮色背景）
void BarPlot::darkBackground(bool enabled)
{
    if (enabled)
    {
        // 如果启用暗色背景，设置绘图区域背景为黑色
        setCanvasBackground(QBrush(Qt::black));
    }
    else
    {
        // 否则，设置绘图区域背景为白色
        setCanvasBackground(QBrush(Qt::white));
    }

    // 重新绘制图表以应用背景色变化
    replot();
}
