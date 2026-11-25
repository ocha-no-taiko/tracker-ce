# Synthesis of the Images：Polyend Tracker で「深度で重ねる画像 3 枚と音」を操作する

Synthesis of the Images（SOTI）は、Mac の `soti-server` が鳴らす映像と音を、Tracker から操作するためのファームです。Tracker に SOTI 画面を足し、server と USB MIDI でやり取りします。

- **ファーム側**（このリポジトリの `core/interface/soti/`）：ファイルの一覧（左）、15 個の値（右、Layer / Chroma / Blur / Resolution / Space × A / B / C）、下の 8 個のボタン。
- **Mac 側**：別リポジトリ `synthesis-of-the-images`（非公開）。画像の深度解析（Depth Pro 2）、`.soti` ファイル、映像（WebGL2）と音（Web Audio）が入っています。

DAW Control（ch16）・Q.T.R.L.（ch15）・Q-4（ch14）と同じ作りです。チャンネル（既定 ch13）と SysEx の ID（`7D 53 4F`）が違うので、同時に入れても混ざりません。

> 対象は初代 Tracker（MK66）です。
> 実機での表示と操作感は**未確認**です。画面の見え方はホスト上のプレビュー（下の「テスト」）でしか確かめていません。

## 開き方

Config > MIDI > **Synthesis of the Images** を選ぶと画面が開きます。閉じるときは QUIT を 2 回（3 秒以内）押すか、Shift + Config です。

## 操作（既定の割り当て）

| Tracker の操作 | 動作 |
|---|---|
| File / Performance | カーソルをファイル側 / 操作側へ |
| 矢印 | ファイルを選ぶ（3 列、9 個を超えたらスクロール）／ 値を選ぶ |
| ジョグ | ファイル側：1 つずつ選ぶ ／ 操作側：選んだ値を変える（Shift で細かく） |
| 画面下 1〜4 | Load into A / B / C / All（All は同じファイルを 3 つ全部に） |
| 画面下 5〜7 | SLCT row（同じ行の A・B・C）/ column（同じ列の 5 つ）/ All（15 個）。同じボタンをもう一度で 1 個に戻る |
| 画面下 8 | QUIT（もう一度押すと閉じる） |
| Delete | 選んでいる値を既定値に戻す |
| Play | 音の一時停止・再開 |
| Shift + Config | 画面を閉じる |

ファイルのアイコンには、読み込んでいるスロット（A B C）が黄色で出ます。`...` は深度解析待ち、`!` は解析に失敗したファイルです。タイトルバーの右には、A/B/C に入っているファイル（またはお知らせ）と、カーソルの値（%）が出ます。

文言・色・ボタン割り当ては、SD カードの `/SotiControl.json` で変えられます（見本：`sd-card/SotiControl.json`）。画面を開くたびに読み直します。

## 入れ方

1. `PolyendTracker_SOTI_x.y.z.ptf` を通常のファームと同じ手順で書き込む。
2. SD カードの直下に `Soti.ptfont` と `SotiControl.json` を置く（フォントが無いときは本体のフォントで描くので、文字がはみ出すことがあります）。
3. Mac で `soti` を起動し、Tracker を USB でつなぐ（ポート名に "Tracker" を含むものを自動で探します）。

## 通信の取り決め

すべて `controlChannel`（既定 ch13）で送ります。番号は Mac 側の `soti/protocol.py` と揃えています。

- **CC119（hello）**：127 ＝画面を開いた／server が起動した、0 ＝閉じた。127 を受け取った server は一覧・値・状態をすべて送り直す。
- **Tracker → server（CC）**

| CC | 名前 | 値 |
|---|---|---|
| 20 | Focus | 0 ＝ファイル、1 ＝操作 |
| 21 / 22 | FileHi / FileLo | ファイルのカーソル（上位 7 bit を先に、下位で確定） |
| 23 | Control | 値のカーソル 0〜14（行 × 3 ＋列、行＝パラメーター、列＝ A/B/C） |
| 24 / 25 | Jog / JogFine | 64 ＋ステップ数 |
| 26 | Load | 0 ＝ A、1 ＝ B、2 ＝ C、3 ＝全部（直前に送ったファイル） |
| 27 | Select | 1 ＝行、2 ＝列、3 ＝全部（同じ値でもう一度 ＝ 1 個に戻る） |
| 28 | Reset | 選んでいる値を既定値に |
| 29 | Play | 一時停止・再開 |
| 30 | Refresh | すべて送り直してもらう |

Tracker はジョグ・Reset・Select の直前に Control を、Load の直前にファイルのカーソルを送り直します（ブラウザの操作パネルがカーソルを動かしていても、Tracker のカーソルで効くように）。

- **server → Tracker（SysEx）**：`F0 7D 53 4F <種類> <データ> F7`。14 bit の値は上位 7 bit・下位 7 bit の順。Tracker は 1 個 96 バイトまで、256 個まで溜めて受け取る。

| 種類 | 中身 |
|---|---|
| 0x01 一覧 | ファイル数（14 bit）。名前は続く 0x02 で届く |
| 0x02 ファイル | 番号（14 bit）, フラグ（bit0〜2 ＝ A/B/C に入っている, bit3 解析待ち, bit4 失敗）, 名前（ASCII） |
| 0x03 値 | 15 個（14 bit ずつ）。Layer A B C, Chroma A B C, … の順 |
| 0x04 状態 | 選択（0 ＝ 1 個, 1 ＝行, 2 ＝列, 3 ＝全部）, フラグ（bit0 再生中, bit1 解析中）, `A<TAB>B<TAB>C` のファイル名 |
| 0x05 お知らせ | タイトルバーに数秒出す文言 |

## テスト

```bash
sh tests/soti/run.sh
```

```bash
sh tests/soti/preview.sh /tmp/soti_preview
```

- `run.sh`：`SotiControl.json` の読み込みと、見本の JSON ＝組み込みの既定値であることを確かめる。
- `preview.sh`：本物の描画コードをホストで動かし、8 画面の PNG を作る。2 つ目の引数にモックアップを渡すと下に並べる。1 フレームの上限（2043 語）を超える画面があると失敗する。

## ビルド

```bash
make original NPROC=$(sysctl -n hw.ncpu) ORIGINAL_POSTFIX=SOTI_0.1.0
```

フォント `sd-card/Soti.ptfont` は Q-4 と同じもの（Anonymous Pro、`--layout qtrl`）です。

1 MB の FLASH がほぼ埋まっているため、SOTI の 4 ファイルと Q-4・Q.T.R.L. の設定読み込み（`q4Config.cpp` `qtrlConfig.cpp`）だけ `-Os` で組んでいます（ファイル先頭の `#pragma GCC optimize("Os")`）。どれも画面を開いたとき・操作したときにしか動かないので、音の処理の速さには関わりません。0.1.0 の時点で FLASH は 97.6%（残り約 24 KB）です。
