#ifndef _UTIME_H
#define _UTIME_H

#include <sys/types.h> // 我知道 - 不应该这样做，但是..

struct utimbuf
{
	time_t actime;	// 文件访问时间戳，从 1970-01-01 00:00:00 开始的秒数
	time_t modtime; // 文件修改时间戳，从 1970-01-01 00:00:00 开始的秒数
};

// 设置文件访问和修改时间函数
extern int utime(const char *filename, struct utimbuf *times);

#endif
