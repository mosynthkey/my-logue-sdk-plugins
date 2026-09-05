import { inject, provide, ref, watch } from "vue";

const I18N_KEY = Symbol("i18n");
const STORAGE_KEY = "logue-sdk-preview-language";

export const japanesePluginDescriptions = {
  airfm: "Alesis airSynthに着想を得た、FM効果音シンセです。",
  airhorn: "AirHornを鳴らします。\n現在も調整中で、正式版\u2060は近日公開予定。",
  fbackosc: "JP-8080に着想を得たFeedback oscillatorです。band-limited sawをkey-tracked resonant comb filterに通します。",
  hypersaw: "Virus TIに着想を得た9-voice detuned saw stackです。Density、Spread、HyperSub、stereo widthを調整できます。",
  kaocid: "XY Padで演奏する303系のシンセです。Tapするとフレーズが生成されます。\n現在も調整中で、正式版\u2060は近日公開予定。",
  loopkey: "keyboardで操作するmicro-looper oscillatorです。external audio inputをloopし、MIDI noteでloop lengthを設定します。Tempo sync、gate mode、evolutionに対応します。",
  ride909: "テクノでよく聞く、裏打ちの909 Ride Cymbalを再生します。ピッチを変更することができます。\n現在も調整中で、正式版\u2060は近日公開予定。",
  shaker: "PhISEM shakerの移植です。XY Pad / 鍵盤でさまざまなパーカッションを演奏できます。\n現在も調整中で、正式版\u2060は近日公開予定。",
  specwarp: "Vitalに着想を得たspectral stretch / smear FXです。InputをFFTし、frequency domainでwarpしてからresynthesisします。X = Stretch（metallic / ambient textureを作るfrequency-axis warp）、Y = Smear（magnitude blurとphase diffusion）、Depth/MIX = dry/wetです。drum loopからotherworldly ambienceをすぐに作れます。",
  technorumble: "Techno rumble kick processorです。長いreverb tail、sub LPF、drive、kick transientに反応するsidechain duckを1ユニットにまとめています。NTS-3はAUDIO IN、mkIIはsynth出力にkickを入れて使います。X/TIME = decay、Y/DEPTH = cutoff、Depth/MIX = dry/wetです。",
  tapeosc: "tape motorのstart/stopを再現するVarispeed oscillatorです。band-limited synth waveformがpitch envelopeではなく、tape deckのように減速してfreezeします。",
  transitionlooper: "DJの繋ぎ用ルーパーです。テンポ同期した16ステップのステレオループをAUDIO INから取り込みます。ファーム1.4以降はパッドを離しているあいだも事前録音できます。事前録音が無音のときは、最初のホールドで1小節録ってからループします。パッドを離しているときはバイパス、押しているあいだは保存したループへフェードします。Xはフェード時間、Yはフィルタの振り幅、TYPEは音量 / ハイパス / ローパス / ベーススワップ / エコーアウト / ブレーキ / ループロールです。",
  glitchpad: "Illformed Glitch²に着想を得たNTS-3用グリッチです。AUDIO INをテンポ同期バッファに取り込み、XYパッドでシーンを演奏します。触っていないときはバイパス。Xはシーン（リトリガー / リバース / シャッフル / テープストップ / ストレッチ / ゲート / クラッシュ / ディレイ）、Yはスライスの長さ、Depthはwetです。128シーンのシーケンサUIは移植していません。",
  beatrepeat: "テンポ同期のBeat Repeatです。AUDIO INを常時取り込み、拍に吸着したスライスを繰り返します。Xはループ長（1/32〜2拍）、Yは発火確率、タッチで強制フリーズです。",
  echofreeze: "Octatrack風のEcho Freezeです。タッチでディレイバッファをLockし、入力を止めずに中身だけ無限再生します。Xはバッファ長（拍）、Yは再生ピッチです。",
  ringexcit: "入力で弦/モーダル共振体を叩くエキサイターです。キックやノイズがピッチのあるトーンになります。Xは周波数、Yは明るさ/減衰、タッチで内部ノイズ励起です。",
  speccloud: "対数帯域のゲインを確率で張り替えるスペクトラル雲です。フィルタスイープとは違い、帯域がちらつきます。Xは帯域数、Yは再ロール確率、タップでスペクトラム再抽選です。",
  warpsmorph: "ダイオードリング / XOR / コンパレータ / ミニボコーダ / フォールダを1軸で横断するクロス変調です。Xはアルゴリズム、Yはキャリヤ周波数、タッチで内部キャリヤ混合です。",
  pumpduck: "エンベロープフォロワーで音量 / フィルタ / 内部Plateをダックします。キック込みのステムを呼吸させます。Xはアタック/リリース感、Yは深さ、タッチでダック先切替です。",
  rmxscene: "Pioneer RMXのScene FX思想です。XはBuild（ノイズ+HPF）とBreak（クラッシュ+エコー）のモーフ、Yは深さ。離すとRelease Echoで本編に戻ります。",
  eucgate: "DJ TransformとEuclidean / 確率ゲートを統合したリズムミュートです。Xはヒット数、Yはデューティ/確率、タッチでFill（全開ゲート）です。",
  talkform: "入力を母音フォルマントで喋らせるトークフィルタです。Vocoderともトーク合成OSCとも別物です。Xは第1フォルマント、Yは第2、タッチで母音量子化です。",
  mswidth: "M/S処理でサイド拡張やミッド（ボーカル寄り）殺しができます。XはMidゲイン、YはSideゲイン、タッチでCenter Killにスナップします。",
  databend: "CDスキップ / 破損データ / テープ引っかかりを1マクロにしたメディア故障シミュです。XはBend、YはCorrupt、タッチで破損フレームをフリーズします。",
  reesephr: "デチューンソウのReeseドローンです。Hooverではなく、低速で疎に動く低域フレーズ付き。Xはピッチ、Yはデチューン、タッチでゲートです。",
  perciter: "Skin / Liquid / Metalをモーフするパーカッションパッドです。Xはピッチ、YはSkin↔Metal、タッチでトリガです。",
  shepard: "ピッチが永遠に上がる錯覚のシェパード・ライザーです。純正RISER REVERBとは別物。Xは上昇速度、Yは明るさ、離すと急減衰でドロップ合図になります。",
  gridsdrum: "MI Grids的なXYでジャンル密度を決め、BD/SD/HHフレーズを生成します。タッチでシーケンサ走行、右上フリックで1小節Fillです。",
  riffdice: "Elektronのconditional trigを音源側に内蔵したリフ・ダイスです。Xはルート、Yは密度/確率、タッチでフレーズ走行です。",
  chordres: "触った位置のコードを鳴らしたあと、離すと残存アルペジオがテンポ同期で溶けていきます。Xはコード進行軸、Yはヴォイシング、離すとResidueが始まります。",
  hclap: "808 / 909のアナログ・ハンドクラップをXYパッドで鳴らします。ホールドでフレーズ走行。Xは手数（2と4から16分まで）、Yは808→909。\n現在も調整中で、正式版\u2060は近日公開予定。",
};

const messages = {
  en: {
    plugin: "Plugin",
    language: "Language",
    pluginList: "Plugin list",
    selectPlugin: "Select plugin",
    sendTo: "Send to {target}",
    sendToDevice: "Send to device",
    download: "Download",
    downloadFor: "Download {target}",
    loading: "Loading plugins…",
    preview: "Preview",
    latch: "Latch",
    hold: "Hold",
    on: "On",
    off: "Off",
    close: "Close",
    midiRequired: "Chrome or Edge required for MIDI. Download the unit otherwise.",
    output: "Output",
    input: "Input",
    channel: "Channel",
    noPorts: "No ports",
    cancel: "Cancel",
    sendToSlot: "Send to slot",
    log: "Log",
    octaveDown: "Octave down",
    octaveUp: "Octave up",
  },
  ja: {
    plugin: "プラグイン",
    language: "言語",
    pluginList: "プラグイン一覧",
    selectPlugin: "プラグインを選択",
    sendTo: "{target}へ送信",
    sendToDevice: "デバイスに送信",
    download: "ダウンロード",
    downloadFor: "{target}をダウンロード",
    loading: "プラグインを読み込み中…",
    preview: "プレビュー",
    latch: "ラッチ",
    hold: "ホールド",
    on: "オン",
    off: "オフ",
    close: "閉じる",
    midiRequired: "MIDI送信にはChromeまたはEdgeが必要です。それ以外のブラウザではユニットをダウンロードしてください。",
    output: "出力",
    input: "入力",
    channel: "チャンネル",
    noPorts: "ポートなし",
    cancel: "キャンセル",
    sendToSlot: "スロットへ送信",
    log: "ログ",
    octaveDown: "オクターブを下げる",
    octaveUp: "オクターブを上げる",
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
