#include <algorithm>         // 引入标准库算法操作，如std::none_of
#include <QMetaEnum>         // 用于处理Qt的枚举类型
#include <QSvgGenerator>     // 用于生成SVG格式的文件
#include <qwt_symbol.h>      // Qwt库中的绘图符号支持
#include <qwt_plot_renderer.h> // Qwt库中的绘图渲染支持

#include "plot.h"            // 包含绘图组件的定义
#include "plotmanager.h"     // 当前类的定义
#include "utils.h"           // 一些通用工具方法
#include "setting_defines.h" // 全局设置的定义

// 构造函数：基于流创建PlotManager
PlotManager::PlotManager(QWidget* plotArea, PlotMenu* menu,
                         const Stream* stream, QObject* parent) :
    QObject(parent) // 初始化基类QObject
{
    _stream = stream;  // 存储Stream对象的引用
    construct(plotArea, menu); // 调用通用构造函数逻辑
    if (_stream == nullptr) return; // 如果stream为空，直接返回

    // 获取流的ChannelInfoModel，用于监听通道信息变化
    infoModel = _stream->infoModel();
    connect(infoModel, &QAbstractItemModel::dataChanged,
                this, &PlotManager::onChannelInfoChanged); // 当通道数据改变时，触发更新
    connect(infoModel, &QAbstractItemModel::modelReset,
            [this]() // Lambda表达式，当模型重置时更新所有通道信息
            {
                onChannelInfoChanged(infoModel->index(0, 0), // 起始索引
                                     infoModel->index(infoModel->rowCount()-1, 0), // 结束索引
                                     {}); // 忽略的角色
            });

    // 监听流的通道数量变化，更新绘图
    connect(stream, &Stream::numChannelsChanged, this, &PlotManager::onNumChannelsChanged);
    connect(stream, &Stream::dataAdded, this, &PlotManager::replot); // 数据更新时重绘

    // 添加流的所有初始曲线
    for (unsigned int i = 0; i < stream->numChannels(); i++)
    {
        addCurve(stream->channel(i)->name(), // 曲线名称
                 stream->channel(i)->xData(), // 曲线的X轴数据
                 stream->channel(i)->yData()); // 曲线的Y轴数据
    }
}

// 构造函数：基于Snapshot创建PlotManager
PlotManager::PlotManager(QWidget* plotArea, PlotMenu* menu,
                         Snapshot* snapshot, QObject *parent) :
    QObject(parent) // 初始化基类QObject
{
    _stream = nullptr; // 对于Snapshot，流信息为空
    construct(plotArea, menu); // 调用通用构造函数逻辑

    // 从Snapshot中初始化样本数量和绘图宽度
    setNumOfSamples(snapshot->numSamples());
    setPlotWidth(snapshot->numSamples());
    infoModel = snapshot->infoModel(); // 获取Snapshot的ChannelInfoModel

    // 遍历Snapshot中的每个通道，添加曲线
    for (unsigned ci = 0; ci < snapshot->numChannels(); ci++)
    {
        addCurve(snapshot->channelName(ci), // 通道名称
                 snapshot->xData[ci],      // 通道的X轴数据
                 snapshot->yData[ci]);     // 通道的Y轴数据
    }

    // 监听通道信息变化
    connect(infoModel, &QAbstractItemModel::dataChanged,
            this, &PlotManager::onChannelInfoChanged);

    // TODO: 移除多显示支持后，检查所有隐藏的通道
    checkNoVisChannels();
}

// 私有方法：通用构造逻辑
void PlotManager::construct(QWidget* plotArea, PlotMenu* menu)
{
    _menu = menu;              // 保存菜单引用
    _plotArea = plotArea;      // 保存绘图区域引用
    _autoScaled = true;        // 默认启用自动缩放
    _yMin = 0;                 // Y轴最小值
    _yMax = 1;                 // Y轴最大值
    _xAxisAsIndex = true;      // X轴是否作为索引
    isDemoShown = false;       // 默认不显示演示标志
    _numOfSamples = 1;         // 初始样本数量
    _plotWidth = 1;            // 初始绘图宽度
    showSymbols = Plot::ShowSymbolsAuto; // 默认符号显示模式
    emptyPlot = NULL;          // 空绘图指针初始化为NULL
    inScaleSync = false;       // 默认不同步刻度
    lineThickness = 1;         // 初始线条粗细为1

    // 初始化布局为单绘图模式
    isMulti = false;
    scrollArea = NULL;         // 初始化滚动区域为空

    // 连接菜单信号与绘图管理器槽函数
    connect(menu, &PlotMenu::symbolShowChanged, this, &PlotManager::setSymbols);

    // 连接菜单中的各项操作，如显示网格、暗背景等
    connect(&menu->showGridAction, SELECT<bool>::OVERLOAD_OF(&QAction::toggled),
            this, &PlotManager::showGrid);
    connect(&menu->showMinorGridAction, SELECT<bool>::OVERLOAD_OF(&QAction::toggled),
            this, &PlotManager::showMinorGrid);
    connect(&menu->darkBackgroundAction, SELECT<bool>::OVERLOAD_OF(&QAction::toggled),
            this, &PlotManager::darkBackground);
    connect(&menu->showMultiAction, SELECT<bool>::OVERLOAD_OF(&QAction::toggled),
            this, &PlotManager::setMulti);
    connect(&menu->unzoomAction, &QAction::triggered,
            this, &PlotManager::unzoom);

    connect(&menu->showLegendAction, SELECT<bool>::OVERLOAD_OF(&QAction::toggled),
            this, &PlotManager::showLegend);
    connect(menu, &PlotMenu::legendPosChanged, this, &PlotManager::setLegendPosition);

    // 初始化菜单的各项默认设置
    showGrid(menu->showGridAction.isChecked());
    showMinorGrid(menu->showMinorGridAction.isChecked());
    darkBackground(menu->darkBackgroundAction.isChecked());
    showLegend(menu->showLegendAction.isChecked());
    setLegendPosition(menu->legendPosition());
    setMulti(menu->showMultiAction.isChecked());
}


// PlotManager 析构函数
PlotManager::~PlotManager()
{
    while (curves.size())
    {
        delete curves.takeLast(); // 删除列表中最后一个曲线
    }

    // 删除所有图形小部件
    while (plotWidgets.size())
    {
        delete plotWidgets.takeLast(); // 删除列表中最后一个图形小部件
    }

    // 删除滚动区域（如果存在）
    if (scrollArea != NULL) delete scrollArea;

    // 删除空白图（如果存在）
    if (emptyPlot != NULL) delete emptyPlot;
}


// 处理通道数量变化
void PlotManager::onNumChannelsChanged(unsigned value)
{
    unsigned int oldNum = numOfCurves(); // 获取当前曲线数量
    unsigned numOfChannels = value; // 新的通道数量

    if (numOfChannels > oldNum)
    {
        // 添加新通道
        for (unsigned int i = oldNum; i < numOfChannels; i++)
        {
            addCurve(_stream->channel(i)->name(), _stream->channel(i)->xData(), _stream->channel(i)->yData());
        }
    }
    else if(numOfChannels < oldNum)
    {
        // 删除多余通道
        removeCurves(oldNum - numOfChannels);
    }

    replot(); // 重绘所有图表以反映更改
}


// 处理通道信息变化
void PlotManager::onChannelInfoChanged(const QModelIndex &topLeft,
                                       const QModelIndex &bottomRight,
                                       const QVector<int> &roles)
{
    int start = topLeft.row(); // 变化范围的开始行索引
    int end = bottomRight.row(); // 变化范围的结束行索引

    for (int ci = start; ci <= end; ci++)
    {
        // 获取更新后的通道信息
        QString name = topLeft.sibling(ci, ChannelInfoModel::COLUMN_NAME).data(Qt::EditRole).toString();
        QColor color = topLeft.sibling(ci, ChannelInfoModel::COLUMN_NAME).data(Qt::ForegroundRole).value<QColor>();
        bool visible = topLeft.sibling(ci, ChannelInfoModel::COLUMN_VISIBILITY).data(Qt::CheckStateRole).toBool();

        // 更新曲线属性
        curves[ci]->setTitle(name); // 设置曲线标题
        curves[ci]->setPen(color, lineThickness); // 更新线的颜色和粗细
        curves[ci]->setVisible(visible); // 设置曲线的可见性
        curves[ci]->setItemAttribute(QwtPlotItem::Legend, visible); // 更新图例的可见性

        // 多图表模式下，只更新更新的小部件
        if (isMulti)
        {
            plotWidgets[ci]->updateSymbols(); // 更新符号以反映颜色变化
            plotWidgets[ci]->updateLegend(curves[ci]); // 更新此曲线的图例
            plotWidgets[ci]->setVisible(visible); // 设置图形小部件的可见性
            if (visible)
            {
                plotWidgets[ci]->replot(); // 如果可见，则重绘
            }
            syncScales(); // 确保所有图表的比例一致
        }
    }

    checkNoVisChannels(); // 检查是否有隐藏的通道并更新状态

    // 单图表模式下，重绘单个小部件
    if (!isMulti)
    {
        plotWidgets[0]->updateSymbols(); // 更新符号
        plotWidgets[0]->updateLegend(); // 更新单个小部件的图例
        replot(); // 重绘图表
    }
}


// 检查是否所有通道都隐藏，如果是，则显示指示器
void PlotManager::checkNoVisChannels()
{
    // 如果所有曲线都不可见，则显示指示器
    bool allhidden = std::none_of(curves.cbegin(), curves.cend(),
                                  [](QwtPlotCurve* c) { return c->isVisible(); });

    plotWidgets[0]->showNoChannel(allhidden); // 在多图表模式下也需要更新第一个小部件
    if (isMulti)
    {
        plotWidgets[0]->showNoChannel(allhidden); // 显示指示器
        plotWidgets[0]->setVisible(true); // 确保指示器可见
    }
}

// 设置是否启用多图表模式
void PlotManager::setMulti(bool enabled)
{
    isMulti = enabled; // 设置多图表模式的状态

    // 分离所有曲线
    for (auto curve : curves)
    {
        curve->detach(); // 从所有小部件中分离曲线
    }

    // 删除所有图形小部件
    while (plotWidgets.size())
    {
        delete plotWidgets.takeLast(); // 删除列表中最后一个小部件
    }

    // 设置新的布局
    setupLayout(isMulti);

    if (isMulti)
    {
        // 添加新小部件并连接曲线
        int i = 0;
        for (auto curve : curves)
        {
            auto plot = addPlotWidget(); // 创建新的图形小部件
            plot->setVisible(curve->isVisible()); // 设置小部件的可见性
            if (_stream != nullptr)
            {
                plot->setDispChannels(QVector<const StreamChannel*>(1, _stream->channel(i))); // 设置显示的通道
            }
            curve->attach(plot); // 将曲线附加到新的小部件
            i++;
        }
    }
    else
    {
        // 添加单个图形小部件
        auto plot = addPlotWidget();

        if (_stream != nullptr)
        {
            plot->setDispChannels(_stream->allChannels()); // 显示所有通道
        }

        // 附加所有曲线到单个图形小部件
        for (auto curve : curves)
        {
            curve->attach(plot);
        }
    }

    // 调用同步方法，确保小部件准备好时同步比例尺
    QMetaObject::invokeMethod(this, "syncScales", Qt::QueuedConnection);

    // 如果存在图形小部件，检查通道是否全部隐藏
    if (plotWidgets.length())
    {
        checkNoVisChannels();
    }
}


// 设置布局
void PlotManager::setupLayout(bool multiPlot)
{
    // 删除先前的布局（如果存在）
    if (_plotArea->layout() != 0)
    {
        delete _plotArea->layout();
    }

    if (multiPlot)
    {
        // 设置滚动区域
        scrollArea = new QScrollArea();
        auto scrolledPlotArea = new QWidget(scrollArea);
        scrollArea->setWidget(scrolledPlotArea);
        scrollArea->setWidgetResizable(true); // 自动调整滚动区域大小

        _plotArea->setLayout(new QVBoxLayout());
        _plotArea->layout()->addWidget(scrollArea); // 添加滚动区域到主布局
        _plotArea->layout()->setContentsMargins(0, 0, 0, 0); // 设置边距为0

        layout = new QVBoxLayout(scrolledPlotArea); // 设置滚动区域内部布局
    }
    else
    {
        // 删除多图表模式的滚动区域
        if (scrollArea != NULL)
        {
            delete scrollArea;
            scrollArea = NULL;
        }

        layout = new QVBoxLayout(_plotArea); // 设置主布局
    }

    layout->setContentsMargins(2, 2, 2, 2); // 设置布局边距
    layout->setSpacing(1); // 设置布局间隔
}


// 添加图形小部件
Plot* PlotManager::addPlotWidget()
{
    auto plot = new Plot();
    plotWidgets.append(plot); // 将新小部件添加到列表中
    layout->addWidget(plot); // 添加小部件到布局中

    // 根据菜单设置小部件的显示属性
    plot->darkBackground(_menu->darkBackgroundAction.isChecked());
    plot->showGrid(_menu->showGridAction.isChecked());
    plot->showMinorGrid(_menu->showMinorGridAction.isChecked());
    plot->showLegend(_menu->showLegendAction.isChecked());
    plot->setLegendPosition(_menu->legendPosition());
    plot->setSymbols(_menu->showSymbols());

    plot->showDemoIndicator(isDemoShown); // 显示演示指示器
    plot->setYAxis(_autoScaled, _yMin, _yMax);
    plot->setNumOfSamples(_numOfSamples);

    plot->setPlotWidth(_plotWidth);
    if (_xAxisAsIndex)
    {
        plot->setXAxis(0, _numOfSamples); // 设置X轴为索引
    }
    else
    {
        plot->setXAxis(_xMin, _xMax); // 设置X轴范围
    }

    if (isMulti)
    {
        // 如果是多图表模式，连接Y轴的改变信号
        connect(plot->axisWidget(QwtPlot::yLeft), &QwtScaleWidget::scaleDivChanged,
                this, &PlotManager::syncScales);
    }

    return plot;
}


// Taken from Qwt "plotmatrix" playground example 同步绘图的刻度
void PlotManager::syncScales()
{
    // 如果正在同步刻度，则返回
    if (inScaleSync) return;

    // 设置同步状态为正在进行
    inScaleSync = true;

    // 查找最大刻度
    double maxExtent = 0;
    for (auto plot : plotWidgets)
    {
        // 如果图窗口不可见，则跳过
        if (!plot->isVisible()) continue;

        // 获取Y轴刻度组件
        QwtScaleWidget* scaleWidget = plot->axisWidget(QwtPlot::yLeft);
        QwtScaleDraw* scaleDraw = scaleWidget->scaleDraw();
        scaleDraw->setMinimumExtent(0);

        // 获取刻度的大小
        const double extent = scaleDraw->extent(scaleWidget->font());
        if (extent > maxExtent)
            maxExtent = extent;
    }

    // 应用最大刻度到所有图窗口
    for (auto plot : plotWidgets)
    {
        QwtScaleWidget* scaleWidget = plot->axisWidget(QwtPlot::yLeft);
        scaleWidget->scaleDraw()->setMinimumExtent(maxExtent);
        scaleWidget->updateGeometry();
    }

    // 重绘所有图窗口
    for (auto plot : plotWidgets)
    {
        plot->replot();
    }

    // 重置同步状态
    inScaleSync = false;
}


// 添加曲线
void PlotManager::addCurve(QString title, const XFrameBuffer* xBuf, const FrameBuffer* yBuf)
{
    auto curve = new QwtPlotCurve(title); // 创建一个新的曲线
    auto series = new FrameBufferSeries(xBuf, yBuf); // 创建数据系列
    curve->setSamples(series); // 设置样本数据
    _addCurve(curve); // 添加曲线到管理器中
}

// 内部函数：将曲线添加到管理器中
void PlotManager::_addCurve(QwtPlotCurve* curve)
{
    // 存储和初始化曲线
    curves.append(curve); // 添加到曲线列表中

    unsigned index = curves.size() - 1; // 获取曲线的索引
    auto color = infoModel->color(index); // 获取曲线的颜色
    curve->setPen(color, lineThickness); // 设置曲线的笔（线条）属性

    // 创建曲线对应的图形小部件（plot）
    Plot* plot;
    if (isMulti)
    {
        plot = addPlotWidget(); // 如果是多图表模式，创建一个新的小部件
    }
    else
    {
        plot = plotWidgets[0]; // 否则使用主小部件
    }

    if (_stream != nullptr) // 如果不是在显示快照
    {
        QVector<const StreamChannel*> dispChannels;
        if (isMulti)
        {
            dispChannels = QVector<const StreamChannel*>(1, _stream->channel(index));
        }
        else
        {
            dispChannels = _stream->allChannels();
        }
        plot->setDispChannels(dispChannels); // 设置显示的通道
    }

    curve->attach(plot); // 显示曲线
    checkNoVisChannels(); // 检查是否所有通道都被隐藏
    plot->replot(); // 重绘图形
}

// 移除指定数量的曲线
void PlotManager::removeCurves(unsigned number)
{
    if (_stream != nullptr) // 如果不是在显示快照
    {
        if (!isMulti)
        {
            QVector<const StreamChannel*> dispChannels;
            dispChannels = _stream->allChannels();
            plotWidgets[0]->setDispChannels(dispChannels);
        }
    }

    for (unsigned i = 0; i < number; i++)
    {
        if (!curves.isEmpty())
        {
            delete curves.takeLast(); // 删除最后一个曲线
            if (isMulti) // 如果是多图表模式，删除对应的小部件
            {
                delete plotWidgets.takeLast();
            }
        }
    }
}


// 获取当前曲线的数量
unsigned PlotManager::numOfCurves()
{
    return curves.size(); // 返回曲线列表的大小
}

// 获取指定索引的 Plot 小部件
Plot* PlotManager::plotWidget(unsigned curveIndex)
{
    if (isMulti)
    {
        return plotWidgets[curveIndex]; // 如果是多图表模式，返回对应索引的小部件
    }
    else
    {
        return plotWidgets[0]; // 否则返回第一个小部件
    }
}

// 重绘所有图形小部件
void PlotManager::replot()
{
    for (auto plot : plotWidgets)
    {
        plot->replot(); // 重绘每个小部件
    }
    if (isMulti) syncScales(); // 如果是多图表模式，调用同步坐标轴
}

// 显示或隐藏网格线
void PlotManager::showGrid(bool show)
{
    for (auto plot : plotWidgets)
    {
        plot->showGrid(show); // 对每个图形小部件显示或隐藏网格
    }
}

// 显示或隐藏次网格线
void PlotManager::showMinorGrid(bool show)
{
    for (auto plot : plotWidgets)
    {
        plot->showMinorGrid(show); // 对每个图形小部件显示或隐藏次网格
    }
}

// 显示或隐藏图例
void PlotManager::showLegend(bool show)
{
    for (auto plot : plotWidgets)
    {
        plot->showLegend(show); // 对每个图形小部件显示或隐藏图例
    }
}

// 设置图例的位置
void PlotManager::setLegendPosition(Qt::AlignmentFlag alignment)
{
    for (auto plot : plotWidgets)
    {
        plot->setLegendPosition(alignment); // 设置每个图形小部件的图例位置
    }
}

// 显示或隐藏演示模式指示器
void PlotManager::showDemoIndicator(bool show)
{
    isDemoShown = show; // 设置是否显示演示模式指示器
    for (auto plot : plotWidgets)
    {
        plot->showDemoIndicator(show); // 对每个图形小部件显示或隐藏演示模式指示器
    }
}

// 取消缩放，恢复到默认显示区域
void PlotManager::unzoom()
{
    for (auto plot : plotWidgets)
    {
        plot->unzoom(); // 对每个图形小部件进行取消缩放操作
    }
}

// 设置背景为深色或浅色
void PlotManager::darkBackground(bool enabled)
{
    for (auto plot : plotWidgets)
    {
        plot->darkBackground(enabled); // 对每个图形小部件设置背景颜色
    }
}

// 设置是否显示符号
void PlotManager::setSymbols(Plot::ShowSymbols shown)
{
    showSymbols = shown; // 更新显示符号的设置
    for (auto plot : plotWidgets)
    {
        plot->setSymbols(shown); // 对每个图形小部件设置符号显示
    }
}

// 设置 Y 轴的自动缩放和范围
void PlotManager::setYAxis(bool autoScaled, double yAxisMin, double yAxisMax)
{
    _autoScaled = autoScaled; // 设置 Y 轴是否自动缩放
    _yMin = yAxisMin; // 设置 Y 轴最小值
    _yMax = yAxisMax; // 设置 Y 轴最大值
    for (auto plot : plotWidgets)
    {
        plot->setYAxis(autoScaled, yAxisMin, yAxisMax); // 设置每个图形小部件的 Y 轴
    }
}

// 设置 X 轴的显示方式（作为索引还是指定范围）
void PlotManager::setXAxis(bool asIndex, double xMin, double xMax)
{
    _xAxisAsIndex = asIndex; // 设置是否将 X 轴作为索引
    _xMin = xMin; // 设置 X 轴最小值
    _xMax = xMax; // 设置 X 轴最大值

    int ci = 0;
    for (auto curve : curves)
    {
        FrameBufferSeries* series = static_cast<FrameBufferSeries*>(curve->data()); // 获取曲线的数据系列
        series->setX(_stream->channel(ci)->xData()); // 更新曲线的 X 数据
        ci++;
    }
    for (auto plot : plotWidgets)
    {
        if (asIndex)
        {
            plot->setXAxis(0, _numOfSamples); // 如果是索引方式，设置 X 轴范围为 0 到样本数量
        }
        else
        {
            plot->setXAxis(xMin, xMax); // 否则设置 X 轴为指定的最小值和最大值
        }
    }
    replot(); // 重绘所有图形小部件
}


void PlotManager::flashSnapshotOverlay()
{
    for (auto plot : plotWidgets)
    {
        plot->flashSnapshotOverlay(_menu->darkBackgroundAction.isChecked());
    }
}

// 设置图表的样本数量，并更新每个小部件的样本数量
void PlotManager::setNumOfSamples(unsigned value)
{
    _numOfSamples = value; // 设置样本数量
    for (auto plot : plotWidgets)
    {
        plot->setNumOfSamples(value); // 更新每个图形小部件的样本数量
        if (_xAxisAsIndex) plot->setXAxis(0, value); // 如果 X 轴按索引显示，更新 X 轴范围
    }
}

// 设置图表的宽度，并更新每个小部件的宽度
void PlotManager::setPlotWidth(double width)
{
    _plotWidth = width; // 设置图表宽度
    for (auto plot : plotWidgets)
    {
        plot->setPlotWidth(width); // 更新每个图形小部件的宽度
    }
}

// 设置曲线的线条粗细，并更新所有曲线的画笔
void PlotManager::setLineThickness(int thickness)
{
    lineThickness = thickness; // 设置线条粗细

    for (auto curve : curves)
    {
        auto pen = curve->pen(); // 获取当前曲线的画笔
        pen.setWidth(lineThickness); // 设置画笔宽度为指定的粗细
        curve->setPen(pen); // 更新曲线的画笔
    }

    replot(); // 重新绘制图形
}


void PlotManager::exportSvg(QString fileName) const
{
    QString baseName, suffix;

    baseName = fileName.section('.', 0, 0);
    suffix = "." + fileName.section('.', 1, 1);

    // if suffix is empty make sure it is svg
    if (suffix.size() == 1)
    {
        suffix = ".svg";
    }

    for (int i=0; i < plotWidgets.size(); i++)
    {
        if (plotWidgets.size() > 1)
            fileName = baseName + "-" + _stream->channel(i)->name() + suffix;

        auto plot = plotWidgets.at(i);

        QSvgGenerator gen;
        gen.setFileName(fileName);
        gen.setSize(plot->size());
        gen.setViewBox(plot->rect());

        QwtPlotRenderer renderer;
        QPainter painter;
        painter.begin(&gen);
        renderer.render(plot, &painter, plot->rect());
        painter.end();
    }
}
