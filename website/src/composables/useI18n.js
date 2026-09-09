import { inject, provide, ref, watch } from "vue";

const I18N_KEY = Symbol("i18n");
const STORAGE_KEY = "logue-sdk-preview-language";

export const japanesePluginDescriptions = {
  airfm: "Alesis airSynthに着想を得た、FM効果音シンセです。",
  airhorn: "AirHornを鳴らします。\n現在も調整中で、正式版\u2060は近日公開予定。",
  amentime: "合成した1小節のamen風ブレイクを、ホストBPMに合わせてスライス再生します。オリジナル録音は入っていません。パッドを押しているあいだ、小節頭からステップ同期で再生（タップ位置は開始16分を決めません）。Xはリバース確率、Yはグリッド（1/4〜1/32）、STRTはEditで開始オフセット。手元のWAVはWavSliceを使います。",
  wavslice: "任意の1小節WAVをホストBPMに合わせてスライス再生します。同梱はオリジナルのCC0ドラムループ。パッドを押しているあいだ、小節頭からステップ同期で再生（タップ位置は開始16分を決めません）。Xはリバース確率、Yはグリッド。assets/loop.wav を置くか make embed WAV=... で差し替えます。",
  fbackosc: "JP-8080に着想を得たFeedback oscillatorです。band-limited sawをkey-tracked resonant comb filterに通します。FEEDを上げても1/(1-fb)で音量が跳ねないよう補償しています。",
  mohowl: "作者モチーフのフィードバックハウリングです。パッドを押すとJP-8080風の金切り声が出ます。周波数はLFOで揺れ、Xはその深さ、Yはハーモニクス、フィードバックは最大固定です。",
  hypersaw: "Virus TIに着想を得た9-voice detuned saw stackです。Density、Spread、HyperSub、stereo widthを調整できます。",
  kaocid: "XY Padで演奏する303系のシンセです。Tapで16ステップのフレーズを生成（位置はCUT/RESのみ、フレーズ種は連打で進みます）。\n現在も調整中で、正式版\u2060は近日公開予定。",
  loopkey: "keyboardで操作するmicro-looper oscillatorです。external audio inputをloopし、MIDI noteでloop lengthを設定します。Tempo sync、gate mode、evolutionに対応します。",
  ride909: "テクノでよく聞く、裏打ちの909 Ride Cymbalを再生します。ピッチを変更することができます。\n現在も調整中で、正式版\u2060は近日公開予定。",
  shaker: "PhISEM shakerの移植です。XY Pad / 鍵盤でさまざまなパーカッションを演奏できます。\n現在も調整中で、正式版\u2060は近日公開予定。",
  technorumble: "Techno rumble kick processorです。長いreverb tail、sub LPF、drive、kick transientに反応するsidechain duckを1ユニットにまとめています。NTS-3はAUDIO IN、mkIIはsynth出力にkickを入れて使います。X/TIME = decay、Y/DEPTH = cutoff、Depth/MIX = dry/wetです。",
  tapeosc: "tape motorのstart/stopを再現するVarispeed oscillatorです。band-limited synth waveformがpitch envelopeではなく、tape deckのように減速してfreezeします。",
  transitionlooper: "DJの繋ぎ用ルーパーです。テンポ同期した16ステップのステレオループをAUDIO INから取り込みます。ファーム1.4以降はパッドを離しているあいだも事前録音できます。事前録音が無音のときは、最初のホールドで1小節録ってからループします。パッドを離しているときはバイパス、押しているあいだは保存したループへフェードします。Xはフェード時間、Yはフィルタの振り幅、TYPEは音量 / ハイパス / ローパス / ベーススワップ / エコーアウト / ブレーキ / ループロールです。",
  glitchpad: "Illformed Glitch²に着想を得たNTS-3用グリッチです。AUDIO INをテンポ同期バッファに取り込み、XYパッドでシーンを演奏します。触っていないときはバイパス。Xはシーン（リトリガー / リバース / シャッフル / テープストップ / ストレッチ / ゲート / クラッシュ / ディレイ）、Yはスライスの長さ、Depthはwetです。128シーンのシーケンサUIは移植していません。",
  beatrepeat: "テンポ同期のBeat Repeatです。AUDIO INを常時取り込み、拍に吸着したスライスを繰り返します。Xはループ長（1/32〜2拍）、Yは発火確率、タッチで強制フリーズです。",
  ringexcit: "入力で弦/モーダル共振体を叩くエキサイターです。キックやノイズがピッチのあるトーンになります。Xは周波数、Yは明るさ/減衰、タッチで内部ノイズ励起です。",
  warpsmorph: "ダイオードリング / XOR / コンパレータ / ミニボコーダ / フォールダを1軸で横断するクロス変調です。Xはアルゴリズム、Yはキャリヤ周波数、タッチで内部キャリヤ混合です。",
  eucgate: "DJ TransformとEuclidean / 確率ゲートを統合したリズムミュートです。Xはヒット数、Yはデューティ/確率、タッチでFill（全開ゲート）です。",
  eucroll: "ユークリッド・ステップロールです。常時録音し、タッチ中はユークリッドのヒット・ステップだけをロール（非ヒットはドライのまま）。Xは密度、Yはステップ内の細分化、Depthはマイクロロールごとのランダムパン幅です。",
  databend: "CDスキップ / 破損データ / テープ引っかかりを1マクロにしたメディア故障シミュです。XはBend、YはCorrupt、タッチで破損フレームをフリーズします。",
  reesephr: "デチューンソウのReeseドローンです。Hooverではなく、低速で疎に動く低域フレーズ付き。Xはピッチ、Yはデチューン、タッチでゲートです。",
  perciter: "Skin / Liquid / Metalをモーフするパーカッションパッドです。Xはピッチ、YはSkin↔Metal、タッチでトリガです。",
  gridsdrum: "MI Grids的なXYでジャンル密度を決め、BD/SD/HHフレーズを生成します。タッチでシーケンサ走行、右上フリックで1小節Fillです。",
  ukgarage: "テンポ同期の2-step UK Garageキットです。ホールドはゲートのみで、タップ瞬間ではなくホストの拍グリッドに合わせて発音します。Xはゴーストノート、Yはハット密度とFill感。右上フリックで1小節Fill。",
  hclap: "808 / 909のアナログ・ハンドクラップをXYパッドで鳴らします。ホールドでフレーズ走行。Xは手数（2と4から16分まで）、Yは808→909。\n現在も調整中で、正式版\u2060は近日公開予定。",
  hsnare: "808 / 909のアナログ・スネアをXYパッドで鳴らします。ホールドでフレーズ走行。Xは手数（2と4から16分まで）、Yは808→909。\n現在も調整中で、正式版\u2060は近日公開予定。",
  revroll: "DJMのRev Rollです。タッチで掴んだ区間を逆再生で繰り返します。Xは区間長、Yは逆再生の速度カーブ。離すと順方向に戻ります。",
  dredbass: "UKG／ジャングルのサクション低域です。LPF開口エンベロープを逆向きにして逆再生に聞こえるベースになります。Xはピッチ、Yはサクションの深さです。",
  stepdice: "テンポに同期して1小節を16/8/4/2/1ステップに分け、ステップごとにバラバラのエフェクトをかけます。Xは強さ、Yはパターンの種、タッチで再ロール（RUN）またはグリッドON（HOLD）です。",
  stepfenv: "テンポ同期のステップ・フィルタ・エンベロープです。各ステップでカットオフEGが発火します（アタック0、ディケイ可変、サスティン0、リリース0）。Xはカットオフ、Yはエンベロープの深さ、タッチでグリッドをリセットします。",
  steprndflt: "テンポ同期のサンプル＆ホールドLFOをマルチモード・フィルタのカットオフへかけます。各ステップでランダム値を引き直し、Xは深さ、Yはレゾナンス。TYPEでLP12/LP24/BPF/HP12/HP24を選択。パッドを押している間だけ効きます。",
  trap808: "トラップ風ドラム＋909 ROMハイハット＋スライドする808ベースです。ホールドでゲートし、発音は常にビートにロック（タップタイミング無視）。Xはハット密度／ロール、Yはグルーヴ、ROOTで808のキー。右上フリックで次の拍頭から1小節フィル。",
};

const messages = {
  en: {
    plugin: "Plugin",
    language: "Language",
    pluginList: "Plugin list",
    selectPlugin: "Select plugin",
    category: "Category",
    selectCategory: "Filter by category",
    categoryAll: "All",
    categoryOscillator: "Oscillator",
    categorySynth: "Synth",
    categoryDrum: "Drum",
    categoryFx: "FX",
    noPluginsInCategory: "No plugins in this category.",
    sendTo: "Send to {target}",
    sendToDevice: "Send to device",
    connectUsbHint: "Connect {target} over USB. It will be recognized automatically.",
    download: "Download",
    downloadFor: "Download {target}",
    loading: "Loading plugins…",
    preview: "Preview",
    latch: "Latch",
    hold: "Hold",
    play: "Play",
    stop: "Stop",
    inputSource: "Source",
    selectInputSource: "Select input source",
    sourceHouse: "House Loop",
    sourceTechno: "Techno Loop",
    sourceGarage: "UK Garage",
    sourceAcid: "Acid Line",
    sourceKick: "Four-on-the-floor Kick",
    sourceBreakbeat: "Breakbeat",
    sourceReese: "Reese Bass",
    sourceStab: "Chord Stab",
    sourceSawtooth: "Sawtooth",
    sourceSquare: "Square",
    sourceSine: "Sine",
    sourceTriangle: "Triangle",
    sourceNoise: "Noise",
    on: "On",
    off: "Off",
    close: "Close",
    dspHowItWorks: "How the DSP works",
    dspBlockDiagram: "Block diagram",
    dspExplainMissing: "DSP notes for this unit are not available yet.",
    midiRequired: "Chrome or Edge required for MIDI. Download the unit otherwise.",
    output: "Output",
    input: "Input",
    channel: "Channel",
    noPorts: "No ports",
    cancel: "Cancel",
    sendToSlot: "Send to slot",
    sendSlotHint: "Choose a {module} slot, then send",
    slot: "Slot",
    log: "Log",
    octaveDown: "Octave down",
    octaveUp: "Octave up",
    appHeaderKicker: "",
    appHeaderTitle: "My Logue SDK Plugins",
    editProgram: "Program Edit",
    programEditorKicker: "NTS-3 kaoss pad",
    programEditorTitle: "Program Editor",
    routing: "Routing",
    fxSlots: "FX slots",
    slotFx: "Slot FX",
    clearSlot: "Clear",
    fxInSelect: "FX In Select",
    fxReleaseMode: "FX Release Mode",
    fxReleaseTime: "FX Release Time",
    outGain: "Out Gain",
  },
  ja: {
    plugin: "プラグイン",
    language: "言語",
    pluginList: "プラグイン一覧",
    selectPlugin: "プラグインを選択",
    category: "カテゴリー",
    selectCategory: "カテゴリーで絞り込み",
    categoryAll: "すべて",
    categoryOscillator: "オシレーター",
    categorySynth: "シンセ",
    categoryDrum: "ドラム",
    categoryFx: "FX",
    noPluginsInCategory: "このカテゴリーのプラグインはありません。",
    sendTo: "{target}へ送信",
    sendToDevice: "デバイスに送信",
    connectUsbHint: "{target}をUSB経由で接続してください。自動的に認識されます。",
    download: "ダウンロード",
    downloadFor: "{target}をダウンロード",
    loading: "プラグインを読み込み中…",
    preview: "プレビュー",
    latch: "ラッチ",
    hold: "ホールド",
    play: "再生",
    stop: "停止",
    inputSource: "音源",
    selectInputSource: "入力音源を選択",
    sourceHouse: "ハウスループ",
    sourceTechno: "テクノループ",
    sourceGarage: "UKガレージ",
    sourceAcid: "アシッドライン",
    sourceKick: "4つ打ちキック",
    sourceBreakbeat: "ブレイクビーツ",
    sourceReese: "リースベース",
    sourceStab: "コードスタブ",
    sourceSawtooth: "ノコギリ波",
    sourceSquare: "矩形波",
    sourceSine: "サイン波",
    sourceTriangle: "三角波",
    sourceNoise: "ノイズ",
    on: "オン",
    off: "オフ",
    close: "閉じる",
    dspHowItWorks: "DSPの仕組み",
    dspBlockDiagram: "ブロック図",
    dspExplainMissing: "このユニットのDSP解説はまだありません。",
    midiRequired: "MIDI送信にはChromeまたはEdgeが必要です。それ以外のブラウザではユニットをダウンロードしてください。",
    output: "出力",
    input: "入力",
    channel: "チャンネル",
    noPorts: "ポートなし",
    cancel: "キャンセル",
    sendToSlot: "スロットへ送信",
    sendSlotHint: "{module}スロットを選んで送信",
    slot: "スロット",
    log: "ログ",
    octaveDown: "オクターブを下げる",
    octaveUp: "オクターブを上げる",
    appHeaderKicker: "",
    appHeaderTitle: "My Logue SDK Plugins",
    editProgram: "Program Edit",
    programEditorKicker: "NTS-3 kaoss pad",
    programEditorTitle: "Program Editor",
    routing: "ルーティング",
    fxSlots: "FXスロット",
    slotFx: "SLOT FX",
    clearSlot: "Clear",
    fxInSelect: "FX IN SELECT",
    fxReleaseMode: "FX RELEASE MODE",
    fxReleaseTime: "FX RELEASE TIME",
    outGain: "OUT GAIN",
  },
};

function initialLocale() {
  const saved = window.localStorage.getItem(STORAGE_KEY);
  if (saved === "en" || saved === "ja") return saved;
  return navigator.language.toLowerCase().startsWith("ja") ? "ja" : "en";
}

export function provideI18n() {
  const locale = ref(initialLocale());

  function setLocale(nextLocale) {
    if (messages[nextLocale]) locale.value = nextLocale;
  }

  function t(key, params = {}) {
    const template = messages[locale.value][key] ?? messages.en[key] ?? key;
    return Object.entries(params).reduce(
      (text, [name, value]) => text.replaceAll(`{${name}}`, String(value)),
      template,
    );
  }

  function pluginDescription(plugin) {
    if (locale.value !== "ja") return plugin.description;
    return japanesePluginDescriptions[plugin.id] ?? plugin.description;
  }

  watch(locale, (nextLocale) => {
    window.localStorage.setItem(STORAGE_KEY, nextLocale);
    document.documentElement.lang = nextLocale;
  }, { immediate: true });

  const i18n = { locale, setLocale, t, pluginDescription };
  provide(I18N_KEY, i18n);
  return i18n;
}

export function useI18n() {
  const i18n = inject(I18N_KEY);
  if (!i18n) throw new Error("i18n provider is missing");
  return i18n;
}
