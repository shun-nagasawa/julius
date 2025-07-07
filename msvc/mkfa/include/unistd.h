

#ifndef _UNISTD_H_
#define _UNISTD_H_

// Windows の _read/_write/_close などを POSIX 名にマップ
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>

#define read   _read
#define write  _write
#define close  _close
#define lseek  _lseek

// getopt が必要なら以下を実装するか、
// 他のオプション解析ライブラリを使ってください。
// int getopt(int argc, char * const argv[], const char *optstring);

#endif // _UNISTD_H_


