#pragma once


#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>

#include "julius/juliuslib.h"
#include <sent/htk_param.h>  

#ifdef ENABLE_PLUGIN
#include <julius/plugin.h>
#endif

// Windows マクロ解除
#ifdef open
#undef open
#endif
#ifdef close
#undef close
#endif
#ifdef write
#undef write
#endif


class Utils
{


  /// <summary>
  /// EUC-JP→UTF-8 変換ユーティリティ
  /// </summary>
  /// <param name="eucjp"></param>
  /// <returns></returns>
  static std::string eucjp_to_utf8(const char* eucjp)
  {
    // 1) EUC-JP バイト列 → UTF-16（ワイド文字列）
    int wlen = MultiByteToWideChar(51932 /* CP_EUCJP */, 0, eucjp, -1, nullptr, 0);
    if (wlen == 0) return "";
    std::wstring wbuf(wlen, L'\0');
    MultiByteToWideChar(51932, 0, eucjp, -1, &wbuf[0], wlen);

    // 2) UTF-16 → UTF-8
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8len == 0) return "";
    std::string u8buf(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf.c_str(), -1, &u8buf[0], u8len, nullptr, nullptr);
    return u8buf;
  }

};

