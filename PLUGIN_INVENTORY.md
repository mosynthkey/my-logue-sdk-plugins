# プラグイン一覧

- **airFM** (`airfm`) — fm
  - 対応: nts-3_kaoss
  - Alesis airSynth 風の FM 効果音シンセ。

- **AirHorn** (`airhorn`) — osc
  - 対応: nts-1_mkii, nts-3_kaoss, microkorg2
  - エアホーンを鳴らす。調整中。正式リリース予定。

- **BeatRepeat** (`beatrepeat`) — fx（実験的）
  - 対応: nts-3_kaoss
  - テンポ同期の Beat Repeat。AUDIO IN を常時キャプチャし、グリッド上でスライスをスタッター。X=ループ長、Y=発火確率、タッチで強制フリーズ。

- **DataBend** (`databend`) — fx（実験的）
  - 対応: nts-3_kaoss
  - CDスキップ・テープ切れ・ビット腐敗をまとめたメディア破損バッファ。X=ベンド、Y=コラプト。タッチで破損フレームをノイズ壁に固定。

- **DredBass** (`dredbass`) — synth（実験的）
  - 対応: nts-3_kaoss
  - UKG / ジャングル風サクションベース。LPF エンベロープが逆向きに開き、反転したような低音。X=ピッチ、Y=サクション深さ。

- **EucGate** (`eucgate`) — fx（実験的）
  - 対応: nts-3_kaoss
  - DJ Transform ゲート＋ユークリッド/確率チョップ。LFO トレモロではなくグリッド上のヒット。X=ヒット数、Y=デューティ/確率。タッチ=Fill。

- **EucRoll** (`eucroll`) — fx（実験的）
  - 対応: nts-3_kaoss
  - ユークリッド・ステップロール。常時録音。タッチ中はヒット・ステップだけをロール（非ヒットはドライ）。X=密度、Y=細分化、Depth=ランダムパン幅。

- **FbOsc** (`fbackosc`) — osc（実験的）
  - 対応: nts-1_mkii, microkorg2
  - JP-8080 風フィードバックオシレータ。帯域制限ソーをキー追従の共振コムで加工。

- **GlitchPad** (`glitchpad`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Glitch² 風 XY パッド。テンポ同期の AUDIO IN バッファからシーン（RTRG/REV/SHUF 等）を再生。非タッチ=バイパス。X=シーン、Y=スライス/レート。

- **GrainPad** (`grainpad`) — fx（実験的）
  - 対応: nts-3_kaoss
  - 直近 AUDIO IN を最大3秒フリーズ→Hann風グラニュラー。X=疎↔密、Y=±1oct。ENV=粒のA/R、SPRD / HPF / REVS。

- **ReGrain** (`regrain`) — fx（実験的）
  - 対応: nts-3_kaoss
  - 深いウェット・リバーブを常時かけてその音をキャプチャ。タッチでフリーズ→グレイン雲。X=密度、Y=SIZE（残響の深さ）、TONE/ENV/SPRD/REVS。

- **GridsDrum** (`gridsdrum`) — synth（実験的）
  - 対応: nts-3_kaoss
  - Mutable Grids 風の BD/SD/HH 生成フレーズ。X=キック/スネアマップ、Y=ハット密度。右上フリックで 1 小節 Fill。

- **HClap** (`hclap`) — drum（実験的）
  - 対応: nts-3_kaoss
  - 808/909 風アナログハンドクラップのフレーズパッド。X=ヒット密度、Y=808↔909。調整中。

- **HSnare** (`hsnare`) — drum（実験的）
  - 対応: nts-3_kaoss
  - 808/909 風アナログスネアのフレーズパッド。X=ヒット密度、Y=808↔909。調整中。

- **HyperSaw** (`hypersaw`) — osc（実験的）
  - 対応: nts-1_mkii, microkorg2
  - Virus TI 風 9 ボイス・デチューンソー。Density / Spread / HyperSub / ステレオ幅。

- **Kaocid** (`kaocid`) — synth
  - 対応: nts-3_kaoss
  - XY パッドで弾く 303 風シンセ。タップでフレーズ生成。調整中。正式リリース予定。

- **LoopKey** (`loopkey`) — loopkey（実験的）
  - 対応: nts-1_mkii
  - キーボード制御のマイクロルーパー osc。外部入力をループし、MIDI ノートでループ長を設定。テンポ同期・ゲート・進化。

- **MeloCap** (`melocap`) — fx（実験的）
  - 対応: nts-3_kaoss
  - DJM Melodic Capture 風。タッチで直近キャプチャをスケール量子化ピッチ（X）とグレイン長（Y）で再生。長押しで再キャプチャ。

- **MicroGap** (`microgap`) — fx（実験的）
  - 対応: nts-3_kaoss
  - ドロップ前の真空マイクロギャップ。タッチで次のダウンビートに量子化したミュートをアーム。X=ギャップ長、Y=微小ノイズ床。

- **MoHowl** (`mohowl`) — synth
  - 対応: nts-3_kaoss
  - 作者モチーフのフィードバック・ハウル。JP-8080 風コムスクリーム。X=LFO 深さ、Y=ハーモニクス。

- **MsWidth** (`mswidth`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Mid/Side 幅とセンターキル。X=Mid、Y=Side。タッチで Center Kill（Mid ミュート）プリセット。

- **PercIter** (`perciter`) — synth（実験的）
  - 対応: nts-3_kaoss
  - Basimilus 風パーカッションパッド。加算/FM ボディ＋ノイズ＋フォルダ。Y で Skin↔Metal。タッチがトリガ、連打でフィル。

- **PullUp** (`pullup`) — fx（実験的）
  - 対応: nts-3_kaoss
  - プルアップ / リワインド。常時録音。タッチで 1 小節目・ドロップキュー・ホットキューへジャンプ。Y=リワインド演出。

- **PumpDuck** (`pumpduck`) — fx（実験的）
  - 対応: nts-3_kaoss
  - エンベロープフォロワー・サイドチェーン。AUDIO IN に追従して振幅/フィルタ/プレートをダック。X=アタック/リリース感、Y=深さ。

- **ReesePhr** (`reesephr`) — synth（実験的）
  - 対応: nts-3_kaoss
  - デチューン・リース・ドローン＋疎なフレーズ生成。タッチでビートするスーパーソーをゲート。1–2 小節ごとにルートが短3度歩いて圧力変化。

- **RevBass** (`revbass`) — synth（実験的）
  - 対応: nts-3_kaoss
  - ハードスタイル風リバースベース。オフビートにロックするスウェル。X=ピッチ、Y=アタック長とドライブ。

- **RevRoll** (`revroll`) — fx（実験的）
  - 対応: nts-3_kaoss
  - DJM Rev Roll。タッチでテンポ同期スライスをキャプチャし逆再生で繰り返す。X=スライス長、Y=逆再生カーブ。

- **RiddimJg** (`riddimjg`) — fx（実験的）
  - 対応: nts-3_kaoss
  - サウンドシステム風リディム・ジャグル。ループをバージョン・スライスに分割。X=バージョン選択、Y=クロスフェード速度。破壊グリッチではなくセレクタ切替。

- **Ride909** (`ride909`) — drum
  - 対応: nts-3_kaoss
  - テクノ定番の裏拍 909 ライドシンバル。ピッチ調整可。調整中。正式リリース予定。

- **RiffDice** (`riffdice`) — synth（実験的）
  - 対応: nts-3_kaoss
  - Elektron 風条件付きリフ生成。16 ステップのスケールシーケンスに always / 1:2 / パーセントトリガ。X=ルート、Y=条件ステップの発火確率。

- **RingExcit** (`ringexcit`) — fx（実験的）
  - 対応: nts-3_kaoss
  - 入力励起レゾネータ。キックやノイズで Karplus-Strong 弦＋3 モーダル倍音を弾く。タッチで内部ノイズ・プラック（ワンショットにも）。

- **RmxScene** (`rmxscene`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Pioneer RMX 風 Scene FX。X で Build（ノイズ+HPF）↔ Break（クラッシュ+エコー）。離すと短いエコー残響かスナップバック。

- **Shaker** (`shaker`) — shaker
  - 対応: nts-1_mkii, nts-3_kaoss
  - PhISEM シェイカーの移植。XY パッドやキーボードで各種パーカッション。調整中。正式リリース予定。

- **Shepard** (`shepard`) — synth（実験的）
  - 対応: nts-3_kaoss
  - 無限 Shepard / Risset ライザー。重なったオクターブが永遠に上昇。ホールドでビルド、離すとトーンを落としてドロップへ。

- **SlipRoll** (`sliproll`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Pioneer DJM Slip Roll。タッチで現在スライスをループしつつ下のトラックは進む。X 変更で再キャプチャ。Y=ライブ・ドライ残量（Helix）。

- **SnareRush** (`snarerush`) — synth（実験的）
  - 対応: nts-3_kaoss
  - 指数的スネアロール・ビルド。タッチで密度が倍々に増えるラッシュ、離すと最後のヒット。X=何小節に圧縮するか、Y=タイト↔ウォッシュ。

- **SpecCloud** (`speccloud`) — fx（実験的）
  - 対応: nts-3_kaoss
  - FFT スペクトラル・バンドクラウド。対数バンドのゲインをランダム再抽選。X=バンド数、Y=再抽選確率/スムーズ。タッチで即再描画。

- **SpecWarp** (`specwarp`) — fx（実験的）
  - 対応: nts-1_mkii, nts-3_kaoss
  - Vital 風スペクトラル・ストレッチ/スミア。FFT → 周波数領域ワープ → 再合成。X=Stretch、Y=Smear。ドラムループで異世界アンビエンス。

- **StepFenv** (`stepfenv`) — fx（実験的）
  - 対応: nts-3_kaoss
  - テンポ同期のステップ・フィルタ・エンベロープ。各ステップでカットオフEG（アタック0、ディケイ可変、サスティン0、リリース0）。X=カットオフ、Y=エンベロープ深さ。タッチでグリッドリセット。

- **Spiral** (`spiral`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Pioneer Spiral。エコーの各リピートでピッチドリフトが蓄積。X=エコー音価、Y=ドリフト量と方向。

- **TalkForm** (`talkform`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Talk / Digi Talk フォルマント・フィルタ。X/Y で第1・第2フォルマント。タッチであいうえお格子にスナップ。

- **TapeOsc** (`tapeosc`) — osc（実験的）
  - 対応: nts-1_mkii
  - テープモーター始動/停止つきバリスピード・オシレータ。ピッチエンベロープではなくテープデッキのように減速・凍結。

- **TechnoRumble** (`technorumble`) — fx（実験的）
  - 対応: nts-1_mkii, nts-3_kaoss
  - テクノ・ランブルキック処理。長いリバーブテール、サブ LPF、ドライブ、トランジェント連動サイドチェーン。

- **TransitionLooper** (`transitionlooper`) — fx（実験的）
  - 対応: nts-3_kaoss
  - DJ トランジション・ルーパー。AUDIO IN をテンポ同期 16 ステップ・ステレオループにキャプチャ。ホールドでループへフェード、離すと復帰。

- **WarpsMorph** (`warpsmorph`) — fx（実験的）
  - 対応: nts-3_kaoss
  - Mutable Warps 風クロスモッド・モーフ。ダイオードリング / XOR / コンパレータ / ミニボコーダ / ウェーブフォルダを横断。

- **WashOut** (`washout`) — fx（実験的）
  - 対応: nts-3_kaoss
  - ウォッシュアウト・ブラー・ビルド。タッチでリバーブ・コーラス・HPF の指数マクロ。離すとドライにスナップ。
