/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 时间头文件，定义了标准时间数据结构 tm 和一些处理时间函数原型
#include <time.h>

// 这不是库函数，它仅供内核使用，因此我们不关心小于 1970 年的年份等，但假定一切均很正常
// 同样，时区 TZ 问题也先忽略，我们只是尽可能简单地处理问题
// 最好能找到一些公开的库函数（尽管我认为 minix 的时间函数是公开的）

// 另外，我恨那个设置 1970 年开始的人，难道他们就不能选择从一个闰年开始？
// 我恨格里高利历、罗马教皇、主教，我什么都不在乎。我是个脾气暴躁的人。

// 格里高利历 是指 公元纪年法📅，1582 年，时任罗马教皇的格列高利十三世👴，予以批准颁行

// 1 分钟的秒数
#define MINUTE 60

// 1 小时的秒数
#define HOUR (60 * MINUTE)

// 1 天的秒数
#define DAY (24 * HOUR)

// 1 年的秒数
#define YEAR (365 * DAY)

// 有趣的是我们考虑进了闰年
// 下面以年为界限，定义了每个月开始时的秒数时间数组

static int month[12] = {
	0,
	DAY *(31),
	DAY *(31 + 29),
	DAY *(31 + 29 + 31),
	DAY *(31 + 29 + 31 + 30),
	DAY *(31 + 29 + 31 + 30 + 31),
	DAY *(31 + 29 + 31 + 30 + 31 + 30),
	DAY *(31 + 29 + 31 + 30 + 31 + 30 + 31),
	DAY *(31 + 29 + 31 + 30 + 31 + 30 + 31 + 31),
	DAY *(31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30),
	DAY *(31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31),
	DAY *(31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30)};

// 该函数计算从 1970 年1 月1 日 0 时起到开机时刻经过的秒数，作为开机时间
long kernel_mktime(struct tm *tm)
{
	long res;
	int year;
	if (tm->tm_year >= 70)
		year = tm->tm_year - 70; // 从 70 年到现在经过的年数(2 位表示方式)，因此会有 2000 年问题。
	else
		/* Y2K bug fix by hellotigercn 20110803 */
		// 来自 https://github.com/yuan-xy/Linux-0.11
		year = tm->tm_year + 100 - 70;

	// 为了获得正确的闰年数，这里需要这样一个魔幻偏值 (y+1)

	// 这些年经过的秒数时间 + 每个闰年时多 1 天的秒数时间，在加上当年到当月时的秒数
	res = YEAR * year + DAY * ((year + 1) / 4);
	res += month[tm->tm_mon];

	// 以及 (y+2)。如果 (y+2) 不是闰年，那么我们就必须进行调整 (减去一天的秒数时间)
	if (tm->tm_mon > 1 && ((year + 2) % 4))
		res -= DAY;

	// 再加上本月过去的天数的秒数时间
	res += DAY * (tm->tm_mday - 1);

	// 再加上当天过去的小时数的秒数时间
	res += HOUR * tm->tm_hour;

	// 再加上 1 小时内过去的分钟数的秒数时间
	res += MINUTE * tm->tm_min;

	// 再加上 1 分钟内已过的秒数
	res += tm->tm_sec;

	// 即等于从 1970 年以来经过的秒数时间
	return res;
}

/*
闰年的基本计算方法是：
如果 y 能被 4 除尽且不能被 100 除尽，或者能被 400 除尽，则 y 是闰年

这里 400 年很奇怪，为什么是这样呢？

一回归年大概是 365.2422 天

所以如果一年 365 天的话，这样四年就少了 0.2422 * 4 =  0.9688 天
于是四年要加一天，但是又每四年多出了 1 - 0.9688 =  0.0312 天

但是如果持续加下去的话，400 年也就是要加 100 天
这样多加了 0.0312 * 100 = 3.12 天
于是在 400 年中就会有缺少三个闰年弥补误差
这样 400 年后还是查 0.12 天，不过下一次补误差就需要至少 3200 年，于是就忽略了。
*/
