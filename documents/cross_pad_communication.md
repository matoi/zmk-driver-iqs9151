# Cross-Pad Gesture: 通信設計

> **⚠️ PoC (Proof of Concept)** — 本家ドライバには含まれていない独自拡張です。

クロスパッドジェスチャーにおける双方向タッチ状態通知の通信設計。

[メイン設計ドキュメント](cross_pad_gesture_design.md) に戻る。

## 双方向タッチ状態通知

各側のドライバは、peer のタッチ状態 (fc) を知る必要がある。

**通知の目的**: 各ドライバが「local パッドの fc が変わった」ことを peer に伝える。
これはクロスパッドジェスチャーの判定材料を peer に提供するだけであり、
通知自体がジェスチャーを発火させたり、通常動作を変更したりすることはない。

### 具体的なシナリオ

```
例: 左パッドが peripheral、右パッドが central の場合

t0: 左パッドに指を2本置く
    → 左ドライバ: fc が 0→2 に変化
    → 左ドライバ: input_report(EV_MSC, MSC_CROSS_PAD_TOUCH, 2) で central に通知
    → BLE split transport → central の proxy device → INPUT_CALLBACK_DEFINE で受信
    → central: cross_pad_peer_finger_count = 2

t1: 右パッドに指を1本置く
    → 右ドライバ: fc=1, prev_fc=0 → notify_touch で INVOKE_BEHAVIOR("pdt", param1=1)
    → BLE → peripheral の pdt behavior → iqs9151_set_peer_state(dev, 1)
    → 右ドライバ: resolve(1, 2) = PRESS_HOLD → プレス＆ホールド開始
    → BTN_0 press (HID 直接操作)

t2: 両パッドの指を動かす
    → peripheral (左): ホールド中 → send_rel_xy → EV_MSC(SPREAD, dx) + EV_MSC(REL_Y, dy)
    → central (右): local dx/dy + peer dx/dy → REL_X/Y 出力 (カーソル移動)

t3: 左パッドから指を離す
    → 左ドライバ: fc 2→0 → EV_MSC(TOUCH, 0) → central: peer_fc=0
    → central: MAX(1, 0) = 1 ≠ 2 → ホールド終了 → BTN_0 release
```

### 通知の仕組み

ZMK split keyboard の通信は本質的に非対称であり、
方向ごとに異なる仕組みを使い分ける。

## peripheral → central: EV_MSC による input event

peripheral 側のドライバが fc 変化時に EV_MSC イベントを送出する。

```c
/* peripheral 側: notify_touch 内 */
#elif IS_ENABLED(CONFIG_ZMK_SPLIT)
    input_report(data->dev, INPUT_EV_MSC, INPUT_MSC_CROSS_PAD_TOUCH,
                 frame->finger_count, true, K_NO_WAIT);
#endif
```

**転送経路:**
```
peripheral: input_report(dev, EV_MSC, ...)
  → split_input_handler (input_split.c) が捕捉
  → BLE GATT notify で送信

central: peripheral_input_event_notify_cb()
  → peripheral_event_msgq にキューイング
  → input_report(proxy_dev, EV_MSC, ...) で再生
  → INPUT_CALLBACK_DEFINE で iqs9151_cross_pad_proxy_cb が受信
```

**type フィールドにフィルタリングは一切ない** — `EV_MSC` も `EV_KEY`/`EV_REL` と
まったく同じ経路でそのまま転送される。

### ピンチ/ホールド中の移動データ送信 (peripheral → central)

ピンチ中は `MSC_CROSS_PAD_SPREAD` で rel_x を送信:
```c
input_report(data->dev, INPUT_EV_MSC, INPUT_MSC_CROSS_PAD_SPREAD,
             dx, true, K_NO_WAIT);
```

ホールド中は `MSC_CROSS_PAD_SPREAD` で rel_x、`MSC_CROSS_PAD_REL_Y` で rel_y を送信:
```c
if (rel_x != 0) {
    input_report(data->dev, INPUT_EV_MSC, INPUT_MSC_CROSS_PAD_SPREAD,
                 rel_x, true, K_NO_WAIT);
}
if (rel_y != 0) {
    input_report(data->dev, INPUT_EV_MSC, INPUT_MSC_CROSS_PAD_REL_Y,
                 rel_y, true, K_NO_WAIT);
}
```

### central 側の受信

```c
static void iqs9151_cross_pad_proxy_cb(struct input_event *evt) {
    if (evt->type != INPUT_EV_MSC) {
        return;
    }
    const struct device *local_dev = DEVICE_DT_GET(DT_DRV_INST(0));
    switch (evt->code) {
    case INPUT_MSC_CROSS_PAD_TOUCH:
        iqs9151_set_peer_state(local_dev, (uint8_t)evt->value);
        break;
    case INPUT_MSC_CROSS_PAD_SPREAD:
        iqs9151_set_peer_rel_x(local_dev, (int16_t)evt->value);
        break;
    case INPUT_MSC_CROSS_PAD_REL_Y:
        iqs9151_set_peer_rel_y(local_dev, (int16_t)evt->value);
        break;
    }
}

INPUT_CALLBACK_DEFINE(
    DEVICE_DT_GET(DT_INST_PHANDLE(0, cross_pad_peer_input)),
    iqs9151_cross_pad_proxy_cb);
```

`cross-pad-peer-input` DT プロパティで proxy device を指定。
central の overlay で設定:
```dts
&iqs9151 {
    cross-pad-peer-input = <&trackpad_split_L>;
};
```

## central → peripheral: INVOKE_BEHAVIOR

central 側のドライバが fc 変化時に
`zmk_split_central_invoke_behavior()` を呼び出し、
peripheral 側でカスタムビヘイビア `pdt` を起動する。

```c
/* central 側: notify_touch 内 */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    struct zmk_behavior_binding binding = {
        .behavior_dev = "pdt",
        .param1 = frame->finger_count,
        .param2 = 0,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_split_central_invoke_behavior(0, &binding, event, true);
#endif
```

### peripheral 側の受信 (pdt ビヘイビア)

```c
/* behaviors/behavior_pad_touch.c */
static int on_binding_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    const struct device *iqs_dev =
        DEVICE_DT_GET_OR_NULL(DT_INST(0, azoteq_iqs9151));
    if (iqs_dev == NULL) {
        return -ENODEV;
    }
#ifdef CONFIG_INPUT_IQS9151_CROSS_PAD
    iqs9151_set_peer_state(iqs_dev, (uint8_t)binding->param1);
#endif
    return 0;
}
```

**`DT_INST(0, azoteq_iqs9151)` について:**
Zephyr の `DT_INST(inst, compat)` は2引数形式の正式マクロ。
`DT_DRV_COMPAT` に依存しないため、behavior ファイル
(`DT_DRV_COMPAT = zmk_behavior_pad_touch`) からでも正しく動作する。

### ビヘイビアの登録

- DTS ノード名: `pdt` (16バイトの behavior_dev バッファに収まるよう短くした)
- DTS バインディング: `dts/bindings/behaviors/zmk,behavior-pad-touch.yaml`
- C 実装: `behaviors/behavior_pad_touch.c`
- Kconfig: `ZMK_BEHAVIOR_PAD_TOUCH` (depends on `INPUT_IQS9151_CROSS_PAD`)

```dts
/* lalapadgen2.dtsi */
pdt: pdt {
    compatible = "zmk,behavior-pad-touch";
    #binding-cells = <2>;
};
```

## 方向ごとの通信方式まとめ

```
central 側                                peripheral 側
(右)                                      (左)
┌──────────────────┐                     ┌──────────────────┐
│  iqs9151 driver  │                     │  iqs9151 driver  │
│                  │                     │                  │
│  peer_fc     ◄───┼── EV_MSC(TOUCH) ───┼─ fc              │
│  peer_rel_x  ◄───┼── EV_MSC(SPREAD) ──┼─ dx              │
│  peer_rel_y  ◄───┼── EV_MSC(REL_Y) ──┼─ dy              │
│  (proxy_cb で受信)│                     │  (input_report)  │
│                  │                     │                  │
│  fc           ───┼── INVOKE_BEHAVIOR ──┼─► peer_fc        │
│  (notify_touch)  │   ("pdt")           │  (pdt behavior   │
│                  │                     │   で受信)         │
└──────────────────┘                     └──────────────────┘
```

| 方向 | 方式 | データ | ZMK 変更 | 実機確認 |
|------|------|--------|:--------:|:--------:|
| peripheral → central | EV_MSC input event | fc, rel_x, rel_y | 不要 | ✅ |
| central → peripheral | INVOKE_BEHAVIOR | fc | 不要 | ✅ |

## EV_MSC の安全性

### HID マウスリスナーの EV_MSC 処理

`input_listener.c` は `EV_MSC` を処理しない (`INPUT_EV_MSC` の case なし) → **影響なし**。

### activity.c への影響

全デバイスの全 input event に反応してアクティビティタイマーをリセットするが、
指を触れた/離した瞬間のリセットは実害なし。

### MSC コード値

```c
#define INPUT_MSC_CROSS_PAD_TOUCH  0x06
#define INPUT_MSC_CROSS_PAD_SPREAD 0x07
#define INPUT_MSC_CROSS_PAD_REL_Y  0x08
```

0x05 以下は Linux/Zephyr で使用済み。0x06 以上は未使用。
