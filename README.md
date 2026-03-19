# zmk-driver-iqs9151

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
- **クロスパッドジェスチャー**: 左右分割キーボードで両側のトラックパッドを同時に使い、ピンチズームやドラッグ&ドロップが可能


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

> **フォークやブランチを使用する場合:**
> `remotes` と `projects` を変更してください。例えば、クロスパッドジェスチャー機能を含むブランチを使用する場合:
> ```yaml
>   remotes:
>     - name: matoi
>       url-base: https://github.com/matoi
>
>   projects:
>     - name: zmk-driver-iqs9151
>       remote: matoi
>       revision: feature/cross-pad-gesture
> ```

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


## クロスパッドジェスチャー

> **⚠️ この機能は本家 ([ShiniNet/zmk-driver-iqs9151](https://github.com/ShiniNet/zmk-driver-iqs9151)) には含まれていない独自拡張の PoC (Proof of Concept) です。**
> 動作は無保証であり、予告なく変更・削除される可能性があります。本家ドライバとの互換性も保証されません。
> 使用するには [fork のブランチ](https://github.com/matoi/zmk-driver-iqs9151/tree/feature/cross-pad-gesture) を参照してください。

左右分割キーボードの両側にトラックパッドがある構成で、両側を同時にタッチすることで特殊ジェスチャーを実行する機能です。

### 使えるジェスチャー

| 左 | 右 | 動作 |
|:--:|:--:|------|
| 1本指 | 1本指 | **ピンチイン・アウト** — 指を外側/内側に動かしてズーム |
| 1本指 | 2本指 | **プレス＆ホールド** — 左クリックを押しながらカーソル移動（ドラッグ） |
| 2本指 | 1本指 | **プレス＆ホールド** — 同上 |
| 2本指 | 2本指 | **プレス＆ホールド** — 同上 |

※ジェスチャーの割り当ては `iqs9151_cross_pad_resolve()` の switch 文を編集することで変更可能です。

### セットアップ

クロスパッドジェスチャーを使用するには、以下の3つの設定が必要です。

#### 1. `.conf` — 左右それぞれに追加

**左側 (peripheral):**
```conf
CONFIG_INPUT_IQS9151_CROSS_PAD=y
CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_LEFT=y
```

**右側 (central):**
```conf
CONFIG_INPUT_IQS9151_CROSS_PAD=y
CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_RIGHT=y
```

#### 2. DTS — `pdt` ビヘイビアの定義

共通の `.dtsi` ファイルに以下を追加します（central → peripheral 通信に必要）:

```dts
/ {
    behaviors {
        pdt: pdt {
            compatible = "zmk,behavior-pad-touch";
            #binding-cells = <2>;
        };
    };
};
```

#### 3. DTS — central 側の overlay

central 側の overlay で、peripheral のトラックパッドを `cross-pad-peer-input` として指定します:

```dts
&iqs9151 {
    cross-pad-peer-input = <&trackpad_split_L>;
};
```

`trackpad_split_L` は peripheral 側のトラックパッド入力を受け取る `zmk,input-split` デバイスです。

### Kconfig オプション

| 設定 | 型 | デフォルト | 説明 |
|------|----|-----------|------|
| `CONFIG_INPUT_IQS9151_CROSS_PAD` | bool | `n` | クロスパッドジェスチャーの有効化 |
| `CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_LEFT` | choice | — | このトラックパッドが左側であることを指定 |
| `CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_RIGHT` | choice | — | このトラックパッドが右側であることを指定 |
| `CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_GAIN_X10` | int | `40` | ピンチのホイール出力ゲイン (10=1.0倍, 40=4.0倍, 80=8.0倍) |
| `CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER` | int | `1` | ピンチ時の修飾キー (0=なし, 1=Left Ctrl, 2=Mouse Button 4) |

**修飾キーの選択について:**
- `1` (Left Ctrl): ほとんどのOSで Ctrl+Wheel ズームとして動作します（デフォルト）
- `2` (Mouse Button 4): macOS で BetterTouchTool 等のユーティリティを使用し、MB4 に smart zoom を割り当てている場合に便利です
- `0` (なし): REL_WHEEL のみ出力。修飾キーの制御を別の方法で行う場合

### 設定例

左右両方の `.conf` に以下を追加する最小構成例:

**左側 `.conf`:**
```conf
# Cross-pad gesture
CONFIG_INPUT_IQS9151_CROSS_PAD=y
CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_LEFT=y
CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_GAIN_X10=80
CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER=1
```

**右側 `.conf`:**
```conf
# Cross-pad gesture
CONFIG_INPUT_IQS9151_CROSS_PAD=y
CONFIG_INPUT_IQS9151_CROSS_PAD_SIDE_RIGHT=y
CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_GAIN_X10=80
CONFIG_INPUT_IQS9151_CROSS_PAD_PINCH_MODIFIER=1
```


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
