#include <QDateTime>
#include "samplecounter.h"

// 构造函数，用于初始化时间戳和样本计数器
SampleCounter::SampleCounter()
{
    // 获取当前时间的毫秒数，作为上一次统计的时间
    prevTimeMs = QDateTime::currentMSecsSinceEpoch();
    // 初始化样本计数器为0
    count = 0;
}

#include <QtDebug>

// 这个函数用于接收样本数据并统计样本数
void SampleCounter::feedIn(const SamplePack& data)
{
    // 累加样本数量
    count += data.numSamples();

    // 获取当前时间的毫秒数
    qint64 current = QDateTime::currentMSecsSinceEpoch();
    // 计算当前时间和上次更新时间的差值（单位：毫秒）
    auto diff = current - prevTimeMs;

    // 如果时间差大于1000毫秒，即超过1秒
    if (diff > 1000) // 1秒
    {
        // 计算并发射每秒样本数（SPS）。这里的SPS是过去1秒内的数据处理速度。
        emit spsChanged(1000 * float(count) / diff);

        // 更新上次统计时间为当前时间
        prevTimeMs = current;
        // 重置样本计数器，准备下一次计数
        count = 0;
    }
}
