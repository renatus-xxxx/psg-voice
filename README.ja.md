# psg-voice

日本語 | [English](README.md)

MSX1のPSG（AY-3-8910）の音量レジスタを4ビットDACとして使う音声再生の、スライドとC言語による解説。ぼいち（KAKAO SOFT）『PSGで音声を再生する仕組み』（『KIRU!』技術解説）を引用・解説しています。

## ブラウザで再生

- [WebMSXで再生（ROM、MSX turboR）](https://webmsx.org/?MACHINE=MSXTRJ&P=NODISK&CARTRIDGE1_URL=https://raw.githubusercontent.com/renatus-xxxx/psg-voice/main/dist/PSGVOICE-LITE.ROM)

ROMは似非DOS（Ese-DOS／DISK ROM Lite）で自動起動し、AUTOEXEC.BATから
`PSGVOICE.COM` を実行します。起動時校正→サンプル音声の再生→測定値の
表示→`A>` プロンプトへ復帰、までが自動で進みます。
注意：このリポジトリがPrivateの間は、上のWebMSXリンクはROMを匿名で
取得できません。Publicにすればそのまま使えます。同じROMはローカルの
WebMSXで最後まで動作確認済みです（`images/webmsx-verified.png`：
校正値 716サンプル/フレーム、cost 83、W=24）。

## 内容

- `slides/` — 原典を順に追い直した解説スライド14枚（発表者ノート付き）。
  DACの発想、データの作り方、447サイクルの予算、ウェイト式、起動時校正。
- `src/wav2psg.c` — PC用ツール（C99）。WAV→ピーク正規化→8000Hzへ
  間引き→4倍＋頭打ち→0〜15段へ量子化し、`voice_data.h` を出力。
- `src/psgvoice.c` — MSX-DOS／似非DOS用コマンド `PSGVOICE.COM`
  （z88dk。アセンブラはサイクル数を数える再生ループだけ）。
- `sample/` — 同梱サンプル音声「あいうえお。こんにちは、ピーエスジー
  です。」2026年に本プロジェクト用にAI合成した音声で、第三者の録音は
  使っていないため著作権の問題はありません。母音5つ＋短い挨拶は
  4ビット再生でも聞き取りやすいため選びました。
- `dist/` — ビルド済み `PSGVOICE.COM`（35,096バイト）と
  `PSGVOICE-LITE.ROM`（512KB、DISK ROM Liteカートリッジ、ASCII 8K）。

## ビルド

```sh
make -f Makefile.linux        # PSGVOICE.COM
make -f Makefile.linux rom    # + PSGVOICE-LITE.ROM（似非DOS）
```

z88dkが必要です（このワークスペースでは nightly 20261116 に固定）。
ROMイメージ用に `tools/tablacus/` へ Tablacus DISK ROM Lite
（MIT、自前パッチ版。詳細は同梱README）を収録しています。

## 校正方式についての注記

本サンプルではJIFFYではなく、割り込み停止中にVDPの垂直帰線フラグを
数える方式にしています。WebMSXではBIOS割り込み処理自体が高くつき
（PSGへの入出力が重い）、JIFFY方式の実測では約22サイクル/サンプルの
偏りが出たためです。詳細は `src/psgvoice.c` のコメントにあります。

## クレジット

- 手法と原典：ぼいち（KAKAO SOFT）『PSGで音声を再生する仕組み
  ― MSX1で「むねん…」を喋らせるまで ―』MSX版『KIRU!』技術解説、
  (c) 2024-2026。https://voichworks.booth.pm/items/9205845
  本リポジトリは独立した解説と再実装で、原典そのものの再配布は
  含みません。
- DISK ROM Lite／似非DOS：Gaku（Tablacus）氏、MITライセンス。
