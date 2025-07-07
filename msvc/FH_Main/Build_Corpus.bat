@echo off
setlocal EnableDelayedExpansion
REM ================================================
REM Build_Corpus.bat
REM  ���ꃂ�f���i�r�O�������f���j�Ǝ����iHTK-DIC�j�𐶐�����o�b�`�X�N���v�g
REM  �g�p�t�@�C���Flt_corpus.txt, lt_words.txt, bccwj.60k.htkdic
REM ================================================

REM 1) �X�N���v�g���݃f�B���N�g���ֈړ�
pushd "%~dp0" || (
  echo ERROR: Cannot change to script directory "%~dp0"
  exit /b 1
)

REM 2) ���ꃂ�f���f�B���N�g���ֈړ�
pushd "thirdparty\dictation-kit\model\lang_m" || (
  echo ERROR: Cannot change to lang_m directory "%CD%"
  popd
  exit /b 1
)


REM ------------------------------------------------
REM [1] ������HTK���������b���X�g�𒊏o
REM    bccwj.60k.htkdic�̐擪��i�P��j������current.vocab.txt�ɏo��
REM ------------------------------------------------
echo [1] Extract existing vocabulary from bccwj.60k.htkdic
if exist current.vocab.txt del /f /q current.vocab.txt
(
  for /f "tokens=1" %%A in ('findstr /v "^#" bccwj.60k.htkdic') do @echo %%A
) > current.vocab.txt


REM ------------------------------------------------
REM [2] �J�X�^���P��ilt_words.txt�j�ƌ��� �� merged_vocab
REM    lt_words.txt�ɐV�K�ǉ��������P���1�s���L��
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
REM [3] ��b���\�[�g���d������
REM    sort�ŕ��בւ������ƁA�A���d���s��prev�ϐ��ŃX�L�b�v
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
REM [4] merged_vocab�ƃR�[�p�X(lt_corpus.txt)����
REM     �o�C�i���`���̃r�O�������ꃂ�f���𐶐�
REM ------------------------------------------------
echo [4] Generate new.bingram from merged_vocab and lt_corpus.txt
set "MKBINGRAM=D:\Misc\julius\msvc\Release\mkbingram.exe"
if not exist "%MKBINGRAM%" (
  echo ERROR: mkbingram.exe not found at "%MKBINGRAM%"
  popd & popd
  exit /b 1
)

REM �� ���̓t�@�C���Əo�̓t�@�C�����ʒu�����œn��
"%MKBINGRAM%" -order 2 -unk -wbdiscount -vocab merged_vocab "%~dp0lt_corpus.txt" new.bingram

if errorlevel 1 (
  echo ERROR: mkbingram failed
  popd & popd
  exit /b 1
)


REM ------------------------------------------------
REM [5] merged_vocab�Ɋ�Â�HTK����(new.htkdic)�𐶐�
REM    bccwj.60k.htkdic����merged_vocab�̒P��s�����𔲂��o��
REM ------------------------------------------------
echo [5] Generate new.htkdic from merged_vocab and bccwj.60k.htkdic
if exist new.htkdic del /f /q new.htkdic
for /f "usebackq delims=" %%W in ("merged_vocab") do (
  REM �s����%%W�^�u�Ŏn�܂�s�𒊏o
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
