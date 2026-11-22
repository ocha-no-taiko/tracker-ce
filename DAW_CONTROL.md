# DAW Control：Polyend Tracker を Ableton Live のコントローラーにする

初代 Polyend Tracker のボタン・パッド・ジョグホイールで、Ableton Live の再生・録音・クリップ起動などを操作するための改造です。Push のような使い方を目指しています。

- **ファーム側**（このリポジトリの `core/interface/dawControl/`）：Tracker に「DAW Control」画面を追加します。この画面では、操作を USB MIDI で Live に送り、Live から届いた状態をパッドの LED と画面に表示します。
- **Live 側**（`ableton/PolyendTrackerDAW/`）：MIDI Remote Script（Control Surface）です。どのボタンで何をするかは、ここの `mapping.json` で決めます。

> 対象は初代 Tracker（MK66）です。Tracker+ / Mini 用のビルドも通りますが、実機での確認はしていません。
> 改造ファームは自己責任で使ってください（公式の README にある注意書きのとおり、保証の対象外です）。

## できること（既定の割り当て）

| Tracker の操作 | Live の動作 |
|---|---|
| Play | 再生／停止（Shift：続きから再生） |
| Rec | Arrangement 録音（Shift：Session 録音） |
| Params（Instrument Parameters） | ミキサーの表示／非表示 ※1 |
| Performance | Session／Arrangement ビューの切り替え（Tab キーと同じ） |
| File | ブラウザの表示／非表示 |
| Config | 設定ウィンドウを開く ※1 |
| Master | Main トラックのデバイスビューを開く／閉じる（閉じると元のトラックに戻る） |
| Note | 選択中のクリップのクリップビューを開く／閉じる |
| Instr | 選択中のトラックのデバイスビューを開く／閉じる |
| Vol（FX1）／Fx（FX2） | 選択中のトラックのデバイスを左／右に選ぶ |
| Copy | コピー（Shift：ペースト） ※1 |
| Insert | オートメーションを再有効化（Shift：Home キー） ※2 |
| Delete | 選択範囲を削除 ※1 |
| 矢印 | カーソル移動（キーボードの矢印キーと同じ） ※1 |
| Shift + 矢印 | 範囲選択（キーボードの Shift + 矢印キーと同じ） ※1 |
| パッド（Session モード） | 赤枠内のクリップを起動 |
| パッド（Note モード） | 鍵盤として演奏（MIDI ch1） |
| 画面下ボタン 1（Act） | 選択中のトラックのオン／オフ（トラックアクティベーター） |
| 画面下ボタン 2（Solo） | 選択中のトラックのソロ |
| 画面下ボタン 3（Rec Arm） | 選択中のトラックの録音アーム |
| 画面下ボタン 4（Undo） | 元に戻す（Shift：やり直し） |
| 画面下ボタン 5（Redo） | やり直し |
| 画面下ボタン 6（Metro） | メトロノーム（Shift：タップテンポ） |
| 画面下ボタン 7 | Session／Note モード切り替え（Tracker 側で処理） |
| 画面下ボタン 8（Exit）または Shift + Config | DAW Control 画面を閉じる |
| Pattern | 元に戻す（Shift：やり直し） |
| Song | タップテンポ |
| ジョグホイール | 右に回すと拡大、左に回すと縮小（Arrangement の時間軸。Shift でテンポ ±1） |
| Up／Down（Note モード中） | オクターブ上下（Tracker 側で処理。カーソル移動にはならない） |

- 赤枠（12 トラック × 4 シーン）は選択中のトラック・シーンについていきます（`mapping.json` の `ringFollowsSelection`）。
- ※1 Live の API に無い操作なので、Mac のキーボードショートカットとして Live に送ります（下の「キーボード操作について」）。
- ※2 Live の Home キーは、Arrangement では挿入マーカーを先頭へ、Session では先頭のトラックへ移動します。

パッドの LED はクリップの状態を明るさと点滅で表します（空＝消灯、停止中＝暗め、再生中＝最大、再生待ち・録音中＝点滅）。Tracker のパッドは単色なので、クリップの色は出ません。

### キーボード操作について（※1）

コピー・ペースト・削除・矢印・範囲選択・設定・ミキサーは、Live の API に操作が無いため、同梱の `keysender.js` が Mac のキー入力として Live に送ります。

- **macOS だけ**で動きます。
- 最初に押したとき、macOS が Live に「アクセシビリティ」の許可を求めます。**システム設定 > プライバシーとセキュリティ > アクセシビリティ** で Ableton Live をオンにしてください。オンにするまでキーは届きません。
- キーは **Live が最前面のときだけ**送ります。ほかのアプリを使っているときに押しても、そのアプリには何も入りません。
- 送った結果は `Remote Scripts/PolyendTrackerDAW/keysender.log` に残ります（許可の有無もここに出ます）。
- `mapping.json` では、`"key:cmd+shift+c"` のように書くと好きなショートカットを割り当てられます。

## 1. ファームを書き込む

1. ビルド済みの `PolyendTracker_DAW_x.y.z.ptf` を、SD カードの `/Firmware/` に入れます（ビルド方法は下の「ビルド」を参照）。
2. 必要なら `sd-card/DawControl.json` を SD カードの一番上（`/DawControl.json`）にコピーします。無くても既定値で動きます。
   - 画面下のラベルはこの JSON が優先されます。古い JSON が SD カードに残っていると、ファームを新しくしてもラベルは古いままです。ラベルだけ変えたいときは、JSON を差し替えるだけで済みます（ファームの書き直しは不要）。
3. Tracker で **Config > Firmware > Firmware update** を実行します。
4. 起動しなくなった場合は、`/Firmware/` に公式の `.ptf` だけを入れて電源を入れ、本体底面のリセット穴を押すと戻せます（公式 README の「Emergency Recovery」）。

## 2. Live にスクリプトを入れる

```sh
sh ableton/install.sh
```

`~/Music/Ableton/User Library/Remote Scripts/PolyendTrackerDAW/` にコピーされます。すでにある `mapping.json` が新しい既定値と違う場合は、その内容を残し、新しい既定値を `mapping.default.json` として隣に置きます。新しい既定値に置き換えるときは次のとおりです。

```sh
RESET_MAPPING=1 sh ableton/install.sh
```

スクリプト（`.py`）を入れ替えたときは、Live の再起動が必要です。

1. Live を再起動します。
2. **設定 > Tempo & MIDI** の「コントロールサーフェス」で、空いている行に次を設定します。
   - コントロールサーフェス：`PolyendTrackerDAW`
   - 入力：`Tracker`
   - 出力：`Tracker`
3. 下の「MIDI ポート」で、入力の `Tracker` の **トラック** をオンにします（Note モードの演奏をトラックに届けるため）。

## 3. 使う

1. Tracker と Mac を USB でつなぎます。
2. Tracker で **Shift + Config** を押すか、**Config > MIDI > DAW Control** を選びます。
3. 画面下に「Ableton connected」と出れば接続できています。「Waiting for Ableton...」のままなら、Live の Control Surface の設定を見直してください。

DAW Control 画面を開いている間は、Tracker 自身のシーケンサーは止まり、USB のクロックや MIDI の受信も止まります。画面を閉じると元に戻ります。

## 設定を変える

### Live 側：`mapping.json`（操作の割り当て）

インストール先の `Remote Scripts/PolyendTrackerDAW/mapping.json` を編集して、Live の Control Surface をいったん「なし」にしてから選び直すと反映されます。

```json
"buttons": {
  "Play": { "press": "play_toggle", "shift": "continue" }
}
```

- ボタン名：`Screen1`〜`Screen8`、`Params`、`Performance`、`File`、`Config`、`Master`、`SampleLoad`、`SamplePlay`、`SampleEdit`、`SampleRec`、`Song`、`Note`、`Instr`、`Vol`、`Fx`、`Pattern`、`Play`、`Copy`、`Insert`、`Up`、`Delete`、`Rec`、`Shift`、`Left`、`Down`、`Right`
- 使える動作：`toggle_mixer`、`automation_mode`、`toggle_session_arrangement`、`toggle_browser`、`open_settings`、`toggle_main_devices`、`toggle_clip_view`、`toggle_device_view`、`device_prev`、`device_next`、`copy`、`paste`、`delete`、`home`、`cursor_up`／`down`／`left`／`right`、`select_up`／`down`／`left`／`right`、`re_enable_automation`、`track_activator`、`solo_selected_track`、`play_toggle`、`continue`、`stop`、`arrangement_record`、`session_record`、`overdub`、`metronome`、`tap_tempo`、`tempo_up`、`tempo_down`、`undo`、`redo`、`capture_midi`、`stop_all_clips`、`stop_selected_track`、`arm_selected_track`、`track_prev`、`track_next`、`scene_prev`、`scene_next`、`ring_left`、`ring_right`、`ring_up`、`ring_down`、`ring_page_left`、`ring_page_right`、`ring_page_up`、`ring_page_down`、`scene_launch_1`〜`scene_launch_4`、`key:<ショートカット>`（例：`key:cmd+shift+c`。修飾キーは `cmd`・`shift`・`alt`（`option`）・`ctrl`）
- ジョグ（`jog`）：`zoom`、`scene_select`、`track_select`、`tempo`
- LED の明るさ（`ledValues`）：0＝消灯、1〜31＝点灯、33〜63＝点滅（明るさは値−32）

### Tracker 側：SD カードの `/DawControl.json`

MIDI チャンネル、Note モードの音域・スケール、Tracker 側で処理するボタン（終了・モード切り替え・オクターブ）、画面下のラベルを変えられます。見本は `sd-card/DawControl.json` です。DAW Control 画面を開くたびに読み直します。

- ボタンは名前（`"Screen8"`、`"Up"` など）か番号で書けます。`null` にすると無効になります。
- `controlChannel` などの MIDI 番号を変えたときは、`mapping.json` 側も同じ値にそろえてください。

## 通信の取り決め

USB MIDI のポートは1つなので、チャンネルで役割を分けています（既定値）。

| 向き | 内容 | メッセージ |
|---|---|---|
| Tracker → Live | ボタン（ID 0〜43） | ch16 CC（20＋ID）、押す 127／離す 0 |
| Tracker → Live | パッド（Session） | ch16 ノート 36〜83 |
| Tracker → Live | パッド（Note） | ch1 ノート |
| Tracker → Live | ジョグ | ch16 CC70、64＋変化量 |
| 双方向 | 接続の合図 | ch16 CC119、127＝開いた／0＝閉じた |
| Live → Tracker | パッド LED | ch16 ノート 36〜83、ベロシティ＝LED の値 |
| Live → Tracker | 状態 | ch16 CC100〜104（再生、Arrangement 録音、Session 録音、オーバーダブ、メトロノーム） |
| Live → Tracker | テンポ | ch16 CC110／111（テンポ×10 の上位7ビット／下位7ビット） |
| Live → Tracker | 赤枠と選択トラック | ch16 CC112（トラック位置）、CC113（シーン位置）、CC114（選択トラック） |

## ビルド

GNU Arm Embedded Toolchain 10.3-2021.10 が必要です（公式 README の手順と同じ）。

```sh
make original ORIGINAL_POSTFIX=DAW_1.1.0
```

`build/PolyendTracker_DAW_1.1.0.ptf` ができます。

## テスト

```sh
sh tests/dawControl/run.sh                         # ファームの JSON 読み込み（Mac/Linux で実行）
python3 ableton/tools/test_script_offline.py       # Remote Script の動作（Live 不要）
python3 ableton/tools/fake_tracker.py selftest 300 # Live と実際に通信（python-rtmidi が必要）
python3 ableton/tools/fake_tracker.py --port "Tracker Test" selftest 300  # 実機をつないだまま試すとき
```

`fake_tracker.py` は「Tracker」という名前の仮想 MIDI ポートを作り、Tracker のふりをします。実行中に Live の Control Surface で入出力を `Tracker` にすると、自動テストが進みます。

## まだできないこと

- トラック名やクリップ名を Tracker の画面に出すこと（SysEx の対応が必要）
- パッドのベロシティ（初代 Tracker のパッドはベロシティを送りません）
- デバイスのパラメーター操作
