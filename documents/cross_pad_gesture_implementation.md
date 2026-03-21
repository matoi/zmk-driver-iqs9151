# Cross-Pad Gesture: 実装設計

> **⚠️ PoC (Proof of Concept)** — 本家ドライバには含まれていない独自拡張です。

ピンチイン・アウトおよびプレス＆ホールドの実装詳細。

[メイン設計ドキュメント](cross_pad_gesture_design.md) に戻る。

## 移動量の算出: セントロイドデルタ方式

ピンチ・プレス＆ホールドの両ジェスチャーで共通して使用する、
指の移動量をセントロイド（重心）の差分から算出する関数:

```c
static void iqs9151_cross_pad_effective_rel(struct iqs9151_data *data,
                                             const struct iqs9151_frame *frame,
                                             int16_t *out_dx, int16_t *out_dy) {
    // finger_count == 1: centroid = finger1_x/y
    // finger_count >= 2: centroid = (finger1 + finger2) / 2
    // delta = current centroid - previous centroid
}
```

- `frame->rel_x/y` の代わりにセントロイドデルタを使用
- 通常の2Fスクロールと同じ方式であり、実績で品質が証明されている
- 指数によらず統一的な処理が可能
- `cross_pad_centroid_valid` フラグにより、再タッチ時のジャンプを防止

## ピンチイン・アウトの実装

### ジェスチャー判定

`iqs9151_cross_pad_resolve()` が `CROSS_PAD_GESTURE_PINCH` を返した場合に発動。
**ステートレス**: 毎フレーム `resolve()` で判定し、条件を満たさなくなれば即終了。

### 方向判定と符号反転

```
            ← 内側    外側 →
  ┌──────────┐              ┌──────────┐
  │   LEFT   │              │  RIGHT   │
  │  パッド   │              │  パッド   │
  └──────────┘              └──────────┘
     rel_x < 0 = 外側(拡大)    rel_x > 0 = 外側(拡大)
     rel_x > 0 = 内側(縮小)    rel_x < 0 = 内側(縮小)
```

LEFT 側の rel_x を反転することで、「外側への動き」を常に正の spread に変換する:

```c
#if defined(CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_LEFT)
#define CROSS_PAD_LOCAL_SIGN  (-1)
#define CROSS_PAD_PEER_SIGN   (1)
#else
#define CROSS_PAD_LOCAL_SIGN   (1)
#define CROSS_PAD_PEER_SIGN  (-1)
#endif
```

### central 集約方式

peripheral は `rel_x` を EV_MSC で central に送信し、
central が両側の `rel_x` を合算して単一の `REL_WHEEL` を出力する。

```
peripheral 側:
  ピンチ中 → dx (セントロイドデルタ X) を EV_MSC (MSC_CROSS_PAD_SPREAD) で central に送信
           → local 側では REL_WHEEL を出力しない

central 側:
  local の dx + peer の dx (EV_MSC で受信) → 同符号フィルタ → REL_WHEEL 出力
```

### 変換ロジック (central 側)

```c
static void iqs9151_cross_pad_pinch_calc(struct iqs9151_data *data,
                                          int16_t local_rx) {
    int16_t peer_rx = data->cross_pad_peer_rel_x;

    /* 同符号チェック: 両手が同方向 → スキップ (ガクつき防止) */
    if ((local_rx > 0 && peer_rx > 0) || (local_rx < 0 && peer_rx < 0)) {
        return;
    }

    int32_t local_spread = (int32_t)local_rx * CROSS_PAD_LOCAL_SIGN;
    int32_t peer_spread = (int32_t)peer_rx * CROSS_PAD_PEER_SIGN;
    int32_t total_spread = local_spread + peer_spread;

    int32_t wheel_raw = total_spread * CROSS_PAD_PINCH_WHEEL_GAIN_X10
                        / CROSS_PAD_PINCH_WHEEL_GAIN_DEN;

    wheel_raw += data->cross_pad_pinch_remainder;
    int16_t wheel_out = (int16_t)(wheel_raw / CROSS_PAD_PINCH_WHEEL_DIV);
    data->cross_pad_pinch_remainder =
        wheel_raw - ((int32_t)wheel_out * CROSS_PAD_PINCH_WHEEL_DIV);

    if (wheel_out != 0) {
        iqs9151_report_rel_event(data->dev, INPUT_REL_WHEEL,
                                  wheel_out, true, K_NO_WAIT);
    }
    data->cross_pad_peer_rel_x = 0;
}
```

### peripheral 側のピンチ処理

```c
static void iqs9151_cross_pad_send_rel_x(struct iqs9151_data *data,
                                           int16_t rel_x) {
    if (rel_x == 0) {
        return;
    }
    input_report(data->dev, INPUT_EV_MSC, INPUT_MSC_CROSS_PAD_SPREAD,
                 rel_x, true, K_NO_WAIT);
}
```

## プレス＆ホールドの実装

### ジェスチャー判定

`iqs9151_cross_pad_resolve()` が `CROSS_PAD_GESTURE_PRESS_HOLD` を返した場合に発動。
**ステートフル**: 開始後、`MAX(local_fc, peer_fc) == 2` が維持される限り継続。

```c
const bool hold_now_resolve = (gesture == CROSS_PAD_GESTURE_PRESS_HOLD);
const bool hold_continuing = data->cross_pad_hold_active && (max_fc == 2U);
const bool hold_now = hold_now_resolve || hold_continuing;
```

1F 側が離れても(`peer_fc == 0`)、2F 側が維持されていれば `MAX == 2` → 継続。
再タッチすればドラッグが再開される（カーソル位置の調整が可能）。

### 開始・終了条件

| 条件 | 動作 |
|------|------|
| `resolve() == PRESS_HOLD` | 開始: BTN_0 press, cleanup_normal |
| `MAX == 2` かつ `hold_active` | 継続: カーソル移動 |
| `MAX != 2` かつ `hold_active` | 終了: BTN_0 release |

終了トリガー:
- 2F 側が離れた or 1F に減った → MAX < 2 → 終了
- 3F に変わった → MAX == 3 → 終了
- 両側離れた → MAX == 0 → 終了

### カーソル移動 (central 側)

ホールド中、通常処理はスキップ (`return true`) し、
セントロイドデルタを自力で `REL_X` / `REL_Y` として出力する。
peripheral からの peer デルタも加算する:

```c
if (hold_now) {
    int16_t dx, dy;
    iqs9151_cross_pad_effective_rel(data, frame, &dx, &dy);

    /* Add peer's movement delta */
    dx += data->cross_pad_peer_rel_x;
    dy += data->cross_pad_peer_rel_y;
    data->cross_pad_peer_rel_x = 0;
    data->cross_pad_peer_rel_y = 0;

    if (dx != 0) {
        iqs9151_report_rel_event(data->dev, INPUT_REL_X, dx, ...);
    }
    if (dy != 0) {
        iqs9151_report_rel_event(data->dev, INPUT_REL_Y, dy, ...);
    }
    return true;
}
```

### カーソル移動 (peripheral 側)

peripheral は X と Y の両方を central に送信:

```c
static void iqs9151_cross_pad_send_rel_xy(struct iqs9151_data *data,
                                            int16_t rel_x, int16_t rel_y) {
    if (rel_x != 0) {
        input_report(data->dev, INPUT_EV_MSC, INPUT_MSC_CROSS_PAD_SPREAD,
                     rel_x, true, K_NO_WAIT);
    }
    if (rel_y != 0) {
        input_report(data->dev, INPUT_EV_MSC, INPUT_MSC_CROSS_PAD_REL_Y,
                     rel_y, true, K_NO_WAIT);
    }
}
```

### ピンチとの違い

| | ピンチ | プレス＆ホールド |
|---|--------|-----------------|
| 状態管理 | ステートレス | ステートフル (`hold_active` フラグ) |
| 開始条件 | `resolve() == PINCH` | `resolve() == PRESS_HOLD` |
| 継続条件 | `resolve() == PINCH` | `MAX(local_fc, peer_fc) == 2` |
| 終了条件 | `resolve() != PINCH` | `MAX(local_fc, peer_fc) != 2` |
| 1F 側が離れた時 | 即終了 | 継続（2F 側が維持されていれば） |
| HID 出力 | modifier + REL_WHEEL | BTN_0 + REL_X/Y |
| 通常処理 | スキップ | スキップ |
| peer データ | X のみ (`MSC_CROSS_PAD_SPREAD`) | X + Y (`SPREAD` + `REL_Y`) |

## 修飾キー (ピンチ→ズーム)

### 実装方式: HID レポート直接操作

ピンチ→ズームには修飾キー + Wheel が必要。
ドライバから直接 HID レポートを操作する（キーマップに依存しない）。

**修飾キーの種類**: `CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER` で選択:

```c
#define CROSS_PAD_MOD_NONE  0
#define CROSS_PAD_MOD_LCTRL 1  /* デフォルト */
#define CROSS_PAD_MOD_MB4   2
```

**press/release 関数**:

```c
static void iqs9151_cross_pad_modifier_press(void) {
#if CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER == CROSS_PAD_MOD_LCTRL
    zmk_hid_register_mod(0x00); /* bit-position 0 = Left Ctrl */
    zmk_endpoints_send_report(HID_USAGE_KEY);
#elif CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER == CROSS_PAD_MOD_MB4
    zmk_hid_mouse_button_press(3); /* MB4 = button index 3 */
    zmk_endpoints_send_mouse_report();
#endif
}
```

**プレス＆ホールドの BTN_0**:

```c
static void iqs9151_cross_pad_hold_press(void) {
    zmk_hid_mouse_button_press(0); /* BTN_0 = left click */
    zmk_endpoints_send_mouse_report();
}

static void iqs9151_cross_pad_hold_release(void) {
    zmk_hid_mouse_button_release(0);
    zmk_endpoints_send_mouse_report();
}
```

**central 側でのみ実行**する（HID レポートは central が生成するため）。

**注意: `zmk_hid_register_mod()` の引数はビット位置**:
- `0` = Left Ctrl, `1` = Left Shift, `2` = Left Alt, `3` = Left GUI
- `MOD_LCTL = 0x01` はビットフラグ (`1 << 0`) であり、ビット位置ではない。混同注意

**設計上の注意:**
- 初期設計では `INPUT_BTN_7` → input processor → keymap (`&kp LCTRL`) の経路を
  再利用していたが、キーマップ設定に暗黙的に依存する問題があったため、
  ドライバから直接 HID レポートを操作する方式に変更した
- これにより、修飾キー/ボタンの種類をキーマップに関係なく切り替えられる

## set_peer_state でのジェスチャー終了

BLE 経由で peer の fc が更新された際、local 側でフレーム処理が
走っていない場合でもジェスチャーを適切に終了する必要がある。

`iqs9151_set_peer_state()` 内で、ホールド・ピンチの終了条件を評価する:

```c
void iqs9151_set_peer_state(const struct device *dev, uint8_t finger_count) {
    // ...
    if (data->cross_pad_hold_active && max_fc != 2U) {
        iqs9151_cross_pad_hold_release();
        // ... reset state ...
    }
    if (data->cross_pad_ctrl_pressed) {
        if (finger_count == 0U || local_fc == 0U) {
            iqs9151_cross_pad_modifier_release();
            // ... reset state ...
        }
    }
}
```

これにより、local 側の指が離れた後に peer の状態が変わった場合でも、
ボタン/修飾キーが正しくリリースされる。

## process_frame での分岐

```c
#ifdef CONFIG_INPUT_IQS9151_CROSS_PAD
    /* タッチ状態変化を peer に通知 */
    iqs9151_cross_pad_notify_touch(data, frame);

    /* クロスパッド判定: true なら通常処理をスキップ */
    if (iqs9151_cross_pad_handle(data, frame)) {
        iqs9151_update_prev_frame(data, frame, &prev_frame);
        iqs9151_push_finger_history(data, frame->finger_count, now_ms);
        return;
    }
#endif

    /* === 以降: 既存の通常処理 === */
```

## 変更対象ファイル

| ファイル | 変更内容 |
|---------|---------|
| `drivers/input/iqs9151.c` | クロスパッド関数群、process_frame 分岐、proxy_cb |
| `drivers/input/CMakeLists.txt` | ZMK app の include パス追加 (`zmk/split/central.h` 等の参照に必要) |
| `drivers/input/Kconfig` | `CROSS_PAD`, `CROSS_PAD_SIDE`, `CROSS_PAD_PINCH_GAIN_X10`, `CROSS_PAD_PINCH_MODIFIER` |
| `dts/bindings/input/azoteq,iqs9151.yaml` | `cross-pad-peer-input` phandle プロパティ |
| `behaviors/behavior_pad_touch.c` | `pdt` ビヘイビア |
| `dts/bindings/behaviors/zmk,behavior-pad-touch.yaml` | DT バインディング (`#binding-cells = 2`) |
| `include/iqs9151_cross_pad.h` | 公開 API ヘッダ |
