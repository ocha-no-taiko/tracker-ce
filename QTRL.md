# Q.T.R.L.：Polyend Tracker を研究室の端末にする

Q.T.R.L.（Quick Terminal for Remote & Logs）の「Terminal」役のファームです。Tracker に Q.T.R.L. 画面を足し、Mac の `qtrl-server` と USB MIDI でやり取りして、機器の状態・ログ・チャットを表示します。

- **ファーム側**（このリポジトリの `core/interface/qtrl/`）：Q.T.R.L. 画面（Home / Status / Logs / Chat / Help / QUIT）。
- **サーバー側**：別リポジトリ `qtrl`（`server/`）。Uno Q（QTRL1）、ESP32-CAM（QTRL2）、プリンター（FAB1）の情報をまとめて、この画面に送ります。

DAW Control（Live Tracker）と同じ作りです。違いは、相手が Ableton Live ではなく `qtrl-server` であることと、使う MIDI チャンネル（既定 ch15）だけです。

> 対象は初代 Tracker（MK66）です。SysEx の送信は MK66 のビルドだけに入っています。
> 実機での表示（細い線、8 px フォント、パッドのキーボード）は**未確認**です。画面の見え方はホスト上のプレビュー（下の「テスト」）でしか確かめていません。

## 開き方

Config > MIDI > **Q.T.R.L.** を選ぶと画面が開きます。閉じるときは QUIT タブで QUIT を押すか、Shift + Config です。

## 操作（既定の割り当て）

| Tracker の操作 | 動作 |
|---|---|
| 画面下ボタン 1〜8 | Home / Status / Logs / Chat / Help / Undo / Redo / QUIT |
| Shift + 左右の矢印 | タブを前後に切り替える |
| 左／右の矢印 | Undo／Redo（チャットのやり取りを 1 組ずつ取り消す／戻す）。Logs タブでは上下左右の矢印で日付を選ぶ |
| Performance / File / Params | Status / Logs / Help |
| Shift + Insert | Home |
| Delete | QUIT タブ |
| Config | Tracker の Config を開く（Shift + Config は画面を閉じる） |
| パッド（48 個） | Chat タブのキーボード（Shift で記号・大文字、CAPS で固定） |
| Play | チャットを送る（Shift + Play は改行） |
| ジョグ | ログ・チャットのスクロール |

割り当て・文言・色・キー配列は、SD カードの `/QtrlControl.json` で変えられます（見本：`sd-card/QtrlControl.json`）。画面を開くたびに読み直します。

## 入れ方

1. `PolyendTracker_QTRL_x.y.z.ptf` を通常のファームと同じ手順で書き込む。
2. SD カードの直下に `Qtrl.ptfont` と `QtrlControl.json` を置く（フォントが無いときは本体のフォントで描くので、文字がはみ出すことがあります）。
3. Mac で `qtrl-server` を起動し、Tracker を USB でつなぐ。

## 通信の取り決め

すべて `controlChannel`（既定 ch15）で送ります。Live Tracker の ch16・CC 番号・SysEx（`7D 50 54`）には触れません。

- **CC119（hello）**：127 ＝画面を開いた／サーバーが起動した、0 ＝閉じた。127 を受け取ったサーバーは状態をすべて送り直す。
- **SysEx**：`F0 7D 51 54 <種類> <番号 上位7bit> <番号 下位7bit> <ASCII> F7`。Tracker は 1 個 128 バイトまで、256 個まで溜めて受け取る。

| 向き | 種類 | 番号 | 文字列 |
|---|---|---|---|
| サーバー→ | 0x01 サーバー名 | 0 | 名前（届くと接続中の表示になる） |
| サーバー→ | 0x02 お知らせ | 0 | タイトルバーに 6 秒出す文言 |
| サーバー→ | 0x10 ノード | 0〜4 | `状態<TAB>名前<TAB>補足<TAB>状態の文言`（状態は O / A / U。名前・補足が空なら JSON の値のまま） |
| サーバー→ | 0x11 システム全体 | 0 | `状態<TAB>文言` |
| サーバー→ | 0x12 詳細行 | 0〜5 | 1 行（番号 0 で一覧を作り直す） |
| サーバー→ | 0x20 ログの日付 | 0〜119 | `YYYY.MM.DD`（番号 0 で一覧を作り直す） |
| サーバー→ | 0x21 / 0x22 / 0x23 | | ログの始まり（日付）／ 1 行（`時刻<TAB>本文`、400 行まで）／ 終わり（番号 ＝行数） |
| サーバー→ | 0x30〜0x34 | | チャットの消去／発言の始まり（話し手）／ 本文の一部／ 終わり／ 応答中（番号 1・0） |
| →サーバー | 0x40 ログの要求 | 日付の位置 | `YYYY.MM.DD` |
| →サーバー | 0x41 / 0x42 | 部分の番号 | 送る文の一部（60 バイトずつ）／ 最後の部分 |
| →サーバー | 0x43 / 0x44 | 0 | Undo / Redo |

定義の本体は `core/interface/qtrl/qtrlConfig.h`（`QtrlSysEx`）です。サーバー側の `qtrl_server/protocol.py` と数字をそろえてください。

## ビルド

DAW Control と同じです（`DAW_CONTROL.md` の「ビルド」）。フォントの作り直し：

```bash
uv run --with pillow tools/fonts/make_ptfont.py --layout qtrl tools/fonts/AnonymousPro/AnonymousPro-Regular.ttf tools/fonts/AnonymousPro/AnonymousPro-Bold.ttf sd-card/Qtrl.ptfont
```

## テスト

ハードウェアなしで確かめられるもの：

```bash
sh tests/qtrl/run.sh
```

```bash
sh tests/qtrl/preview.sh
```

1 つ目は `/QtrlControl.json` の読み込み、2 つ目は 8 画面ぶんの PNG（`tests/qtrl/out/`）と、1 フレームの表示リストの語数（上限 2043）を出します。

## まだできないこと・未確認

- 実機での表示・操作・USB MIDI の往復（すべて未確認）。
- Tracker+ / Mini では SysEx を送れないため、チャットとログの日付選びが動きません。
- 日本語は表示できません（ASCII と © のみ）。
