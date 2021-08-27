#ifndef _TIME_H
#define _TIME_H

// 在 MINIX 中有一段对时间的描述很有趣：
// 时间的处理较为复杂，比如什么是 GMT(Greenwich Mean Time)（格林威治标准时间）、本地时间或其它时间等
// 尽管(天主教)主教 Ussher(1581-1656 年) 曾经计算过、
// 根据圣经，世界开始之日是公元前 4004 年 10 月 12 日上午 9 点
// 但在 UNIX 世界里，时间是从 GMT 1970 年 1 月 1 日午夜开始的
// 在这之前，所有均是空无的和(无效的)

#ifndef _TIME_T
#define _TIME_T
// 从 GMT 1970 年 1 月 1 日开始的以秒计数的时间（日历时间）
typedef long time_t;
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

// 系统时钟滴答频率，100HZ
#define CLOCKS_PER_SEC 100

// 从进程开始系统经过的时钟滴答数
typedef long clock_t;

struct tm
{
	int tm_sec;	  // 秒数 [0，59]
	int tm_min;	  // 分钟数 [0，59]
	int tm_hour;  // 小时数 [0，59]
	int tm_mday;  // 1 个月的天数 [0，31]
	int tm_mon;	  // 1 年中月份 [0，11]
	int tm_year;  // 从 1900 年开始的年数
	int tm_wday;  // 1 星期中的某天 [0，6] (星期天 =0)
	int tm_yday;  // 1 年中的某天 [0，365]
	int tm_isdst; // 夏令时标志
};

// 以下是有关时间操作的函数原型
// 确定处理器使用时间，返回程序所用处理器时间（滴答数）的近似值
clock_t clock(void);

// 取时间（秒数），返回从 1970-01-11 00:00:00 开始的秒数（称为日历时间）
time_t time(time_t *tp);

// 计算时间差。返回时间 time2 与 time1 之间经过的秒数
double difftime(time_t time2, time_t time1);

// 将 tm 结构表示的时间转换成日历时间
time_t mktime(struct tm *tp);

// 将 tm 结构表示的时间转换成一个字符串，返回指向该串的指针
char *asctime(const struct tm *tp);

// 将日历时间转换成一个字符串形式，如“Wed Jun 30 21:49:08:1993\n”
char *ctime(const time_t *tp);

// 将日历时间转换成 tm 结构表示的 UTC 时间（UTC - 世界时间代码 Universal Time Code）。
struct tm *gmtime(const time_t *tp);

// 将日历时间转换成 tm 结构表示的指定时间区(timezone)的时间
struct tm *localtime(const time_t *tp);

// 将 tm 结构表示的时间，利用格式字符串 fmt 转换成最大长度为 smax 的字符串，并将结果存储在 s 中
size_t strftime(char *s, size_t smax, const char *fmt, const struct tm *tp);

// 初始化时间转换信息，使用环境变量 TZ，对 zname 变量进行初始化
// 在与时间区相关的时间转换函数中将自动调用该函数
void tzset(void);

#endif
