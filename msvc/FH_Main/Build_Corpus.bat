@echo off
setlocal EnableDelayedExpansion
REM ================================================
REM Build_Corpus.bat
REM  言語モデル（ビグラムモデル）と辞書（HTK-DIC）を生成するバッチスクリプト
REM  使用ファイル：lt_corpus.txt, lt_words.txt, bccwj.60k.htkdic
REM ================================================

REM 1) スクリプト所在ディレクトリへ移動
pushd "%~dp0" || (
  echo ERROR: Cannot change to script directory "%~dp0"
  exit /b 1
)

REM 2) 言語モデルディレクトリへ移動
pushd "thirdparty\dictation-kit\model\lang_m" || (
  echo ERROR: Cannot change to lang_m directory "%CD%"
  popd
  exit /b 1
)


REM ------------------------------------------------
REM [1] 既存のHTK辞書から語彙リストを抽出
REM    bccwj.60k.htkdicの先頭列（単語）だけをcurrent.vocab.txtに出力
REM ------------------------------------------------
echo [1] Extract existing vocabulary from bccwj.60k.htkdic
if exist current.vocab.txt del /f /q current.vocab.txt
(
  for /f "tokens=1" %%A in ('findstr /v "^#" bccwj.60k.htkdic') do @echo %%A
) > current.vocab.txt


REM ------------------------------------------------
REM [2] カスタム単語（lt_words.txt）と結合 → merged_vocab
REM    lt_words.txtに新規追加したい単語を1行ずつ記載
REM ------------------------------------------------
echo [2] Merge current.vocab.txt and lt_words -> merged_vocab
if not exist lt_words.txt (
  echo ERROR: lt_words not found in "%CD%"
  popd & popd
  exit /b 1
)
copy /y current.vocab.txt merged_vocab >nul
type lt_words.txt >> merged_vocab


REM ------------------------------------------------
REM [3] 語彙をソート＆重複除去
REM    sortで並べ替えたあと、連続重複行をprev変数でスキップ
REM ------------------------------------------------
echo [3] Sort and remove duplicates -> merged_vocab
if exist merged_temp.txt del /f /q merged_temp.txt
set "prev="
for /f "delims=" %%L in ('sort merged_vocab') do (
  if "%%L" neq "!prev!" (
    echo %%L>>merged_temp.txt
    set "prev=%%L"
  )
)
move /y merged_temp.txt merged_vocab >nul


REM ------------------------------------------------
REM [4] merged_vocabとコーパス(lt_corpus.txt)から
REM     バイナリ形式のビグラム言語モデルを生成
REM ------------------------------------------------
echo [4] Generate new.bingram from merged_vocab and lt_corpus.txt
set "MKBINGRAM=D:\Misc\julius\msvc\Release\mkbingram.exe"
if not exist "%MKBINGRAM%" (
  echo ERROR: mkbingram.exe not found at "%MKBINGRAM%"
  popd & popd
  exit /b 1
)

REM ↓ 入力ファイルと出力ファイルを位置引数で渡す
"%MKBINGRAM%" -order 2 -unk -wbdiscount -vocab merged_vocab "%~dp0lt_corpus.txt" new.bingram

if errorlevel 1 (
  echo ERROR: mkbingram failed
  popd & popd
  exit /b 1
)


REM ------------------------------------------------
REM [5] merged_vocabに基づきHTK辞書(new.htkdic)を生成
REM    bccwj.60k.htkdicからmerged_vocabの単語行だけを抜き出す
REM ------------------------------------------------
echo [5] Generate new.htkdic from merged_vocab and bccwj.60k.htkdic
if exist new.htkdic del /f /q new.htkdic
for /f "usebackq delims=" %%W in ("merged_vocab") do (
  REM 行頭が%%Wタブで始まる行を抽出
  findstr /b /c:"%%W	" "bccwj.60k.htkdic" >> new.htkdic
)

if not exist new.htkdic (
  echo ERROR: mkdfa.py failed
  popd & popd
  exit /b 1
)

echo ================================================
echo Done: new.bingram and new.htkdic created in "%CD%"
popd & popd
endlocal
exit /b 0
