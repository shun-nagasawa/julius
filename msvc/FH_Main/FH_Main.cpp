
// FH_Main.cpp の先頭に挿入
#define WIN32_LEAN_AND_MEAN   // windows.h の余計な部分を省く
#include <winsock2.h>         // winsock2 を先に
#include <windows.h>          // これで winsock.h は読み込まれない

// 必要なら TCP/IP 拡張も
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
#include <map>
#include <array>

#include "julius/juliuslib.h"
//#include <julius/grammar.h> 
//#include <sent/realtime-1stpass.h>   // これが抜けていると MFCCCalc は未定義
#include <sent/htk_param.h>          // HTK_Param の定義
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

static std::ofstream outf;
static std::vector<std::vector<float>> feats;
static std::vector<std::string> phones;
static std::string g_wav_path;


/// <summary>
/// EUC-JP→UTF-8 変換ユーティリティ
/// </summary>
/// <param name="eucjp"></param>
/// <returns></returns>
std::string eucjp_to_utf8(const char* eucjp)
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


/// <summary>
/// 「第１パス」（特徴量抽出パス）が終わったタイミングで呼ばれるコールバック
/// </summary>
/// <param name="recog"></param>
/// <param name=""></param>
static void pass1_end_callback(Recog* recog, void* /*dummy*/) {
  feats.clear();
  // recog->mfcclist に第一パスで計算された全フレーム MFCC が入っている
  for (MFCCCalc* mf = recog->mfcclist; mf; mf = mf->next) {
    HTK_Param* param = mf->param;
    // フレーム数は header.samplenum、次元は veclen
    unsigned int nframes = param->header.samplenum;
    int dim = param->veclen;

    // 各フレームごとにベクトルをコピー
    for (unsigned int t = 0; t < nframes; ++t) {
      // 長さ dim の配列
      float* vec = param->parvec[t];
      std::vector<float> frame(vec, vec + dim);
      feats.push_back(std::move(frame));
    }
  }
}


/// <summary>
/// Julius の認識結果を使って「フレームごとの音素ラベル＋母音強度＋エネルギー」を計算し、あらかじめ開いてある出力ストリーム (outf) に書き出す
/// </summary>
/// <param name="recog"></param>
/// <param name=""></param>
static void result_callback(Recog* recog, void* /*dummy*/) {
  phones.assign(feats.size(), "");

  // 1) Phone アライメントを phones[f] にセット
  for (RecogProcess* r = recog->process_list; r; r = r->next) {
    if (!r->live || r->result.sentnum <= 0) {
      continue;
    }

    Sentence* s = &r->result.sent[0];
    if (!s->align) {
      continue;
    }

    for (SentenceAlign* al = s->align; al; al = al->next) {
      if (al->unittype != PER_PHONEME) {
	continue;
      }

      for (int i = 0; i < al->num; ++i) {
	const char* phoneme = al->ph[i]->name;
	int sf = al->begin_frame[i];
	int ef = al->end_frame[i];

	if (sf < 0 || ef <= sf || ef >(int)phones.size()) {
	  continue;
	}
	for (int f = sf; f < ef; ++f) {
	  phones[f] = phoneme;
	}
      }
    }
  }

  // 2) 全フレームのエネルギーを計算し，最大値を探す
  int N = feats.size();
  std::vector<float> energy(N);
  float Emax = 1e-12f;
  for (int f = 0; f < N; ++f) {
    double sum = 1e-12;
    for (float c0 : feats[f]) {
      sum += c0 * c0;
    }
    energy[f] = float(sum);
    if (energy[f] > Emax) {
      Emax = energy[f];
    }
  }

  // 3) ヘッダー出力（WAV 名 + 60fps 固定）
  outf << "// input: " << g_wav_path << "\n";
  outf << "// framerate: 60 [fps]\n";
  outf << "// frame count, msec, width(0-1 def=0.583), height(0-1 def=0.000), tongue(0-1 def=0.000), A(0-1), I(0-1), U(0-1), E(0-1), O(0-1), Vol(dB)\n";

  const double width = 0.000000;
  const double height = 0.000000;

  // 4) 本体出力 & 母音区間検出用バッファ準備
  struct VowelRegion { int start_frame, end_frame, start_msec, end_msec; char vowel; };
  std::vector<VowelRegion> regions;
  bool in_region = false;
  VowelRegion cur_region = {};

  for (size_t f = 0; f < feats.size(); ++f) {
    // msec 計算は 60fps 固定
    int msec = int(f * (1000.0 / 60.0) + 0.5);

    const std::string& ph = phones[f];

#if false
    if (!ph.empty()) {
      std::cerr << "[DEBUG] frame=" << f << " phone=\"" << ph << "\"\n";
    }
#endif

    // --- ① コンテキスト「-」「+」の間から現在音素を抽出 ---
    // 長母音（a: や u:）
    std::string cur;
    auto p1 = ph.find('-');
    auto p2 = ph.find('+');
    if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
      cur = ph.substr(p1 + 1, p2 - (p1 + 1));
    }
    else {
      // コンテキスト未定義なら丸ごと
      cur = ph;  
    }

    // ① phones[f] から母音文字を抽出する（大文字化・小文字対応・長母音対応）
    char v = 0;
    if (!cur.empty()) {
      const char c = std::toupper(cur[0]);
      if (c == 'A' || c == 'I' || c == 'U' || c == 'E' || c == 'O') {
	v = c;
      }
    }

    // 正規化した母音強度 (0–1)
    float strength = (v ? energy[f] / Emax : 0.0f);

    // ② 各母音フラグを立てる
    float A = (v == 'A') ? strength : 0.0f;
    float I = (v == 'I') ? strength : 0.0f;
    float U = (v == 'U') ? strength : 0.0f;
    float E = (v == 'E') ? strength : 0.0f;
    float O = (v == 'O') ? strength : 0.0f;

    // ③ 母音区間の開始／終了を検出
    if (!in_region && v) {
      in_region = true;
      cur_region.start_frame = f;
      cur_region.start_msec = msec;
      cur_region.vowel = v;
    }
    else if (in_region && !v) {
      in_region = false;
      cur_region.end_frame = f;
      cur_region.end_msec = msec;
      regions.push_back(cur_region);
    }

    float db = 10.0f * std::log10(energy[f]);

    // ⑤ ファイル出力
    outf << f << ", "
      << msec << ", "
      << std::fixed << std::setprecision(6) << width << ", "
      << height << ", " << 0.0f << ", "   // tongue
      << A << ", " << I << ", " << U << ", " << E << ", " << O << ", " << db << "\n";
  }

  // 最後まで母音継続中なら Region を閉じる
  if (in_region) {
    cur_region.end_frame = feats.size();
    cur_region.end_msec = int(feats.size() * (1000.0 / 60.0) + 0.5);
    regions.push_back(cur_region);
  }

}



/// <summary>
/// 生の音声 → Julius が認識した単語列 → 見やすい形でログに出力
/// </summary>
static void parse_string_callback(Recog* recog, void* /*user_data*/) {

  for (RecogProcess* rp = recog->process_list; rp; rp = rp->next) {

    // ライブ状態かつ認識結果があるものだけ
    if (!rp->live || rp->result.sentnum <= 0) {
      continue;
    }

    Sentence* s = &rp->result.sent[0];
    if (s->word_num <= 0) {
      continue;
    }

    WORD_INFO* wi = rp->lm->winfo;
    if (!wi) {
      continue;
    }

    // 単語列を空白区切りで組み立て
    std::ostringstream oss;
    for (int i = 0; i < s->word_num; ++i) {
      WORD_ID wid = s->word[i];
      // 出力用文字列 (woutput) があれば優先、なければ基本辞書名 (wname)
      const char* wstr = (wi->woutput && wi->woutput[wid]) ? wi->woutput[wid] : wi->wname[wid];
      if (i) oss << ' ';
      oss << (wstr ? wstr : "<unk>");
    }

    std::string hyp = oss.str();
    std::cerr << "[Lips_ja] " << hyp << std::endl;
  }
}



int main(int argc, char** argv) {

  // ① コンソールを UTF-8 モードに
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  // adxlip フォルダ作成
  if (!CreateDirectoryA("adxlip", NULL)) {
    DWORD err = GetLastError();
    if (err != ERROR_ALREADY_EXISTS) {
      std::cerr << "ERROR: Failed to create adxlip directory (code " << err << ")" << std::endl;
    }
  }

  const char* config_file = "Main.jconf";

  // Julius 初期化
  Jconf* jconf = j_config_load_file_new(const_cast<char*>(config_file));
  if (!jconf) {
    return 1;
  }


  // 2) Main.jconf 自身を開き、-filelist 行を取得
  std::string filelist_path;
  {
    std::ifstream cfg(config_file);
    std::string ln;
    while (std::getline(cfg, ln)) {
      size_t p = ln.find_first_not_of(" \t");
      if (p == std::string::npos) {
	continue;
      }
      if (ln.compare(p, 9, "-filelist") == 0) {
	std::istringstream iss(ln.substr(p));
	std::string opt;
	iss >> opt >> filelist_path;
	break;
      }
    }
    if (filelist_path.empty()) {
      std::cerr << "ERROR: -filelist not found in " << config_file << std::endl;
      j_jconf_free(jconf);
      return 1;
    }
  }

  // 3) filelist.txt から全 WAV パスを収集
  std::vector<std::string> wav_list;
  {
    std::ifstream ifs(filelist_path);
    if (!ifs) {
      std::cerr << "ERROR: cannot open filelist: " << filelist_path << std::endl;
      j_jconf_free(jconf);
      return 1;
    }
    std::string line;
    while (std::getline(ifs, line)) {
      size_t p = line.find_first_not_of(" \t");
      if (p == std::string::npos) {
	continue;
      }
      if (line[p] == '#' || line[p] == ';') {
	continue;
      }
      wav_list.push_back(line.substr(p));
    }
    if (wav_list.empty()) {
      std::cerr << "ERROR: no valid WAV in " << filelist_path << std::endl;
      j_jconf_free(jconf);
      return 1;
    }
  }

  // 4) 各 WAV ファイルごとに処理
  for (const auto& wav : wav_list) {
    g_wav_path = wav;
    std::cerr << "[DEBUG] using WAV: " << g_wav_path << std::endl;


    // 出力ファイル名を WAV ベースで生成 (filesystem を使わず手動でパス処理)
    std::string base = wav;

    // パス区切り文字でファイル名部分を抽出
    auto pos = base.find_last_of("/\\");
    if (pos != std::string::npos) {
      base = base.substr(pos + 1);
    }

    // 拡張子を除去
    auto dot = base.find_last_of('.');
    if (dot != std::string::npos) {
      base = base.substr(0, dot);
    }

    std::string out_name = base + ".adxlip";
    std::string out_path = std::string("adxlip\\") + out_name;
    outf.open(out_path, std::ios::out | std::ios::trunc);
    if (!outf) {
      std::cerr << "Cannot open output file: " << out_path << std::endl;
      continue;
    }

    // Julius インスタンス作成
    Recog* recog = j_create_instance_from_jconf(jconf);
    if (!recog) {
      std::cerr << "Failed to create Julius instance" << std::endl;
      outf.close();
      continue;
    }
    // コールバック登録
    callback_add(recog, CALLBACK_EVENT_PASS1_END, pass1_end_callback, nullptr);
    callback_add(recog, CALLBACK_RESULT, parse_string_callback, nullptr);
    callback_add(recog, CALLBACK_RESULT, result_callback, nullptr);

    // 認識開始
    if (j_adin_init(recog) == FALSE || j_open_stream(recog, const_cast<char*>(g_wav_path.c_str())) != 0) {
      std::cerr << "Audio init/open failed for " << g_wav_path << std::endl;
      j_recog_free(recog);
      outf.close();
      continue;
    }
    j_recognize_stream(recog);

    // 後始末
    outf.close();
  }

  // 5) 共通リソース解放
  j_jconf_free(jconf);
  return 0;
}


