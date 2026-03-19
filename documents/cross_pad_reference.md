# Cross-Pad Gesture: リファレンス

データ構造、Kconfig、設計判断の一覧。

[メイン設計ドキュメント](cross_pad_gesture_design.md) に戻る。

## データ構造

### iqs9151_data に追加されるフィールド (フラット構造)

設計段階では `iqs9151_cross_pad_state` 構造体を検討したが、
実装では**最小限のフラットフィールド**を `iqs9151_data` に直接追加する方式を採用した。

```c
struct iqs9151_data {
    /* ... 既存フィールド ... */

#ifdef CONFIG_INPUT_IQS9151_CROSS_PAD
    uint8_t cross_pad_peer_finger_count;  /* 相手側の指本数 (通信経由で更新) */
    int16_t cross_pad_peer_rel_x;         /* peer から受信した rel_x (central 側のみ使用) */
    int16_t cross_pad_peer_rel_y;         /* peer から受信した rel_y (central 側のみ使用) */
    int32_t cross_pad_pinch_remainder;    /* ピンチのスケーリング剰余 */
    bool cross_pad_ctrl_pressed;          /* ピンチ修飾キーが押されているか (遷移検出用) */
    bool cross_pad_hold_active;           /* プレス＆ホールド: BTN_0 が押されているか */
    int32_t cross_pad_centroid_x;         /* 前回セントロイド X (デルタ算出用) */
    int32_t cross_pad_centroid_y;         /* 前回セントロイド Y (デルタ算出用) */
    bool cross_pad_centroid_valid;        /* true: 前回セントロイドが有効 */
#endif
};
```

**設計判断: なぜ構造体やセッション状態を持たないか**

- 毎フレーム `local_fc` と `peer_fc` から `iqs9151_cross_pad_resolve()` でジェスチャーを導出すれば十分
- プレス＆ホールドのステートフルな継続は `hold_active` + `MAX == 2` で管理
- `session_active` / `gesture_active` 等は不要
- フラット構造により、コードが単純になり、状態の不整合が起きにくい

### ジェスチャー判定関数 (ディスパッチテーブル)

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

- 両側にタッチがあり、`MAX == 1` → ピンチ、`MAX == 2` → プレス＆ホールド
- 3本指以上は除外（通常の3F操作と干渉しないため）
- ジェスチャー割り当ての変更は switch 文を編集するだけで可能

## Kconfig

```kconfig
config INPUT_IQS9151_CROSS_PAD
    bool "Cross-pad gesture support"
    default n

if INPUT_IQS9151_CROSS_PAD

choice INPUT_IQS9151_CROSS_PAD_SIDE
    prompt "Trackpad side"

config INPUT_IQS9151_CROSS_PAD_SIDE_LEFT
    bool "Left"

config INPUT_IQS9151_CROSS_PAD_SIDE_RIGHT
    bool "Right"

endchoice

config INPUT_IQS9151_CROSS_PAD_PINCH_GAIN_X10
    int "Cross-pad pinch wheel gain (x10)"
    range 1 100
    default 40

config INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER
    int "Cross-pad pinch modifier (0=none, 1=Left Ctrl, 2=MB4)"
    range 0 2
    default 1

endif # INPUT_IQS9151_CROSS_PAD
```

### side の決定

Kconfig の choice からコンパイル時に符号反転方向を決定する:

```c
#if defined(CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_LEFT)
#define CROSS_PAD_LOCAL_SIGN  (-1)
#define CROSS_PAD_PEER_SIGN   (1)
#else
#define CROSS_PAD_LOCAL_SIGN   (1)
#define CROSS_PAD_PEER_SIGN  (-1)
#endif
```

## 定数定義

```c
#define INPUT_MSC_CROSS_PAD_TOUCH  0x06  /* finger_count の通知 */
#define INPUT_MSC_CROSS_PAD_SPREAD 0x07  /* ピンチ/ホールド中の rel_x 通知 */
#define INPUT_MSC_CROSS_PAD_REL_Y  0x08  /* ホールド中の rel_y 通知 */
#define CROSS_PAD_PINCH_WHEEL_DIV  12
```

## 公開 API

```c
/* include/iqs9151_cross_pad.h */
void iqs9151_set_peer_state(const struct device *dev, uint8_t finger_count);
void iqs9151_set_peer_rel_x(const struct device *dev, int16_t rel_x);
void iqs9151_set_peer_rel_y(const struct device *dev, int16_t rel_y);
```

## 設計判断一覧

### 解決済み

| 項目 | 決定 | 理由 |
|------|------|------|
| 状態管理方式 | ディスパッチテーブル + ジェスチャー別の継続条件 | ピンチはステートレス（毎フレーム導出）、プレス＆ホールドはステートフル（`hold_active` + `MAX == 2`） |
| ジェスチャー割り当て | max=1 → ピンチ、max=2 → プレス＆ホールド、max=3 → 予約 | ディスパッチテーブル (`switch` 文) で管理。変更はコード編集のみ |
| 移動量の算出 | セントロイドデルタ方式 | `frame->rel_x` ではなく、絶対座標からセントロイドのフレーム間差分を算出。通常の2Fスクロールと同じ方式で実績あり。指数によらず統一的に処理可能 |
| ピンチの合算方式 | central 集約 | peripheral は rel_x を EV_MSC で送信、central が合算・出力 |
| ホールドのカーソル移動 | central 集約 | peripheral は rel_x/y を EV_MSC で送信、central がローカルのデルタと合算して REL_X/Y を出力 |
| 符号反転方式 | LEFT 側を常に反転 (Kconfig) | 動的反転は誤動作のリスクあり |
| 同方向の動きの処理 | 同符号のフレームはスキップ | ガクつき防止 |
| peripheral → central 通信 | EV_MSC (input event) | ZMK 本体の変更不要 |
| central → peripheral 通信 | INVOKE_BEHAVIOR (`pdt`) | API に制限なし。ドライバから直接呼び出し可能 |
| 修飾キー/ボタン送信 | HID レポート直接操作 | `zmk_hid_register_mod()` / `zmk_hid_mouse_button_press()` を直接呼び出し。キーマップに依存しない |
| ピンチの修飾キー | `CROSS_PAD_PINCH_MODIFIER` で選択 (0=None, 1=LCtrl, 2=MB4) | 単一の int 設定値。choice ブロックではなく int 型にして簡潔に |
| ホールドのボタン | BTN_0 (左クリック) 固定 | `zmk_hid_mouse_button_press(0)` で直接操作 |
| グレースピリオド | 削除済み | 当初200msの抑制を実装したが、通常2Fスクロールの体感を悪化させたため削除。`cleanup_normal` による遷移時リセットで十分 |
| DT マクロ | `DT_INST(0, azoteq_iqs9151)` | 2引数形式の `DT_INST(inst, compat)` は Zephyr の正式マクロ。`DT_DRV_COMPAT` に依存しないため behavior ファイルからも使用可能 |
| behavior_dev サイズ | 16 バイト | `"pdt"` は余裕で収まる |
| peripheral の source ID | 2分割キーボードでは常に `source=0` | |
| INVOKE_BEHAVIOR の state | 常に `state=true`、`param1` に finger_count | pressed/released の対称性の問題を回避 |
| EV_MSC の識別 | 2段階フィルタ (proxy device + type/code) | 他のドライバと干渉しない |
| MSC コード値 | `0x06`, `0x07`, `0x08` | 0x05 以下は Linux/Zephyr で使用済み。0x06 以上は未使用 |
| set_peer_state でのリリース | peer 更新時にもジェスチャー終了を評価 | ローカル側のフレーム処理が走っていない場合でも確実にリリース |
| 再タッチ時のジャンプ防止 | `cross_pad_centroid_valid = false` | finger_count 変化時、ジェスチャー開始/終了時に無効化 |

### 実機テスト結果

| 項目 | 結果 |
|------|------|
| EV_MSC の BLE 転送 | ✅ 動作確認済み。peripheral → central 方向で正常に転送される |
| INVOKE_BEHAVIOR | ✅ 動作確認済み。central → peripheral 方向で `pdt` behavior が正常に呼ばれる |
| ピンチの体感 | ✅ Apple Maps でズーム動作を確認。両側からの操作可能 |
| プレス＆ホールド | ✅ ドラッグ操作を確認。1F側の再タッチでドラッグ継続 |
| peer 側のカーソル移動 (ホールド) | ✅ peripheral の移動データが central に転送されカーソル移動に反映 |
| ジェスチャー終了タイミング | ✅ 指を離した時点で即リリース（set_peer_state でのリリース追加後） |
| 3本指の除外 | ✅ 3本指タッチ時に Ctrl / BTN_0 が送信されないことを確認 |

### 未決定・将来検討

| 項目 | 状態 |
|------|------|
| 回転ジェスチャー (ROTATE) | 実現方法の調査が必要 |
| max=3 の割り当て | 未定（現在は NONE） |
| `iqs9151_stable_finger_count` デバウンス | 初期実装では省略。不安定な場合に追加を検討 |
| ジェスチャー割り当てのカスタマイズ | `iqs9151_cross_pad_resolve()` の switch 文で管理。Kconfig 化は不要と判断 |
| `pdt` behavior 名 | `padtouch` への変更を試みたが不具合が発生。原因未調査。現状 `pdt` のまま |
