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


static void pass1_end_cb(Recog* recog, void* /*dummy*/) {
  feats.clear();
  // recog->mfcclist に第一パスで計算された全フレーム MFCC が入っている
  for (MFCCCalc* mf = recog->mfcclist; mf; mf = mf->next) {
    HTK_Param* param = mf->param;
    // フレーム数は header.samplenum、次元は veclen
    unsigned int nframes = param->header.samplenum;
    int dim = param->veclen;

    // 各フレームごとにベクトルをコピー
    for (unsigned int t = 0; t < nframes; ++t) {
      float* vec = param->parvec[t];  // 長さ dim の配列
      std::vector<float> frame(vec, vec + dim);
      feats.push_back(std::move(frame));
    }
  }
}


// Replace result_cb with this version:

static void result_cb(Recog* recog, void* /*dummy*/) {
  phones.assign(feats.size(), "");

  // 1) Phone アライメントを phones[f] にセット
  for (RecogProcess* r = recog->process_list; r; r = r->next) {
    if (!r->live || r->result.sentnum <= 0) continue;
    Sentence* s = &r->result.sent[0];
    if (!s->align) continue;
    for (SentenceAlign* al = s->align; al; al = al->next) {
      if (al->unittype != PER_PHONEME) continue;
      for (int i = 0; i < al->num; ++i) {
	const char* phoneme = al->ph[i]->name;
	int sf = al->begin_frame[i];
	int ef = al->end_frame[i];
	if (sf < 0 || ef <= sf || ef >(int)phones.size()) continue;
	for (int f = sf; f < ef; ++f) phones[f] = phoneme;
      }
    }
  }

  // 2) ヘッダー出力（WAV 名 + 60fps 固定）
  outf << "// input: " << g_wav_path << "\n";
  outf << "// framerate: 60 [fps]\n";
  outf << "// frame count, msec, width(0-1 def=0.583), height(0-1 def=0.000), "
    << "tongue(0-1 def=0.000), A(0-1), I(0-1), U(0-1), E(0-1), O(0-1), Vol(dB)\n";

  const double width = 0.000000;
  const double height = 0.000000;

  // 3) 本体出力 & 母音区間検出用バッファ準備
  struct VowelRegion { int start_frame, end_frame, start_msec, end_msec; char vowel; };
  std::vector<VowelRegion> regions;
  bool in_region = false;
  VowelRegion cur = {};

  for (size_t f = 0; f < feats.size(); ++f) {
    // msec 計算は 60fps 固定
    int msec = int(f * (1000.0 / 60.0) + 0.5);

    const std::string& ph = phones[f];
    std::cerr << "[DEBUG] frame=" << f << " phone=\"" << (ph.empty() ? "(none)" : ph) << "\"\n";

    // ① phones[f] から母音文字を抽出する（大文字化・小文字対応・長母音対応）
    char v = 0;
    //const std::string& ph = phones[f];
    if (!ph.empty()) {
      char c = std::toupper(ph[0]);
      if (c == 'A' || c == 'I' || c == 'U' || c == 'E' || c == 'O') {
	v = c;
      }
    }

    // ② 各母音フラグを立てる
    float A = (v == 'A') ? 1.0f : 0.0f;
    float I = (v == 'I') ? 1.0f : 0.0f;
    float U = (v == 'U') ? 1.0f : 0.0f;
    float E = (v == 'E') ? 1.0f : 0.0f;
    float O = (v == 'O') ? 1.0f : 0.0f;

    // ③ 母音区間の開始／終了を検出
    if (!in_region && v) {
      in_region = true;
      cur.start_frame = f;
      cur.start_msec = msec;
      cur.vowel = v;
    }
    else if (in_region && !v) {
      in_region = false;
      cur.end_frame = f;
      cur.end_msec = msec;
      regions.push_back(cur);
    }

    // ④ dB 計算は（前回提示の）全次元二乗和方式
    double Esum = 1e-12;
    for (float c0 : feats[f])
      Esum += double(c0) * double(c0);
    float db = 10.0f * std::log10(float(Esum));

    // ⑤ ファイル出力
    outf << f << ", "
      << msec << ", "
      << std::fixed << std::setprecision(6) << width << ", "
      << height << ", " << 0.0f << ", "   // tongue
      << A << ", " << I << ", " << U << ", " << E << ", " << O << ", " << db << "\n";
  }

  // 最後まで母音継続中なら Region を閉じる
  if (in_region) {
    cur.end_frame = feats.size();
    cur.end_msec = int(feats.size() * (1000.0 / 60.0) + 0.5);
    regions.push_back(cur);
  }

}


int main(int argc, char** argv) {

  const char* config_file = "Main.jconf";

  // Julius 初期化
  Jconf* jconf = j_config_load_file_new(const_cast<char*>(config_file));
  if (!jconf) {
    return 1;
  }

  // 2) Main.jconf 自身を開き、-filelist 行をパース
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
      std::cerr << "ERROR: -filelist not found in " << config_file << "\n";
      return 1;
    }
  }

  // 3) filelist.txt から最初の有効な行を取得
  {
    std::ifstream ifs(filelist_path);
    if (!ifs) {
      std::cerr << "ERROR: cannot open filelist: " << filelist_path << "\n";
      return 1;
    }
    std::string line;
    while (std::getline(ifs, line)) {
      size_t p = line.find_first_not_of(" \t");
      if (p == std::string::npos) continue;
      if (line[p] == '#' || line[p] == ';') continue;
      g_wav_path = line.substr(p);
      break;
    }
    if (g_wav_path.empty()) {
      std::cerr << "ERROR: no valid WAV in " << filelist_path << "\n";
      return 1;
    }
    std::cerr << "[DEBUG] using WAV: " << g_wav_path << "\n";
  }

  // 4) 出力ファイルをオープン
  outf.open("output.adxlip", std::ios::out | std::ios::trunc);
  if (!outf) {
    std::cerr << "Cannot open output.adxlip\n";
    return 1;
  }

  // 5) Julius インスタンス作成
  Recog* recog = j_create_instance_from_jconf(jconf);
  if (!recog) {
    std::cerr << "Failed to create Julius instance\n";
    j_jconf_free(jconf);
    return 1;
  }

  // 6) コールバック登録
  callback_add(recog, CALLBACK_EVENT_PASS1_END, pass1_end_cb, nullptr);
  callback_add(recog, CALLBACK_RESULT, result_cb, nullptr);

  // 7) 認識開始（filelist モード）
  if (j_adin_init(recog) == FALSE || j_open_stream(recog, nullptr) != 0) {
    std::cerr << "Audio init/open failed\n";
    j_recog_free(recog);
    j_jconf_free(jconf);
    return 1;
  }
  j_recognize_stream(recog);

  // 8) 後始末
  outf.close();
  j_recog_free(recog);
  return 0;
}


