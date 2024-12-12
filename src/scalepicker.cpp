#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <qwt_scale_widget.h>
#include <qwt_scale_map.h>
#include <qwt_scale_div.h>
#include <qwt_text.h>
#include <math.h>

#include "scalepicker.h"

// 最小的选择区域尺寸（像素）
#define MIN_PICK_SIZE (2)

// 吸附的距离（像素）
#define SNAP_DISTANCE (5)

// PlotOverlay 类：绘制图表区域的覆盖层
class PlotOverlay : public QwtWidgetOverlay
{
public:
    PlotOverlay(QWidget* widget, ScalePicker* picker);  // 构造函数

protected:
    virtual void drawOverlay(QPainter*) const;  // 绘制覆盖层

private:
    ScalePicker* _picker;  // 指向 ScalePicker 的指针
};

// ScaleOverlay 类：绘制标尺区域的覆盖层
class ScaleOverlay : public QwtWidgetOverlay
{
public:
    ScaleOverlay(QWidget* widget, ScalePicker* picker);  // 构造函数

protected:
    virtual void drawOverlay(QPainter*) const;  // 绘制覆盖层

private:
    ScalePicker* _picker;  // 指向 ScalePicker 的指针
};

// PlotOverlay 构造函数：初始化覆盖层
PlotOverlay::PlotOverlay(QWidget* widget, ScalePicker* picker) :
    QwtWidgetOverlay(widget)
{
    _picker = picker;  // 设置 ScalePicker 指针
}

// 绘制图表区域的覆盖层
void PlotOverlay::drawOverlay(QPainter* painter) const
{
    _picker->drawPlotOverlay(painter);  // 调用 ScalePicker 绘制图表覆盖层
}

// ScaleOverlay 构造函数：初始化覆盖层
ScaleOverlay::ScaleOverlay(QWidget* widget, ScalePicker* picker) :
    QwtWidgetOverlay(widget)
{
    _picker = picker;  // 设置 ScalePicker 指针
}

// 绘制标尺区域的覆盖层
void ScaleOverlay::drawOverlay(QPainter* painter) const
{
    _picker->drawScaleOverlay(painter);  // 调用 ScalePicker 绘制标尺覆盖层
}

// ScalePicker 类：用于选择标尺范围的工具类
ScalePicker::ScalePicker(QwtScaleWidget* scaleWidget, QWidget* canvas) :
    QObject(scaleWidget)  // 安装事件过滤器
{
    _scaleWidget = scaleWidget;  // 设置标尺控件
    _canvas = canvas;  // 设置画布控件
    scaleWidget->installEventFilter(this);  // 安装事件过滤器
    scaleWidget->setMouseTracking(true);  // 开启鼠标追踪
    pickerOverlay = new PlotOverlay(canvas, this);  // 初始化图表区域覆盖层
    scaleOverlay = new ScaleOverlay(scaleWidget, this);  // 初始化标尺区域覆盖层
    started = false;  // 标识是否开始选择
    pressed = false;  // 标识是否按下鼠标
}

// 事件过滤器：处理鼠标事件
bool ScalePicker::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::MouseMove)
    {
        updateSnapPoints();  // 更新吸附点

        QMouseEvent* mouseEvent = (QMouseEvent*) event;
        int posPx = this->positionPx(mouseEvent);  // 获取鼠标位置（像素）

        // 如果没有按下 Shift 键，则进行吸附
        if (! (mouseEvent->modifiers() & Qt::ShiftModifier))
        {
            // 尝试将鼠标位置吸附到最近的吸附点
            for (auto sp : snapPoints)
            {
                if (std::abs(posPx-sp) <= SNAP_DISTANCE)
                {
                    posPx = sp;  // 吸附到该点
                    break;
                }
            }
        }

        double pos = this->position(posPx);  // 转换为标尺位置
        currentPosPx = posPx;  // 更新当前位置

        if (event->type() == QEvent::MouseButtonPress &&
            mouseEvent->button() == Qt::LeftButton)
        {
            pressed = true;  // 鼠标按下，标记为开始
            firstPos = pos;  // 记录开始位置
            firstPosPx = posPx;  // 记录开始位置（像素）
        }
        else if (event->type() == QEvent::MouseMove)
        {
            // 确保选择区域足够大，避免误触发选择
            if (!started && pressed && (fabs(posPx-firstPosPx) > MIN_PICK_SIZE))
            {
                started = true;  // 标记选择开始
            }
            pickerOverlay->updateOverlay();  // 更新覆盖层
            scaleOverlay->updateOverlay();  // 更新标尺覆盖层
        }
        else // event->type() == QEvent::MouseButtonRelease
        {
            pressed = false;  // 鼠标释放
            if (started)
            {
                started = false;  // 选择结束
                if (firstPos != pos)  // 如果起始位置和结束位置不同
                {
                    emit picked(firstPos, pos);  // 发出选择信号
                }
            }
            pickerOverlay->updateOverlay();  // 更新覆盖层
            scaleOverlay->updateOverlay();  // 更新标尺覆盖层
        }
        return true;
    }
    else if (event->type() == QEvent::Leave)
    {
        // 鼠标离开时更新覆盖层
        scaleOverlay->updateOverlay();
        pickerOverlay->updateOverlay();
        return true;
    }
    else
    {
        return QObject::eventFilter(object, event);  // 其他事件交给父类处理
    }
}

const int TEXT_MARGIN = 4;
// 绘制图表区域的覆盖层
void ScalePicker::drawPlotOverlay(QPainter* painter)
{
    const double FILL_ALPHA = 0.2;  // 半透明度

    painter->save();
    painter->setPen(_pen);  // 设置画笔

    if (started)
    {
        QColor color = _pen.color();
        color.setAlphaF(FILL_ALPHA);  // 设置透明度
        painter->setBrush(color);  // 设置画刷

        QRect rect;
        QwtText text = trackerText();  // 获取追踪文本
        auto tSize = text.textSize(painter->font());  // 获取文本尺寸

        // 如果是水平标尺
        if (_scaleWidget->alignment() == QwtScaleDraw::BottomScale ||
            _scaleWidget->alignment() == QwtScaleDraw::TopScale)
        {
            int canvasHeight = painter->device()->height();
            int pickWidth = currentPosPx-firstPosPx;
            rect = QRect(posCanvasPx(firstPosPx), 0, pickWidth, canvasHeight);
        }
        else // 垂直标尺
        {
            int canvasWidth = painter->device()->width();
            int pickHeight = currentPosPx-firstPosPx;
            rect = QRect(0, posCanvasPx(firstPosPx), canvasWidth, pickHeight);
        }
        painter->drawRect(rect);  // 绘制选择区域
        text.draw(painter, pickTrackerTextRect(painter, rect, tSize));  // 绘制追踪文本
    }
    else if (_scaleWidget->underMouse())
    {
        // 如果鼠标悬停在标尺上，绘制追踪文本
        QwtText text = trackerText();
        auto tsize = text.textSize(painter->font());
        text.draw(painter, trackerTextRect(painter, currentPosPx, tsize));
    }
    painter->restore();
}

// 获取追踪文本，显示选择区域的起始位置和结束位置
QwtText ScalePicker::trackerText() const
{
    double pos;
    // 如果已吸附到点，恢复精度
    if (snapPointMap.contains(currentPosPx))
    {
        pos = snapPointMap[currentPosPx];
    }
    else
    {
        pos = position(currentPosPx);  // 获取当前位置
    }

    return QwtText(QString("%1").arg(pos));  // 返回位置文本
}

// 获取追踪文本的矩形区域（用于绘制位置文本）
QRectF ScalePicker::trackerTextRect(QPainter* painter, int posPx, QSizeF textSize) const
{
    int canvasPosPx = posCanvasPx(posPx);  // 获取画布位置
    QPointF topLeft;

    if (_scaleWidget->alignment() == QwtScaleDraw::BottomScale ||
        _scaleWidget->alignment() == QwtScaleDraw::TopScale)
    {
        int left = canvasPosPx - textSize.width() / 2;
        int canvasWidth = painter->device()->width();
        left = std::max(TEXT_MARGIN, left);
        left = std::min(double(left), canvasWidth - textSize.width() - TEXT_MARGIN);
        int top = 0;
        if (_scaleWidget->alignment() == QwtScaleDraw::BottomScale)
        {
            top = painter->device()->height() - textSize.height();
        }
        topLeft = QPointF(left, top);
    }
    else                        // 左右标尺
    {
        int top = canvasPosPx-textSize.height() / 2;
        int canvasHeight = painter->device()->height();
        top = std::max(0, top);
        top = std::min(double(top), canvasHeight - textSize.height());
        int left = TEXT_MARGIN;
        if (_scaleWidget->alignment() == QwtScaleDraw::RightScale)
        {
            left = painter->device()->width() - textSize.width();
        }
        topLeft = QPointF(left, top);
    }
    return QRectF(topLeft, textSize);  // 返回文本矩形区域
}

// 获取选择区域的文本矩形，用于显示追踪文本
QRectF ScalePicker::pickTrackerTextRect(QPainter* painter, QRect pickRect, QSizeF textSize) const
{
    qreal left = 0;
    int pickLength = currentPosPx - firstPosPx;  // 选择区域的长度
    QPointF topLeft;

    if (_scaleWidget->alignment() == QwtScaleDraw::BottomScale ||
        _scaleWidget->alignment() == QwtScaleDraw::TopScale)
    {
        int canvasWidth = painter->device()->width();

        if (pickLength > 0)
        {
            left = pickRect.right() + TEXT_MARGIN;
        }
        else
        {
            left = pickRect.right() - (textSize.width() + TEXT_MARGIN);
        }

        // 确保文本不超出画布
        if (left < TEXT_MARGIN)
        {
            left = std::max(0, pickRect.right()) + TEXT_MARGIN;
        }
        else if (left + textSize.width() + TEXT_MARGIN > canvasWidth)
        {
            left = std::min(pickRect.right(), canvasWidth) - (textSize.width() + TEXT_MARGIN);
        }

        if (_scaleWidget->alignment() == QwtScaleDraw::BottomScale)
        {
            int canvasHeight = painter->device()->height();
            topLeft = QPointF(left, canvasHeight - textSize.height());
        }
        else                // top scale
        {
            topLeft = QPointF(left, 0);
        }
    }
    else                        // 左右标尺
    {
        int canvasHeight = painter->device()->height();

        int top = 0;
        if (pickLength > 0)
        {
            top = pickRect.bottom();
        }
        else
        {
            top = pickRect.bottom() - textSize.height();
        }

        // 确保文本不超出画布
        if (top < 0)
        {
            top = std::max(0, pickRect.bottom());
        }
        else if (top + textSize.height() > canvasHeight)
        {
            top = std::min(canvasHeight, pickRect.bottom()) - textSize.height();
        }

        if (_scaleWidget->alignment() == QwtScaleDraw::LeftScale)
        {
            topLeft = QPointF(TEXT_MARGIN, top);
        }
        else                    // right scale
        {
            int canvasWidth = painter->device()->width();
            topLeft = QPointF(canvasWidth - textSize.width() - TEXT_MARGIN, top);
        }
    }
    return QRectF(topLeft, textSize);  // 返回文本矩形区域
}

// 绘制标尺区域的覆盖层
void ScalePicker::drawScaleOverlay(QPainter* painter)
{
    painter->save();

    // 对于垂直标尺，旋转坐标系
    if (_scaleWidget->alignment() == QwtScaleDraw::LeftScale ||
        _scaleWidget->alignment() == QwtScaleDraw::RightScale) // 垂直
    {
        int width = painter->device()->width();
        painter->rotate(90);  // 旋转90度
        painter->translate(0, -width);  // 调整位置
    }

    // 绘制标尺指示器
    if (started) drawTriangle(painter, firstPosPx);  // 绘制起始指示器
    if (started || _scaleWidget->underMouse())
    {
        drawTriangle(painter, currentPosPx);  // 绘制当前指示器
    }

    painter->restore();
}

// 绘制三角形指示器
void ScalePicker::drawTriangle(QPainter* painter, int position)
{
    const double tan60 = 1.732;  // 60度角的正切值
    const double trsize = 10;  // 三角形的大小
    const int TRIANGLE_NUM_POINTS = 3;
    const int MARGIN = 2;  // 边距
    const QPointF points[TRIANGLE_NUM_POINTS] =
        {
            {0, 0},
            {-trsize/tan60 , trsize},
            {trsize/tan60 , trsize}
        };

    painter->save();
    painter->setPen(Qt::NoPen);  // 不绘制边框
    painter->setBrush(_scaleWidget->palette().windowText());  // 设置画刷颜色
    painter->setRenderHint(QPainter::Antialiasing);  // 启用抗锯齿

    painter->translate(position, MARGIN);  // 移动到指定位置
    painter->drawPolygon(points, TRIANGLE_NUM_POINTS);  // 绘制三角形

    painter->restore();
}

// 设置画笔
void ScalePicker::setPen(QPen pen)
{
    _pen = pen;  // 设置画笔
}

// 将点击位置转换为标尺坐标
double ScalePicker::position(double posPx) const
{
    return _scaleWidget->scaleDraw()->scaleMap().invTransform(posPx);  // 使用反向转换将像素位置转为坐标
}

// 获取鼠标点击位置的像素值
int ScalePicker::positionPx(QMouseEvent* mouseEvent)
{
    double pos;
    if (_scaleWidget->alignment() == QwtScaleDraw::BottomScale ||
        _scaleWidget->alignment() == QwtScaleDraw::TopScale)
    {
        pos = mouseEvent->pos().x();  // 水平标尺
    }
    else // 左右标尺
    {
        pos = mouseEvent->pos().y();
    }
    return pos;
}

// 更新吸附点：获取所有主要、次要和中等刻度线的吸附点
void ScalePicker::updateSnapPoints()
{
    auto allTicks = _scaleWidget->scaleDraw()->scaleDiv().ticks(QwtScaleDiv::MajorTick) +
        _scaleWidget->scaleDraw()->scaleDiv().ticks(QwtScaleDiv::MediumTick) +
        _scaleWidget->scaleDraw()->scaleDiv().ticks(QwtScaleDiv::MinorTick);

    snapPoints.clear();
    snapPointMap.clear();
    for(auto t : allTicks)
    {
        // `round` 用于将 double 类型的刻度值转换为整数像素值
        int p = round(_scaleWidget->scaleDraw()->scaleMap().transform(t));
        snapPoints << p;
        snapPointMap[p] = t;  // 记录刻度值与像素的映射关系
    }
}
