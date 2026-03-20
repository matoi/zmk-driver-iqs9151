# Cross-Pad Gesture Design

> **⚠️ PoC (Proof of Concept)**
> この機能は本家ドライバには含まれていない独自拡張です。
> 動作は無保証であり、予告なく変更・削除される可能性があります。

クロスパッドジェスチャー: 左右分割キーボードの各側にあるトラックパッド間で連携し、
片方にタッチした状態でもう片方を操作することで特殊ジェスチャーを実現する機能の設計。

**関連ドキュメント:**
- [通信設計](cross_pad_communication.md) — EV_MSC, INVOKE_BEHAVIOR, split transport の詳細
- [実装設計](cross_pad_gesture_implementation.md) — ピンチ・プレス＆ホールドの詳細設計
- [リファレンス](cross_pad_reference.md) — データ構造、Kconfig、設計判断一覧

## 前提条件

### ハードウェア構成

```
┌─────────────────┐          BLE           ┌─────────────────┐
│   左側 MCU       │  ◄──────────────────►  │   右側 MCU       │
│  (peripheral)    │    ZMK split transport  │  (central)       │
│  iqs9151 × 1    │                         │  iqs9151 × 1    │
└─────────────────┘                         └─────────────────┘
```

- 左右分割キーボードの各側に iqs9151 トラックパッドが1つずつ
- 通信は ZMK の split transport (BLE) を介して行われる
- 右側が central、左側が peripheral（キーボード設計による）

### 制約

- peripheral → central: EV_MSC input event で転送
- central → peripheral: INVOKE_BEHAVIOR コマンドで転送
- BLE 経由のため数十 ms の遅延が発生しうる
- トラックパッドが1つしかない構成でも安全に動作する (`peer_fc` が常に 0)

## アーキテクチャ

### 設計原則: ディスパッチテーブル + ステートフル継続

初期設計ではセッションベースのモード管理を検討したが、
実装では**ディスパッチテーブル**方式を採用した。

各フレームで `iqs9151_cross_pad_resolve()` を呼び、
`local_fc` と `peer_fc` から `enum cross_pad_gesture` を導出する。

ただし、**ジェスチャー開始前に安定化待ち (stabilization) を行う**:

- 両側タッチが検出されジェスチャーが新規に開始される場合、即座には確定しない
- `CROSS_PAD_STABILIZE_MS`（デフォルト 50ms）の間、`finger_count` が変化しないことを確認
- 安定待ち中はフレームを飲み込み（`return true`）、通常処理もジェスチャーも実行しない
- fc が変化するとタイマーをリスタートし、安定するまで待つ
- 安定後に確定した fc で `resolve()` を評価し、ジェスチャーを開始する
- これにより、2本指を順番に下ろす際の過渡状態（一瞬の1F）で誤ったジェスチャーが発動するのを防ぐ
- ZMK の hold-tap behavior の「即座に決定せずイベントをバッファリングする」設計を参考にした
- 片方だけの操作（`peer_fc == 0`）では安定化待ちに入らないため、通常操作への影響はない

**ジェスチャーによって状態管理方式が異なる**:

- **ピンチ**: ステートレス。毎フレーム `resolve()` で判定。片側が離れると即終了
- **プレス＆ホールド**: ステートフル。`resolve()` で開始後、`MAX(local_fc, peer_fc) == 2`
  が維持される限り継続。片側（1F側）が離れてもホールドが継続する

**ジェスチャー判定関数 (ディスパッチテーブル):**
```c
enum cross_pad_gesture {
    CROSS_PAD_GESTURE_NONE = 0,
    CROSS_PAD_GESTURE_PINCH,
    CROSS_PAD_GESTURE_PRESS_HOLD,
};

static enum cross_pad_gesture iqs9151_cross_pad_resolve(uint8_t local_fc,
                                                         uint8_t peer_fc) {
    if (local_fc == 0U || peer_fc == 0U) {
        return CROSS_PAD_GESTURE_NONE;
    }
    switch (MAX(local_fc, peer_fc)) {
    case 1:  return CROSS_PAD_GESTURE_PINCH;       /* both 1F → pinch */
    case 2:  return CROSS_PAD_GESTURE_PRESS_HOLD;  /* either 2F → press & hold */
    case 3:  return CROSS_PAD_GESTURE_NONE;         /* either 3F → reserved */
    default: return CROSS_PAD_GESTURE_NONE;
    }
}
```

**ジェスチャー割り当ての変更方法**: `iqs9151_cross_pad_resolve()` 内の switch 文を
編集するだけで、各指数の組み合わせに異なるジェスチャーを割り当てられる。
Kconfig での設定変更は不要。

**遷移検出**:

ピンチ: `cross_pad_ctrl_pressed` フラグで検出
- `pinch_now = true` かつ `ctrl_pressed = false` → **開始**: modifier press, cleanup
- `pinch_now = false` かつ `ctrl_pressed = true` → **終了**: modifier release
- `pinch_now = true` かつ `ctrl_pressed = true` → **継続**: pinch_calc / send_rel_x

プレス＆ホールド: `cross_pad_hold_active` フラグで検出
- `hold_now = true` かつ `hold_active = false` → **開始**: BTN_0 press, cleanup
- `hold_now = false` かつ `hold_active = true` → **終了**: BTN_0 release
- `hold_now = true` かつ `hold_active = true` → **継続**: カーソル移動

**プレス＆ホールドの `hold_now` の決定**:
```c
const bool hold_now_resolve = (gesture == CROSS_PAD_GESTURE_PRESS_HOLD);
const bool hold_continuing = data->cross_pad_hold_active && (max_fc == 2U);
const bool hold_now = hold_now_resolve || hold_continuing;
```
`resolve()` が `PRESS_HOLD` を返さなくても（1F側が離れて `peer_fc == 0`）、
`hold_active` が true で `MAX == 2` なら継続する。

### 双方向タッチ状態通知

各側のドライバは `finger_count` が変化した時のみ相手に通知する。

| 方向 | 方式 | 内容 |
|------|------|------|
| peripheral → central | `EV_MSC(MSC_CROSS_PAD_TOUCH, fc)` | input event 転送 |
| central → peripheral | `INVOKE_BEHAVIOR("pdt", param1=fc)` | BLE コマンド |

通信の詳細は [通信設計](cross_pad_communication.md) を参照。

## ジェスチャーの組み合わせ空間

### 現在の実装

`iqs9151_cross_pad_resolve()` の switch 文により、`MAX(local_fc, peer_fc)` の値ごとに
ジェスチャーが割り当てられる:

| 左 | 右 | max | ジェスチャー |
|:--:|:--:|:---:|------------|
| 1 | 1 | 1 | ピンチイン・アウト (ズーム) |
| 1 | 2 | 2 | プレス＆ホールド (ドラッグ) |
| 2 | 1 | 2 | プレス＆ホールド (ドラッグ) |
| 2 | 2 | 2 | プレス＆ホールド (ドラッグ) |
| 1 | 3 | 3 | なし（予約: 将来ジェスチャー用） |
| 3 | 1 | 3 | なし（予約: 将来ジェスチャー用） |
| 3 | 3 | 3 | なし（予約: 将来ジェスチャー用） |

**3本指の組み合わせ**は意図的に除外されている。
3本指タップ等の通常操作と干渉しないようにするため。

### 将来の拡張候補

| max | 候補ジェスチャー | 状態 |
|:---:|-----------------|------|
| 1 | ピンチイン・アウト (ズーム) | ✅ 実装済み |
| 2 | プレス＆ホールド (ドラッグ) | ✅ 実装済み |
| 3 | 未定 | 予約（未実装） |

割り当ての変更は `iqs9151_cross_pad_resolve()` の switch 文を編集するだけで可能。

## ジェスチャー概要

### ピンチイン・アウト
修飾キー（デフォルト: Ctrl）+ REL_WHEEL でズーム。
セントロイドの X 軸方向の動きを Wheel に変換。
片側のタッチが離れると即座に終了。

### プレス＆ホールド
BTN_0 (左クリック) を押しっぱなしにし、カーソル移動でドラッグ＆ドロップや範囲選択。
`MAX(local_fc, peer_fc) == 2` が維持される限り継続。
1F側を一旦離して再タッチしてもホールドが継続するため、
カーソル位置の調整が可能。

## 修飾キー・ボタンの送信

ドライバから直接 HID レポートを操作する（キーマップに依存しない）。

**ピンチの修飾キー**: `CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER` で選択:
- `0` = なし（REL_WHEEL のみ）
- `1` = Left Ctrl（デフォルト。Ctrl+Wheel ズーム）
- `2` = Mouse Button 4（macOS の smart zoom ユーティリティ向け）

**プレス＆ホールドのボタン**: BTN_0 (左クリック) 固定。
`zmk_hid_mouse_button_press(0)` / `zmk_hid_mouse_button_release(0)` で直接操作。

**central 側のみで実行** — HID レポートは central が生成するため。
詳細は [実装設計](cross_pad_gesture_implementation.md) を参照。

## 1台構成での安全性

- `CONFIG_INPUT_IQS9151_CROSS_PAD=n` (デフォルト): 全コードが `#ifdef` で除外。バイナリへの影響ゼロ
- `CONFIG_INPUT_IQS9151_CROSS_PAD=y` + split でない構成: 相手側からのタッチ通知が来ないため
  `peer_fc` は常に 0 → gesture = NONE → 通常動作と同一

## 実装ファイル一覧

| ファイル | 内容 |
|---------|------|
| `drivers/input/iqs9151.c` | クロスパッド関数群、process_frame 分岐、proxy_cb |
| `drivers/input/CMakeLists.txt` | ZMK app include パスの追加 |
| `drivers/input/Kconfig` | `CROSS_PAD`, `CROSS_PAD_SIDE`, `CROSS_PAD_PINCH_GAIN_X10`, `CROSS_PAD_PINCH_MODIFIER` |
| `dts/bindings/input/azoteq,iqs9151.yaml` | `cross-pad-peer-input` phandle プロパティ |
| `behaviors/behavior_pad_touch.c` | `pdt` ビヘイビア (central → peripheral 通信の受信側) |
| `dts/bindings/behaviors/zmk,behavior-pad-touch.yaml` | DT バインディング (`#binding-cells = 2`) |
| `include/iqs9151_cross_pad.h` | 公開 API ヘッダ |
| `include/zmk/cross_pad_gate.h` | Gate API ヘッダ |
| `input_processors/input_processor_cross_pad_gate.c` | Cross-pad gate input processor |
| `input_processors/Kconfig` | Gate Kconfig (`ZMK_INPUT_PROCESSOR_CROSS_PAD_GATE`) |
| `dts/bindings/input_processors/zmk,input-processor-cross-pad-gate.yaml` | Gate DT バインディング |

## ペリフェラル側の応答性改善

### 問題

ペリフェラル（左側）の指の動きによるカーソル移動やピンチが、
セントラル（右側）と比べてスムーズさや移動量が異なっていた。
特にプレス＆ホールド（カーソル移動）で顕著。

### 原因

1. **データ上書きロス**: ペリフェラルから BLE 経由で届く `peer_rel_x`/`peer_rel_y` を
   `=` で上書きしていたため、セントラルのローカルフレーム間に複数の MSC イベントが
   届くと中間データが消失していた
2. **処理タイミングの遅延**: ペリフェラルの移動データが届いても、セントラルの次の
   ローカルフレーム処理まで消費されなかった

### 対策（実装済み）

1. **累積代入** (`+=`): `set_peer_rel_x`/`set_peer_rel_y` を上書き (`=`) から
   累積 (`+=`) に変更。複数の BLE イベントが到着してもデータが失われない
2. **即時フラッシュ** (`flush_peer`): ペリフェラルの MSC データが `proxy_cb` に
   到着した時点で即座にピンチ計算 / カーソル移動を実行。ローカルフレーム待ちが不要に

### 残課題: BLE ラウンドトリップ遅延による通常処理漏れ

セントラルがクロスパッド開始を検知してからペリフェラルに通知が届くまでの
BLE ラウンドトリップ（数十 ms）の間、ペリフェラルは `peer_fc = 0` のままであり、
`cross_pad_handle` が `false` を返して通常カーソル処理が走る。

この漏れた通常処理により:
- ペリフェラル側で慣性追跡 (inertia EMA) が数フレーム走る
- 通常の REL イベントが split transport 経由でセントラルに届く
- セントラルではクロスパッド中にもかかわらず、通常カーソル移動が発生する

**影響**: プレス＆ホールドでカーソル移動に慣性が合算されたような挙動になる。
ピンチ（wheel イベント）では視覚的にほぼ気にならない。

### 対策（実装済み）: `cross-pad-gate` input-processor

セントラル側で、ペリフェラル proxy device からの REL イベントを抑制する
カスタム input-processor を導入した。

**設計方針:**

- セントラルはクロスパッド開始を**遅延なしで検知**できる（自分の fc を即座に知っている）
- ペリフェラル側の変更や通常操作への遅延追加は不要

**構成要素:**

1. **input-processor**: `zmk,input-processor-cross-pad-gate`
   - 共有フラグ（atomic）を持つ
   - フラグ ON かつ `INPUT_EV_REL` → `ZMK_INPUT_PROC_STOP`（イベント破棄）
   - フラグ OFF または `INPUT_EV_REL` 以外 → `ZMK_INPUT_PROC_CONTINUE`（通過）

2. **IQS9151 ドライバとの連携**: クロスパッド開始時にフラグ ON、終了時に OFF

3. **DTS 設定**: ペリフェラル proxy の input-listener の**全ての** input-processor チェインの
   先頭に挿入する（デフォルト・レイヤーオーバーライドを含む全チェイン）
   ```dts
   &trackpad_listener_L {
       input-processors = <&cross_pad_gate>, <&existing_processors ...>;
       some_layer_override {
           input-processors = <&cross_pad_gate>, <&existing_processors ...>;
       };
   };
   ```

**ZMK input-processor パイプラインの特性:**

- `ZMK_INPUT_PROC_STOP` を返すと、後続の processor も含め処理が中断され、
  HID レポートが一切生成されない
- processor チェインの順序は DTS で定義され、**決定的**（リンカ順序に依存しない）
- `cross_pad_proxy_cb`（MSC 受信）は `INPUT_CALLBACK_DEFINE` 経由であり、
  input-processor パイプラインとは独立して動作するため影響を受けない

**補足: gate で解決できない BLE 転送特性**

gate はジェスチャー開始時の通常処理漏れを防ぐが、ホールド中のペリフェラル側
カーソル移動の品質差（BLE 接続間隔によるバースト到着、パイプライン残留）は
BLE 通信の本質的な特性であり、gate の対象外。
この影響は累積代入 (`+=`) と即時フラッシュ (`flush_peer`) で緩和済みであり、
実使用上は問題にならないレベルまで改善されている。

**検討したが採用しなかった案:**

| 案 | 不採用理由 |
|----|-----------|
| ペリフェラル側で通常処理を遅延 | 片側だけの通常操作にも遅延が生じる |
| `INPUT_CALLBACK_DEFINE` で `evt->value = 0` に書き換え | コールバック順序がリンカ依存で制御不可 |
| ZMK レイヤー切り替えで processor チェインを変更 | レイヤー活性化の副作用、複雑性 |

## 既知の課題・制限事項

### トラックパッド端でのタッチ検知の不安定性

IQS9151 のタッチセンサーは、トラックパッドの端付近で指の接触面積が
小さくなると、以下の問題が発生することがある:

- タッチが検出されたり消えたりする（閾値付近での振動）
- `finger_count` が 1 ↔ 2 で揺れる

これはハードウェア（センサー）の特性に起因するため、ソフトウェアでの
完全な解決は困難。根本的な改善には、ドライバのセンシング処理で
トラックパッド端付近の座標を特別扱いする処理の追加が必要。

現状、プレス＆ホールドの途切れなど顕著な問題は報告されていないが、
将来的に問題が顕在化した場合はデバウンスやヒステリシスの導入を検討する。
