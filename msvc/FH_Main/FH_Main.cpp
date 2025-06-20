
// FH_Main.cpp の先頭で標準ヘッダを先にインクルード
#include <iostream>
#include <cstdio>
#include <cstdlib>

// そのあとに Julius 関連
#include "julius/juliuslib.h"

static void result_callback(Recog* recog, void* dummy)
{
  for (RecogProcess* r = recog->process_list; r; r = r->next)
  {
    if (!r->live) continue;
    Sentence* s = &(r->result.sent[0]);
    WORD_INFO* winfo = r->lm->winfo;
    printf("認識結果: ");
    for (int i = 0; i < s->word_num; i++) {
      printf("%s ", winfo->woutput[s->word[i]]);
    }
    printf("\n");
  }
}

int main()
{
  char config_path[] = "Main.jconf";
  Jconf* jconf = j_config_load_file_new(config_path);
  if (!jconf) {
    fprintf(stderr, "jconfの読み込みに失敗しました。\n");
    return 1;
  }

  // Juliusインスタンス作成
  Recog* recog = j_create_instance_from_jconf(jconf);
  if (!recog) {
    fprintf(stderr, "Juliusインスタンスの作成に失敗しました。\n");
    return 1;
  }

  // コールバック登録
  callback_add(recog, CALLBACK_RESULT, result_callback, NULL);

  // 音声入力の初期化
  if (j_adin_init(recog) == FALSE) {
    fprintf(stderr, "音声入力初期化に失敗しました。\n");
    return 1;
  }

  // 音声ストリームを開いて認識開始
  if (j_open_stream(recog, NULL) != 0)
  {
    fprintf(stderr, "音声ストリームのオープンに失敗しました。\n");
    return 1;
  }

  // 認識実行（blocking）
  j_recognize_stream(recog);

  // 解放
  j_recog_free(recog);

  return 0;
}

