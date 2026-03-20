# zmk-driver-iqs9151

## クロスパッドジェスチャー (Cross-Pad Gesture)

左右分割キーボードの両側のトラックパッドを同時にタッチすることで、ピンチズームやドラッグ＆ドロップなどの特殊ジェスチャーを実行する機能です。本家には含まれていない独自拡張の PoC として、別ブランチで開発中です。

Cooperative gestures between two trackpads on a split keyboard — pinch zoom and drag & drop.
This is an independent PoC extension, developed on a separate branch.

- 📄 **[Setup & Design overview (English)](https://github.com/matoi/zmk-driver-iqs9151/blob/feature/cross-pad-gesture/documents/cross_pad_gesture_overview_en.md)**
- 📄 **[設計ドキュメント (日本語)](https://github.com/matoi/zmk-driver-iqs9151/blob/feature/cross-pad-gesture/documents/cross_pad_gesture_design.md)**
- 🔀 **[feature/cross-pad-gesture ブランチ](https://github.com/matoi/zmk-driver-iqs9151/tree/feature/cross-pad-gesture)**
- 📋 **[設定例リポジトリ](https://github.com/matoi/zmk-config-LalaPadGen2-cross-pad-gesture-example)**

---

私が自作したIQS9151トラックパッドモジュールをZMKで使用するための専用ドライバです。
トラックパッドによるカーソル移動/タップ/スクロール/ピンチインアウトや複数指ジェスチャなどの操作を扱えるようになります。  
また、ZMKからトラックパッドの動作設定を行いやすくする為の拡張機能がいくつか追加されます。

トラックパッドモジュールは[Booth（準備中）](https://shininet.booth.pm/)より入手可能です。  

<img width="600"  alt="image" src="https://github.com/user-attachments/assets/76c1e221-bab2-4d7d-9250-408a9b767e39" />

## ドライバの特徴

- IQS9151トラックパッドの入力をZMKへ統合
- 1本指カーソル移動、タップ&プレスホールド
- 2本指スクロール（縦/横）、タップ&プレスホールド、ピンチインアウト
- 3本指タップ&プレスホールド、スワイプ系ジェスチャ（設定に応じて有効化）
- 滑らかな慣性カーソル/スクロール対応
- ZMKのキーマップ連携（レイヤーごとに動作の割り当て可能）
- カーソルやスクロールの速度をリアルタイムに調整可能（電源OFFで設定が消えない）


## クイックスタート

ここでは最小構成での導入手順を示します。  
前提となるZMKの基本構成・ビルド手順は本ドキュメントでは取り扱いません。

### 1. `west.yml` にモジュールを追加

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: ShiniNet                                #<---add this
      url-base: https://github.com/ShiniNet         #<---add this

  projects:
    - name: zmk
      remote: zmkfirmware
      revision: v0.3.0
      import: app/west.yml

    - name: zmk-driver-iqs9151                      #<---add this
      remote: ShiniNet                              #<---add this
      revision: main                                #<---add this

  self:
    path: config
```

### 2. `.conf` に必要設定を追加

```conf
CONFIG_I2C=y
CONFIG_ZMK_POINTING=y
CONFIG_ZMK_POINTING_SMOOTH_SCROLLING=y
CONFIG_INPUT_IQS9151=y
CONFIG_INPUT_IQS9151_LOG_LEVEL=3
```

必要に応じてトラックパッドの調整やジェスチャーONOFFや閾値の設定を追加してください（ConfigList.md参照）。

### 3. DTS(overlay) にIQS9151ノードを追加（Xiao BLE且つセントラル側の例）

```dts
&xiao_i2c {
    status = "okay";

    iqs9151: iqs9151@56 {
        compatible = "azoteq,iqs9151";
        reg = <0x56>;
        status = "okay";
        irq-gpios = <&gpio1 11 (GPIO_ACTIVE_LOW)>;
    };
};

/ {
    trackpad_listener {
        compatible = "zmk,input-listener";
        device = <&iqs9151>;
        status = "okay";
    };
};
```
必要に応じてトラックパッドの速度調整等をする為にInput-Processorを追加してください（Input-ProcessorList.md参照）。

### 4. 物理配線

- `SDA` -> MCUのI2C SDA（Xiao BLEの場合D4）
- `SCL` -> MCUのI2C SCL（Xiao BLEの場合D5）
- `DR` -> DTSの`irq-gpios`で指定したGPIO入力（今回の例ではGPIO1.11=D6）
- `RST` -> MCUのRESETピン（省略可能）
- `3.3V` -> 3.3V電源
- `GND` -> GND

※SDA/SCL/DRのプルアップ抵抗はトラックパッド側に4.7KΩ実装済みなので不要。  

<img width="447" height="202" alt="image" src="https://github.com/user-attachments/assets/e2b8f28e-d779-4635-be67-05a65c6e2911" />

### 5. 動作確認（デフォルト機能）

- ビルド/書き込み後、1～2本指スワイプによるポインタ移動とスクロールを確認
- 1～3本指タップによる左右中クリック、長押しによるプレスホールドを確認
- 3本指左右スワイプによるマウスボタン4-5(進む戻る)の出力を確認

※更なる動作をキーマップから設定できるようにするにはコンフィグ及びDTSの設定が必要です。  
  
  
## 応用編（ドキュメント整備中...）

- ZMKキーマップと連携しKeymap EditorやZMK Studioからトラックパッドの動作を変更する
- キー入力によってトラックパッドのカーソル(スクロール)速度を動的に変更する
- スプリットキーボード構成への組み込み。ダブルトラックパッド化

実装例は `Lalapadv2` 関連リポジトリを参照してください。(準備中)


## 対応環境

- ZMK Firmware `v0.3.0` 以上
- 確認済みMCU/ボード: `Seeed XIAO BLE` / `Seeed XIAO BLE Plus`
- 他のnRF52840系ボードでも動作する可能性はありますが、未検証です（利用は自己責任）。
- 最低でも `SDA/SCL/DR` 用GPIO + `3.3V/GND` が利用可能であること。

## 注意事項

- `TPS65` など同社の汎用トラックパッド等では動作しません。


## 免責事項・その他

- 本ドライバは継続開発中のため、意図しない動作の変更や不具合が発生する可能性があります。
- 本ソフトウェアは現状有姿（as is）で提供され、利用・導入・運用は利用者の自己責任です。
- ドライバ自体の不具合やその他の問題を見つけた場合、ISSUEを起こしてください。
