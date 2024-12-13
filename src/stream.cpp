/*
  Copyright © 2018 Hasan Yavuz Özderya

  This file is part of serialplot.

  serialplot is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  serialplot is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with serialplot.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stream.h"
#include "ringbuffer.h"
#include "indexbuffer.h"
#include "linindexbuffer.h"

// Stream类的构造函数：初始化流数据（数据通道和样本数量）
Stream::Stream(unsigned nc, bool x, unsigned ns) :
    _infoModel(nc)  // 初始化通道信息模型
{
    _numSamples = ns;  // 设置样本数
    _paused = false;   // 默认未暂停数据流

    xAsIndex = true;   // 默认将X轴作为索引
    xMin = 0;          // X轴最小值
    xMax = 1;          // X轴最大值

    // 根据是否有X轴数据创建X轴数据缓冲区
    _hasx = x;
    if (x)
    {
        // TODO: 实现X轴的环形缓冲区（待实现）
        Q_ASSERT(false);
    }
    else
    {
        // 创建X轴缓冲区
        xData = makeXBuffer();
    }

    // 创建数据通道
    for (unsigned i = 0; i < nc; i++)
    {
        auto c = new StreamChannel(i, xData, new RingBuffer(ns), &_infoModel);
        channels.append(c);  // 将通道添加到列表中
    }
}

// Stream类的析构函数：释放所有通道和缓冲区
Stream::~Stream()
{
    for (auto ch : channels)
    {
        delete ch;  // 删除每个数据通道
    }
    delete xData;  // 删除X轴数据缓冲区
}

// 判断是否存在X轴数据
bool Stream::hasX() const
{
    return _hasx;
}

// 获取通道的数量
unsigned Stream::numChannels() const
{
    return channels.length();
}

// 获取样本数量
unsigned Stream::numSamples() const
{
    return _numSamples;
}

// 获取指定索引的通道（只读）
const StreamChannel* Stream::channel(unsigned index) const
{
    Q_ASSERT(index < numChannels());
    return channels[index];
}

// 获取指定索引的通道（可写）
StreamChannel* Stream::channel(unsigned index)
{
    return const_cast<StreamChannel*>(static_cast<const Stream&>(*this).channel(index));
}

// 获取所有的通道
QVector<const StreamChannel*> Stream::allChannels() const
{
    QVector<const StreamChannel*> result(numChannels());
    for (unsigned ci = 0; ci < numChannels(); ci++)
    {
        result[ci] = channel(ci);
    }
    return result;
}

// 获取通道信息模型
const ChannelInfoModel* Stream::infoModel() const
{
    return &_infoModel;
}

// 获取通道信息模型（可写）
ChannelInfoModel* Stream::infoModel()
{
    return const_cast<ChannelInfoModel*>(static_cast<const Stream&>(*this).infoModel());
}

// 设置通道数目，并根据新的通道数进行适当调整
void Stream::setNumChannels(unsigned nc, bool x)
{
    unsigned oldNum = numChannels();  // 获取当前通道数
    if (oldNum == nc && x == _hasx) return;  // 如果通道数和X轴设置未变化，则不做任何操作

    // 调整通道数目
    if (nc > oldNum)
    {
        for (unsigned i = oldNum; i < nc; i++)
        {
            auto c = new StreamChannel(i, xData, new RingBuffer(_numSamples), &_infoModel);
            channels.append(c);  // 增加新的通道
        }
    }
    else if (nc < oldNum)
    {
        for (unsigned i = oldNum - 1; i > nc - 1; i--)
        {
            delete channels.takeLast();  // 删除多余的通道
        }
    }

    // 根据是否有X轴数据，调整X轴数据
    if (x != _hasx)
    {
        if (x)
        {
            // TODO: 实现X轴的环形缓冲区（待实现）
            Q_ASSERT(false);
        }
        else
        {
            xData = makeXBuffer();  // 重新创建X轴数据缓冲区
        }

        for (auto c : channels)
        {
            c->setX(xData);  // 为每个通道设置X轴数据
        }
        _hasx = x;
    }

    if (nc != oldNum)
    {
        _infoModel.setNumOfChannels(nc);  // 更新信息模型中的通道数
        emit numChannelsChanged(nc);  // 发出通道数变化的信号
    }

    Sink::setNumChannels(nc, x);  // 调用基类的方法设置通道数
}

// 创建X轴数据缓冲区
XFrameBuffer* Stream::makeXBuffer() const
{
    if (xAsIndex)
    {
        return new IndexBuffer(_numSamples);  // 创建索引缓冲区
    }
    else
    {
        return new LinIndexBuffer(_numSamples, xMin, xMax);  // 创建线性索引缓冲区
    }
}

// 应用增益和偏移量，调整样本数据
const SamplePack* Stream::applyGainOffset(const SamplePack& pack) const
{
    Q_ASSERT(infoModel()->gainOrOffsetEn());  // 确保增益或偏移已启用

    SamplePack* mPack = new SamplePack(pack);  // 创建样本副本
    unsigned ns = pack.numSamples();  // 获取样本数

    for (unsigned ci = 0; ci < numChannels(); ci++)
    {
        bool gainEn = infoModel()->gainEn(ci);  // 获取当前通道增益启用状态
        bool offsetEn = infoModel()->offsetEn(ci);  // 获取当前通道偏移启用状态
        if (gainEn || offsetEn)
        {
            double* mdata = mPack->data(ci);  // 获取副本中的数据

            double gain = infoModel()->gain(ci);  // 获取增益
            double offset = infoModel()->offset(ci);  // 获取偏移

            // 如果增益启用，应用增益
            if (gainEn)
            {
                for (unsigned i = 0; i < ns; i++)
                {
                    mdata[i] *= gain;
                }
            }
            // 如果偏移启用，应用偏移
            if (offsetEn)
            {
                for (unsigned i = 0; i < ns; i++)
                {
                    mdata[i] += offset;
                }
            }
        }
    }

    return mPack;
}

// 向流中输入样本数据
void Stream::feedIn(const SamplePack& pack)
{
    Q_ASSERT(pack.numChannels() == numChannels() && pack.hasX() == hasX());  // 确保输入数据的通道数和X轴数据与当前流一致

    if (_paused) return;  // 如果流已暂停，则不处理数据

    unsigned ns = pack.numSamples();  // 获取样本数
    if (_hasx)
    {
        // TODO: 实现X轴的环形缓冲区（待实现）
        Q_ASSERT(false);
    }

    // 如果需要应用增益和偏移，修改样本数据
    const SamplePack* mPack = nullptr;
    if (infoModel()->gainOrOffsetEn())
        mPack = applyGainOffset(pack);

    for (unsigned ci = 0; ci < numChannels(); ci++)
    {
        auto buf = static_cast<RingBuffer*>(channels[ci]->yData());  // 获取数据通道的缓冲区
        double* data = (mPack == nullptr) ? pack.data(ci) : mPack->data(ci);  // 获取处理后的数据
        buf->addSamples(data, ns);  // 将数据添加到缓冲区
    }

    Sink::feedIn((mPack == nullptr) ? pack : *mPack);  // 将数据传递给基类处理

    if (mPack != nullptr) delete mPack;  // 释放副本
    emit dataAdded();  // 发出数据添加的信号
}

// 暂停或恢复数据流
void Stream::pause(bool paused)
{
    _paused = paused;
}

// 清空所有通道的数据
void Stream::clear()
{
    for (auto c : channels)
    {
        static_cast<RingBuffer*>(c->yData())->clear();  // 清空每个通道的数据
    }
}

// 设置样本数目
void Stream::setNumSamples(unsigned value)
{
    if (value == _numSamples) return;
    _numSamples = value;

    xData->resize(value);  // 调整X轴数据的大小
    for (auto c : channels)
    {
        static_cast<RingBuffer*>(c->yData())->resize(value);  // 调整每个通道缓冲区的大小
    }
}

// 设置X轴的表示方式（索引或线性）
void Stream::setXAxis(bool asIndex, double min, double max)
{
    xAsIndex = asIndex;
    xMin = min;
    xMax = max;

    // 如果没有X轴数据，创建相应的X轴缓冲区
    if (!hasX())
    {
        xData = makeXBuffer();
        for (auto c : channels)
        {
            c->setX(xData);  // 设置每个通道的X轴数据
        }
    }
}

// 保存流设置到QSettings
void Stream::saveSettings(QSettings* settings) const
{
    _infoModel.saveSettings(settings);
}

// 从QSettings加载流设置
void Stream::loadSettings(QSettings* settings)
{
    _infoModel.loadSettings(settings);
}
