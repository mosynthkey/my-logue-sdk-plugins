#!/usr/bin/env python3
"""Boilerplate for NTS-3 idea pack batch 2. Does not overwrite batch 1."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_nts3_idea_plugins import NONE, PCT, STR, generate_one

BATCH2 = [
    {
        "id": "echoout",
        "class": "EchoOut",
        "name": "EchoOut",
        "unit_id": "0x00000023U",
        "type": "fx",
        "description": (
            "DJ Echo Out throw. Touch mutes the dry instantly and leaves a tempo-synced "
            "echo; lift keeps the wet decaying until it dies. X = delay note, Y = how fast "
            "the feedback falls after release."
        ),
        "ja": (
            "DJのEcho Outです。タッチでドライを即ミュートし、拍同期エコーだけ残します。"
            "離すとウェットが減衰し切るまで残ります。Xはディレイ音符、Yは離した後の減衰の速さです。"
        ),
        "params": [
            ("NOTE", 0, 1023, 400, NONE, 0, 0, "x", "Delay note: 1/8 to 2 beats"),
            ("FALL", 0, 1023, 450, NONE, 0, 0, "y", "How fast feedback dies after release"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Wet level of the thrown echo"),
            ("FB", 0, 1023, 820, NONE, 0, 0, "none", "Feedback while the pad is held"),
        ],
    },
    {
        "id": "sliproll",
        "class": "SlipRoll",
        "name": "SlipRoll",
        "unit_id": "0x00000024U",
        "type": "fx",
        "description": (
            "Pioneer DJM Slip Roll. Touch captures the current slice and loops it while "
            "the underlying track keeps advancing. Changing X recaptures. Y is how much "
            "live dry remains (Helix). Release returns to the real timeline."
        ),
        "ja": (
            "DJMのSlip Rollです。タッチした瞬間のスライスをループし、下の曲は進み続けます。"
            "Xを変えると再キャプチャ。Yは下のビートの残り量（Helix）。離すと正しい位置に戻ります。"
        ),
        "params": [
            ("LEN", 0, 1023, 300, NONE, 0, 0, "x", "Roll length, 1/16 to 2 beats"),
            ("SLIP", 0, 1023, 0, NONE, 0, 0, "y", "Live dry remaining under the roll"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Roll wet level"),
            ("GLUE", 0, 1023, 180, NONE, 0, 0, "none", "Loop splice crossfade"),
        ],
    },
    {
        "id": "revroll",
        "class": "RevRoll",
        "name": "RevRoll",
        "unit_id": "0x00000025U",
        "type": "fx",
        "description": (
            "DJM Rev Roll. Captures a tempo-synced slice on touch and repeats it backwards. "
            "X = slice length, Y = reverse playback curve. Release returns forward."
        ),
        "ja": (
            "DJMのRev Rollです。タッチで掴んだ区間を逆再生で繰り返します。"
            "Xは区間長、Yは逆再生の速度カーブ。離すと順方向に戻ります。"
        ),
        "params": [
            ("LEN", 0, 1023, 350, NONE, 0, 0, "x", "Captured slice, 1/16 to 2 beats"),
            ("CURV", 0, 1023, 512, NONE, 0, 0, "y", "Reverse speed curve (slow suck to 1x)"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Wet level"),
            ("GLUE", 0, 1023, 160, NONE, 0, 0, "none", "Loop splice crossfade"),
        ],
    },
    {
        "id": "spiral",
        "class": "Spiral",
        "name": "Spiral",
        "unit_id": "0x00000026U",
        "type": "fx",
        "description": (
            "Pioneer Spiral: each echo repeat accumulates pitch drift. X = echo note, "
            "Y = drift amount and direction (down left, up right). Touch engages."
        ),
        "ja": (
            "DJMのSpiralです。エコーの繰り返しごとにピッチが累積して上がる／下がります。"
            "Xはエコー音符、Yはドリフト量と方向（左で下降、右で上昇）。タッチでONです。"
        ),
        "params": [
            ("NOTE", 0, 1023, 380, NONE, 0, 0, "x", "Echo note, 1/8 to 1 beat"),
            ("DRFT", 0, 1023, 700, NONE, 0, 0, "y", "Pitch drift per repeat (down to up)"),
            ("MIX", 0, 1000, 700, PCT, 1, 1, "depth", "Dry remain / wet spiral"),
            ("FB", 0, 1023, 720, NONE, 0, 0, "none", "How many spiral repeats survive"),
        ],
    },
    {
        "id": "dubdesk",
        "class": "DubDesk",
        "name": "DubDesk",
        "unit_id": "0x00000027U",
        "type": "fx",
        "description": (
            "Dub delay desk: feedback path through a moving band-pass, with L/R offset. "
            "X throws feedback toward self-oscillation. Y is the return cutoff. Touch is "
            "a one-shot full-wet echo throw."
        ),
        "ja": (
            "ダブ机のフィードバック・ディレイです。帰還にバンドパス、L/Rをずらします。"
            "Xはフィードバック投げ、Yは帰還のカットオフ。タッチで一発フルウェット投げです。"
        ),
        "params": [
            ("SEND", 0, 1023, 280, NONE, 0, 0, "x", "Feedback throw, into self-oscillation"),
            ("TONE", 0, 1023, 420, NONE, 0, 0, "y", "Band-pass cutoff in the return"),
            ("MIX", 0, 1000, 450, PCT, 1, 1, "depth", "Send / wet level"),
            ("TIME", 0, 1023, 500, NONE, 0, 0, "none", "Delay time, tempo-synced"),
            ("SPRD", 0, 1023, 220, NONE, 0, 0, "none", "L/R delay offset"),
        ],
    },
    {
        "id": "colornse",
        "class": "ColorNse",
        "name": "ColorNse",
        "unit_id": "0x00000028U",
        "type": "synth",
        "description": (
            "DJM Color FX Noise. Touch gates white / pink / brown noise through a "
            "filter that morphs HPF to LPF. X = filter morph, Y = noise color and drive."
        ),
        "ja": (
            "DJM Color FXのNoiseです。タッチでノイズをゲートし、フィルタをHPFからLPFへモーフします。"
            "Xはフィルタ、Yはノイズ色（White→Pink→Brown）とドライブです。"
        ),
        "params": [
            ("FILT", 0, 1023, 700, NONE, 0, 0, "x", "Filter morph: HPF (left) to LPF (right)"),
            ("COLR", 0, 1023, 350, NONE, 0, 0, "y", "Noise color and drive"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Level"),
            ("CUT", 0, 1023, 600, NONE, 0, 0, "none", "Filter cutoff span"),
        ],
    },
    {
        "id": "dubsiren",
        "class": "DubSiren",
        "name": "DubSiren",
        "unit_id": "0x00000029U",
        "type": "synth",
        "description": (
            "Sound-system dub siren. X = pitch, Y = LFO rate and depth. Touch gates; "
            "a flick of X fires a one-shot sweep."
        ),
        "ja": (
            "サウンドシステムのダブサイレンです。Xはピッチ、YはLFOの速さと深さ。"
            "タッチでゲート、Xをフリックすると一発スイープします。"
        ),
        "params": [
            ("PITCH", 0, 1023, 480, NONE, 0, 0, "x", "Siren pitch, low growl to scream"),
            ("LFO", 0, 1023, 420, NONE, 0, 0, "y", "LFO rate and vibrato depth"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Level"),
            ("WAVE", 0, 1, 0, STR, 0, 0, "none", "SQ or SAW"),
            ("ECHO", 0, 1023, 280, NONE, 0, 0, "none", "Internal throw delay"),
        ],
    },
    {
        "id": "snarerush",
        "class": "SnareRush",
        "name": "SnareRush",
        "unit_id": "0x0000002AU",
        "type": "synth",
        "description": (
            "Exponential snare-roll build. Touch starts a rush that doubles in density; "
            "lift ends on a last hit. X = how many bars the rush compresses into, "
            "Y = tight snare to washed noise."
        ),
        "ja": (
            "指数的に密度が上がるスネア・ラッシュです。タッチで開始、離すと最後のヒットで止ます。"
            "Xは2〜8小節を圧縮した長さ、Yはタイトなスネアからウォッシュです。"
        ),
        "params": [
            ("BARS", 0, 1023, 400, NONE, 0, 0, "x", "Rush length, about 2 to 8 bars"),
            ("TONE", 0, 1023, 350, NONE, 0, 0, "y", "Tight snare to washed noise"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Level"),
            ("TUNE", 0, 1023, 480, NONE, 0, 0, "none", "Shell pitch"),
        ],
    },
    {
        "id": "revbass",
        "class": "RevBass",
        "name": "RevBass",
        "unit_id": "0x0000002BU",
        "type": "synth",
        "description": (
            "Hardstyle reverse bass. Touch gates a swell that locks to off-beats. "
            "X = pitch, Y = attack length and drive."
        ),
        "ja": (
            "ハードスタイルのリバース・ベースです。タッチすると裏拍に吸着して膨らみます。"
            "Xはピッチ、Yはアタック長と歪みです。"
        ),
        "params": [
            ("PITCH", 0, 1023, 360, NONE, 0, 0, "x", "Bass pitch"),
            ("ATK", 0, 1023, 550, NONE, 0, 0, "y", "Reverse attack length and drive"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Level"),
            ("SYNC", 0, 1, 1, STR, 0, 0, "none", "FREE or OFFB (off-beat absorb)"),
        ],
    },
    {
        "id": "jungstr",
        "class": "JungStr",
        "name": "JungStr",
        "unit_id": "0x0000002CU",
        "type": "fx",
        "description": (
            "Akai S-series jungle vocal stretch. Extreme time-stretch of AUDIO IN with "
            "metallic grain windows. X = stretch ratio, Y = grain / artifact size. Touch = on."
        ),
        "ja": (
            "Akai Sシリーズを限界まで伸ばしたジャングル・ボーカルです。"
            "Xはストレッチ比、Yはグレイン／窓サイズ（アーティファクト量）。タッチで効果ONです。"
        ),
        "params": [
            ("STRC", 0, 1023, 780, NONE, 0, 0, "x", "Stretch ratio, toward extreme"),
            ("GRAIN", 0, 1023, 420, NONE, 0, 0, "y", "Window size / metallic artifacts"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("HOLD", 0, 1023, 200, NONE, 0, 0, "none", "How long captured audio is reused"),
        ],
    },
    {
        "id": "dredbass",
        "class": "DredBass",
        "name": "DredBass",
        "unit_id": "0x0000002DU",
        "type": "synth",
        "description": (
            "UKG / jungle suction bass. A low oscillator through an LPF whose envelope "
            "opens backward so the note feels reversed. X = pitch, Y = suction depth."
        ),
        "ja": (
            "UKG／ジャングルのサクション低域です。LPF開口エンベロープを逆向きにして"
            "逆再生に聞こえるベースになります。Xはピッチ、Yはサクションの深さです。"
        ),
        "params": [
            ("PITCH", 0, 1023, 300, NONE, 0, 0, "x", "Bass pitch"),
            ("SUCK", 0, 1023, 620, NONE, 0, 0, "y", "LPF suction depth and direction"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Level"),
            ("DEC", 0, 1023, 500, NONE, 0, 0, "none", "How long the mouth stays open"),
        ],
    },
    {
        "id": "halfdbl",
        "class": "HalfDbl",
        "name": "HalfDbl",
        "unit_id": "0x0000002EU",
        "type": "fx",
        "description": (
            "Half / double-time bridge. Always records AUDIO IN. Touch plays the buffer "
            "at 0.5x / 1x / 2x (X snaps). Y = 0 is chipmunk/slow pitch, Y = 1 keeps pitch."
        ),
        "ja": (
            "ハーフタイム／ダブルタイムのテンポ橋です。常時録音し、タッチで0.5x／1x／2x再生します。"
            "Xはレート吸着、Yはピッチ補正（0でチップマンク／スロー、1で音程維持）です。"
        ),
        "params": [
            ("RATE", 0, 1023, 512, NONE, 0, 0, "x", "Playback rate: 0.5, 1, or 2"),
            ("CORR", 0, 1023, 1000, NONE, 0, 0, "y", "Pitch correction amount"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet while bridged"),
            ("BARS", 0, 2, 1, STR, 0, 0, "none", "Recorded window: 1, 2, or 4 bars"),
        ],
    },
    {
        "id": "microgap",
        "class": "MicroGap",
        "name": "MicroGap",
        "unit_id": "0x0000002FU",
        "type": "fx",
        "description": (
            "Vacuum micro-gap before a drop. Touch arms a mute quantized to the next "
            "downbeat. X = gap length (30–200 ms), Y = tiny noise floor so the cut is not digital-dead."
        ),
        "ja": (
            "ドロップ直前の真空ギャップです。タッチで次のダウンビートに量子化して無音を入れます。"
            "Xはギャップ長（30〜200ms）、Yは切断感を和らげる微小ノイズ床です。"
        ),
        "params": [
            ("GAP", 0, 1023, 400, NONE, 0, 0, "x", "Gap length, about 30–200 ms"),
            ("NOIS", 0, 1023, 80, NONE, 0, 0, "y", "Noise floor during the gap"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "How completely the gap mutes"),
            ("QNT", 0, 1, 1, STR, 0, 0, "none", "FREE or BEAT (next downbeat)"),
        ],
    },
    {
        "id": "hatchoke",
        "class": "HatChoke",
        "name": "HatChoke",
        "unit_id": "0x00000030U",
        "type": "synth",
        "description": (
            "TR-909 open/closed hat choke. Touch = open hat; the lower pad or a closed "
            "step chokes it. X = open decay, Y = closed-hat density on a 16th grid."
        ),
        "ja": (
            "TR-909のオープン／クローズド・チョークです。タッチはオープン、下方向やクローズド打で窒息します。"
            "Xはオープンの長さ、Yは16分上のクローズド密度です。"
        ),
        "params": [
            ("OPEN", 0, 1023, 520, NONE, 0, 0, "x", "Open-hat decay"),
            ("CLSD", 0, 1023, 400, NONE, 0, 0, "y", "Closed-hat density on 16ths"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Level"),
            ("TONE", 0, 1023, 620, NONE, 0, 0, "none", "Filter brightness"),
        ],
    },
    {
        "id": "riddimjg",
        "class": "RiddimJg",
        "name": "RiddimJg",
        "unit_id": "0x00000031U",
        "type": "fx",
        "description": (
            "Sound-system riddim juggle. Always records a loop and splits it into version "
            "slices. Touch arms playback; X picks the version, Y is crossfade speed. "
            "No glitch destruction — selector switching."
        ),
        "ja": (
            "サウンドシステムのリディム・ジャグリングです。ループを常時録り、ヴァージョン切片に分けます。"
            "タッチで武装、Xで切片切替、Yはクロスフェード速さ。破壊ではなくセレクター切替です。"
        ),
        "params": [
            ("VER", 0, 1023, 0, NONE, 0, 0, "x", "Which version slice to hear"),
            ("XFD", 0, 1023, 350, NONE, 0, 0, "y", "Crossfade speed between versions"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet while juggling"),
            ("SLCS", 0, 2, 1, STR, 0, 0, "none", "4, 6, or 8 slices"),
            ("BARS", 0, 2, 1, STR, 0, 0, "none", "1, 2, or 4 bar loop"),
        ],
    },
    {
        "id": "pullup",
        "class": "PullUp",
        "name": "PullUp",
        "unit_id": "0x00000032U",
        "type": "fx",
        "description": (
            "Pull-up / rewind. Always records. Touch jumps back to bar 1, a drop cue, "
            "or the latest hot cue. Y sets rewind theater (instant vs short reverse)."
        ),
        "ja": (
            "プルアップ／リワインドです。常時録音し、タッチで1小節目・ドロップ頭・直近キューへ戻します。"
            "Yは演出の長さ（即ジャンプ／短い逆再生）。VINYL BREAKの減速とは別物です。"
        ),
        "params": [
            ("CUE", 0, 1023, 0, NONE, 0, 0, "x", "Jump target: bar 1 / drop / last cue"),
            ("WIND", 0, 1023, 280, NONE, 0, 0, "y", "Rewind theater length"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Wet of the rewind / replay"),
            ("BARS", 0, 2, 2, STR, 0, 0, "none", "Recorded loop: 1, 2, or 4 bars"),
        ],
    },
    {
        "id": "melocap",
        "class": "MeloCap",
        "name": "MeloCap",
        "unit_id": "0x00000033U",
        "type": "fx",
        "description": (
            "DJM Melodic capture. Touch plays the last captured stub at a scale-quantized "
            "pitch (X) and grain length (Y). A long hold recaptures AUDIO IN."
        ),
        "ja": (
            "DJM Melodic系のキャプチャ旋律化です。タッチで最後に掴んだスタブを音程再生します。"
            "Xはスケール量子化ピッチ、Yはグレイン／ループ長。長押しで再キャプチャです。"
        ),
        "params": [
            ("NOTE", 0, 1023, 512, NONE, 0, 0, "x", "Scale-quantized pitch"),
            ("SIZE", 0, 1023, 300, NONE, 0, 0, "y", "Grain / loop length"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Dry/wet"),
            ("SCALE", 0, 2, 0, STR, 0, 0, "none", "MIN, MAJ, or PENT"),
            ("ROOT", 0, 1023, 0, NONE, 0, 0, "none", "Key center around C"),
        ],
    },
    {
        "id": "washout",
        "class": "WashOut",
        "name": "WashOut",
        "unit_id": "0x00000034U",
        "type": "fx",
        "description": (
            "Wash-out blur build. Touch runs an exponential macro of reverb, chorus, and "
            "HPF. X is blur amount, Y is how high the HPF climbs. Lift snaps to dry."
        ),
        "ja": (
            "ウォッシュアウト・ビルドです。タッチ中にリバーブ＋コーラス＋HPFが指数的に開きます。"
            "Xはぼかし量、YはHPFの上がり。離すと一瞬でドライに戻ります。"
        ),
        "params": [
            ("BLUR", 0, 1023, 700, NONE, 0, 0, "x", "Reverb / chorus blur macro"),
            ("HPF", 0, 1023, 650, NONE, 0, 0, "y", "High-pass climb"),
            ("MIX", 0, 1000, 1000, PCT, 1, 1, "depth", "Wet ceiling while held"),
            ("RISE", 0, 1023, 500, NONE, 0, 0, "none", "How fast the macro eases in"),
        ],
    },
]


def main():
    for plugin in BATCH2:
        generate_one(plugin)
        print("generated", plugin["id"])


if __name__ == "__main__":
    main()
