# Ichiyanagi V1 言語仕様書

**コードネーム**: Tega-Neson
**バージョン**: 1 (`ich-ver=1`)
**ステータス**: ドラフト（策定中）
**ライセンス**: Apache License 2.0
**Copyright**: (C) 2026 nyan<(nyan4)

---

## 1. 概要

Ichiyanagi V1（コードネーム: Tega-Neson）は、Linux（x86-64）上でネイティブ動作するコンパイル言語である。
コンパイラ自体は C で実装し、AST から x86-64 アセンブリ（System V AMD64 ABI）を自前生成、Linux 純正のアセンブラ／リンカ（`as`/`ld`）に引き渡すことで ELF ネイティブ実行ファイルを生成する。

Windows / macOS への対応は将来のバージョンで着手する展望であり、V1 の実装スコープには含まれない（詳細は 13.2, 13.5 を参照）。CPU アーキテクチャについても、V1 の `codegen.c` は x86-64 のみを対象とし、ARM64 対応は将来課題である（詳細は 13.6 を参照）。

- **実装言語**: C
- **出力**: x86-64 アセンブリ（自前生成）→ Linux の `as`/`ld`
- **対応 OS**: Linux のみ。Windows / macOS は将来対応
- **対応 CPU アーキテクチャ**: x86-64 のみ。ARM64 は将来対応（なお `interp.c` によるインタプリタ実行自体はアーキテクチャ非依存であり、ARM64 Linux 上でも通常のビルドでそのまま動作する）
- **メモリ管理**: `var`/`pvar` はスコープベースの自動管理。ヒープ確保は `hold`/`drop` で手動管理
- **型システム**: 動的型（Python 的に自由。型注釈は現時点でなし）

---

## 2. ファイル構造

### 2.1 ファイル宣言（必須）

すべての Ichiyanagi ソースファイルは、先頭に以下の宣言が必要。無い場合はコンパイルエラーとなる。

```
!"Ichiyanagi Code"
```

表記ゆれとして以下も許容する:

```
!"ichiyanagi code"
!"iciyanagi code"
```

### 2.2 バージョン指定

```
spc var ich-ver=1
```

`spc var`（special var）でコンパイラ／言語バージョンを指定する。指定バージョンと実行環境のバージョンが一致しない場合はエラーとなり実行できない。
将来的に「バージョン統合版」をリリースし、これに対応した際はすべてのバージョン指定を許容できるようにする予定。

---

## 3. コメント

### 3.1 単一行コメント

`\` から行末までがコメントになる。

```
\ これはコメントです
```

### 3.2 複数行コメント

`\"` で開始し、`"` で終了するペア構文。

```
\"
これは
複数行の
コメントです
"
```

複数行コメントの中身は実行される処理ではなく単なるテキストとして扱われる。そのため、レキサーは `\"` の後に最初に現れる `"` をそのままコメントの終端とみなす（ネストはしない）。コメント本文の中で `"` という文字そのものを書きたい場合は、そこでコメントが終了してしまう点に注意する。

---

## 4. モジュール読み込み（join）

```
join <module>              \ モジュール名で読み込み
join <module> as alias     \ エイリアス付き
join "/path/to/file"       \ パス直接指定
join "/path/to/file" as alias  \ パス指定 + エイリアス
```

---

## 5. 変数

### 5.1 グローバル変数（var）

ファイルのトップレベル（関数の外）で宣言する。宣言したファイル内のどの関数からも参照・変更可能なグローバル変数となる。

```
var x            \ 宣言のみ（後から値を入れられる。terminal.input 等で使用）
var y = 10       \ 宣言と同時に代入
```

### 5.2 ローカル変数（pvar）

関数の内部で宣言する。その関数専用のローカル変数であり、外部（他の関数やグローバルスコープ）からは見えない。

```
pvar result           \ 宣言のみ
pvar count = 0         \ 宣言と同時に代入
```

**重要**: `var` と `pvar` は名前が同じでも完全に別の存在（別次元の変数）である。同名であっても自動的には連結されない。

### 5.3 グローバル変数の値を関数内に持ち込む

グローバル変数の値をローカル変数として使いたい場合は、明示的に値を代入して持ち込む。

```
pvar y = var x    \ グローバル変数 x の値をコピーしてローカル変数 y を作る
```

これは値のコピーであり、参照ではない。持ち込んだ後に `y` を変更しても元の `x` には影響しない。

### 5.4 型

型注釈は存在しない。Python のように変数は自由にどの型の値でも保持できる（動的型）。

### 5.5 ヒープメモリ（hold / drop）

`var` / `pvar` は通常のスタック変数であり、スコープを抜けると自動的に解放される。
一方、関数の呼び出しを超えてデータを保持し続けたい場合は、`hold` でヒープ上にメモリを確保し、不要になったら `drop` で明示的に解放する（C の `malloc` / `free` に相当）。

```
hold p = 100    \ 100バイト確保して p という名前をつける
...
drop p          \ p を解放
```

`hold` で確保した領域は `drop` するまで存在し続ける。`drop` し忘れた場合はメモリリークとなる（V1では自動検出・警告の仕組みは持たない）。

---

## 6. 関数

### 6.1 定義

```
名前(引数1, 引数2, ...) exec:
    ...
end
```

引数はオプション（`main()` のように空でもよい）。

```
func(a, b) exec:
    ...
end

main() exec:
    ...
end
```

### 6.2 戻り値

`ret` キーワードで値を返す。返す値の型に制約はない。

```
ret result
```

---

## 7. 制御構文

すべてのブロックは `exec:` で開始し、`end` で終了する（統一）。

### 7.1 if / else

```
if a gt b exec:
    result = a
else exec:
    result = b
end
```

### 7.2 while

```
while result gt 0 exec:
    terminal.print(result)
    result = result - 1
end
```

---

## 8. 演算子

### 8.1 比較演算子

英単語短縮形を使用する（記号は使わない）。

| 演算子 | 意味 |
|---|---|
| `gt` | より大きい（>） |
| `lt` | より小さい（<） |
| `eq` | 等しい（==） |
| `neq` | 等しくない（!=） |
| `ge` | 以上（>=） |
| `le` | 以下（<=） |

### 8.2 算術演算子

記号をそのまま使用する。`+` は数値の加算に加え、文字列の連結にも使用する。

| 演算子 | 意味 |
|---|---|
| `+` | 加算 / 文字列連結 |
| `-` | 減算 |
| `*` | 乗算 |
| `/` | 除算 |

### 8.3 論理演算子

英単語を使用する（比較演算子 `gt`/`lt` と同じノリ）。

| 演算子 | 意味 |
|---|---|
| `not` | 否定 |
| `and` | かつ |
| `or` | または |

### 8.4 演算子の優先順位

上から順に結合が強い（先に評価される）。同順位のものは左から右へ評価する。

1. `( )` 括弧
2. `*` `/`
3. `+` `-`（文字列連結の `+` も同順位）
4. `gt` `lt` `eq` `neq` `ge` `le`（比較演算子、すべて同順位）
5. `not`
6. `and`
7. `or`

---

## 9. エラー処理

### 9.1 pick / error

エラーが発生しうる処理は `pick:` ブロックで囲む。ブロック内でエラーが発生すると、その時点で処理は中断され（残りの `pick` 内の文は実行されない）、`error:` ブロックへ制御が移る。エラーが発生しなければ `error:` ブロックは無視され、そのまま `end` の次へ進む。

```
pick:
    result = 10 / 0
error:
    terminal.print("エラーが発生しました")
end
```

### 9.2 エラー情報の取得

`error:` ブロック内では、以下の特殊変数（`spc var`）でエラーの詳細にアクセスできる。

| 変数 | 内容 |
|---|---|
| `spc var err-msg` | エラーメッセージ |
| `spc var err-code` | エラー種別コード |

```
pick:
    result = 10 / 0
error:
    terminal.print(spc var err-msg)
    terminal.print(spc var err-code)
end
```

---

## 10. 文の区切り

文の区切りは**改行のみ**。文末にカンマやセミコロンは不要。

```
result = a
terminal.print(result)
result = result - 1
```

---

### 10.1 文字列

ダブルクォート `"..."` とシングルクォート `'...'` の両方が使用できる。ただし開始と終了で異なるクォートを混在させることはできない（`"abc'` のような書き方は不可）。

```
var a = "Hello"
var b = 'World'
var c = a + " " + b   \ 文字列連結
```

---

### 10.2 配列（リスト）

関数風の構文で定義する。要素アクセスは角括弧を使う。操作系はメソッド風（`.`）で統一する。

```
var nums = list(1, 2, 3)
nums[0]        \ 0番目の要素にアクセス
nums.push(4)   \ 末尾に要素を追加
nums.pop()     \ 末尾の要素を削除し、その値を返す
nums.len()     \ 要素数を取得
```

---

## 11. サンプルコード

```
!"Ichiyanagi Code"

spc var ich-ver=1

var x

join <example>

func(a, b) exec:
    pvar result

    if a gt b exec:
        result = a
    else exec:
        result = b
    end

    while result gt 0 exec:
        terminal.print(result)
        result = result - 1
    end

    ret result
end

main() exec:
    pvar y = var x
    terminal.print("Hello world!")
    terminal.input("Do you like teganeson?\nAnswer:", answer=x)

    pick:
        result = 10 / 0
    error:
        terminal.print(spc var err-msg)
    end
end
```

---

## 12. 標準モジュール

V1 では以下の4モジュールを標準として提供する（今後追加検討あり）。

### 12.1 terminal（入出力）

| 関数 | 内容 |
|---|---|
| `terminal.print(値)` | 改行ありで出力 |
| `terminal.printn(値)` | 改行なしで出力 |
| `terminal.input(プロンプト, answer=変数)` | 入力を受け取り、指定した変数に格納 |

### 12.2 math（数学関数）

| 関数 | 内容 |
|---|---|
| `math.sqrt(x)` | 平方根 |
| `math.pow(x, y)` | べき乗 |
| `math.abs(x)` | 絶対値 |
| `math.floor(x)` | 切り捨て |
| `math.ceil(x)` | 切り上げ |

### 12.3 string（文字列操作）

| 関数 | 内容 |
|---|---|
| `string.len(s)` | 文字列の長さ |
| `string.upper(s)` | 大文字変換 |
| `string.lower(s)` | 小文字変換 |
| `string.split(s, 区切り文字)` | 分割し配列で返す |
| `string.trim(s)` | 前後の空白を除去 |

### 12.4 file（ファイル入出力）

| 関数 | 内容 |
|---|---|
| `file.read(path)` | ファイルを読み込み内容を返す |
| `file.write(path, 内容)` | ファイルに内容を書き込む |

### 12.5 time（時刻・タイマー）

| 関数 | 内容 |
|---|---|
| `time.now()` | 現在時刻を取得 |
| `time.sleep(秒)` | 指定秒数だけ処理を停止 |

### 12.6 sys（コマンド引数・環境変数・終了コード）

| 関数 | 内容 |
|---|---|
| `sys.args()` | コマンドライン引数を配列で取得 |
| `sys.env(名前)` | 環境変数を取得 |
| `sys.exit(コード)` | 指定した終了コードでプログラムを終了 |

### 12.7 array（固定長配列操作）

`list` は可変長（push/pop 対応）だが、`array` はサイズ固定の配列を扱う。

| 関数 | 内容 |
|---|---|
| `array(サイズ)` | 指定サイズの固定長配列を作成 |
| `array.len()` | 配列の長さを取得 |

---

## 13. コンパイラ内部設計

### 13.1 パイプライン

伝統的な3段階構成を採用する。

```
ソースコード(.ich)
    │
    ▼
[1] 字句解析 (Lexer)   ソース文字列 → トークン列
    │
    ▼
[2] 構文解析 (Parser)  トークン列 → AST（抽象構文木）
    │
    ▼
[3] コード生成 (Codegen)  AST → x86-64 アセンブリ（Linux / System V AMD64 ABI）
    │
    ▼
Linux のアセンブラ/リンカ (as/ld)
    │
    ▼
ELF ネイティブ実行ファイル
```

中間表現（IR）は V1 では持たず、AST から直接アセンブリを生成する。最適化パスなどは将来のバージョンで検討する。

現行の V1 実装（`interp.c`）は AST ツリーウォーク評価器であり、上記パイプラインのうち [1][2] を経て AST を直接実行している。ネイティブコード生成（[3] の `codegen.c`）は次段階の実装課題である。

### 13.2 ターゲットプラットフォーム方針（重要）

**V1 のネイティブコード生成は Linux（x86-64 / System V AMD64 ABI / ELF）のみを対象とする。**

Windows と macOS は当面の実装対象から完全に除外し、将来のバージョンで別途対応する。理由は以下の通り。

- Linux は `syscall` 命令でカーネルへ直接処理を依頼でき、自己完結したアセンブリ出力だけで `write` や `exit` 等の基本動作を実現できる。ABI は System V AMD64（引数は `rdi, rsi, rdx, r10, r8, r9` の順）で一貫している。
- Windows は事情が異なり、syscall を直接叩く経路が正規にサポートされていない。`kernel32.dll` の Win32 API（`WriteConsoleA` / `ExitProcess` 等）を `extern` 宣言し、Microsoft x64 ABI（引数は `rcx, rdx, r8, r9`、シャドウスペースが必要）に従って呼び出し、`link.exe` 等でリンクする必要がある。ABI・呼び出し規約・実行ファイル形式（PE-COFF）のすべてが Linux と別物であり、単純な分岐では吸収できない。
- macOS は開発機を保有していないため、当面は検証不可能。将来的に対応する場合も Mach-O・Apple の syscall 規約への個別対応が必要になる。

これらの理由から、**まず Linux 向けの `codegen.c` を完成させ、実際に動くネイティブバイナリを生成できる状態を確立することを最優先とする。** Windows 対応はこの幹が完成した後、`codegen.c` から分岐する形で別途着手する（詳細は 13.5 を参照）。

### 13.3 ビルドシステム

Makefile と CMake の両対応でプロジェクトを構成する。

### 13.4 想定プロジェクト構成（案）

```
ichiyanagi/
├── (readme.mdやNOTICE、LICENSE)
├── CMakeLists.txt
├── Makefile
├── src/
│   ├── main.c        \ エントリポイント
│   ├── lexer.c/.h     \ 字句解析
│   ├── parser.c/.h    \ 構文解析(AST生成)
│   ├── ast.c/.h        \ AST定義
│   ├── interp.c/.h     \ ASTツリーウォーク評価器（現行実装）
│   ├── codegen.c/.h    \ アセンブリコード生成（Linux/x86-64、次段階の実装対象）
│   └── error.c/.h      \ pick/error のエラー処理ランタイム
├── include/
├── tests/
└── docs/
    └── SPEC.md
```

### 13.5 将来のマルチプラットフォーム対応（予定）

Windows / macOS への対応は、Linux 向け `codegen.c` が安定した後に着手する将来課題として明記しておく。

- **Windows**: `codegen_windows.c`（案）としてコード生成部を分岐させる。Microsoft x64 ABI に従い、`kernel32.dll` 等の Win32 API を `extern` 宣言して呼び出す形とする。実行ファイル形式は PE-COFF、アセンブラ/リンカは `masm`/`link.exe` 相当を想定する。
- **macOS**: 開発機がないため対応時期は未定。対応する場合は Mach-O 形式と Apple 独自の syscall 番号体系（BSD 系）を踏まえた実装が必要になる。

ファイル分割の指針としては、OS 共通の AST 走査ロジックと、OS 固有の命令列生成・呼び出し規約を分離し、`codegen.c`（共通）から `codegen_linux.c` / `codegen_windows.c`（OS 別）を呼び分ける構成を想定する。ただし現時点では Linux 実装が存在しないため、この分割は Linux 版完成後に確定させる。

### 13.6 将来の CPU アーキテクチャ対応（予定）

上記の OS 対応とは別軸の課題として、**CPU アーキテクチャの対応も x86-64 に加えて ARM64（AArch64）を将来的にサポートする。**

- 現行の `interp.c`（AST ツリーウォーク評価器）は素の C コードであり、アーキテクチャに依存しない。そのため x86-64 / ARM64 いずれの Linux 環境でも、通常の C コンパイラでビルドすればそのまま動作する（この対応は既に事実上完了している）。
- 一方、`codegen.c` が生成するネイティブアセンブリはアーキテクチャ依存である。1つのアセンブリ出力が両アーキテクチャで動くことはあり得ないため、`codegen.c` は必ずどちらか一方のターゲット向けに出力を選択する必要がある。
- 将来的には `codegen_x86_64.c` / `codegen_arm64.c` のように、命令セットごとにコード生成部を分岐させる方針とする。Linux 上であれば OS 側の ABI 差異（System V 系）は共通のため、CPU アーキテクチャの分岐は OS 分岐よりも独立して進めやすいと見込まれる。
- 優先順位としては、まず x86-64 Linux 向け `codegen.c` を完成させることを最優先とし、ARM64 Linux 向けコード生成はその後の拡張課題とする。

#### 13.6.1 ターゲット選択方式

`codegen.c`（および将来の `codegen_arm64.c`）がどちらのアーキテクチャ向けに出力するかの決定方式は、以下の2段階で導入する。

- **V1（確定）**: ターゲットは「`ichiyanagi` コンパイラ自身がビルドされた CPU アーキテクチャ」と常に一致させる。すなわち、x86-64 機で `ichiyanagi` をビルドすればそのコンパイラは x86-64 向けのネイティブバイナリのみを生成し、ARM64 機でビルドすれば ARM64 向けのネイティブバイナリのみを生成する。クロスコンパイル（あるアーキテクチャの機械上で別アーキテクチャ向けのバイナリを生成すること）は V1 では非対応とする。実装上は、ビルド時にホストアーキテクチャを検出し（コンパイラマクロ `__x86_64__` / `__aarch64__` 等を利用）、対応する `codegen_*.c` を選択してリンクする形を想定する。
- **将来（予定）**: `ichiyanagi --target=arm64 file.ich` のようなコマンドライン引数でターゲットを明示指定できるようにし、ホストと異なるアーキテクチャ向けのクロスコンパイルにも対応する。この際は `codegen_x86_64.c` と `codegen_arm64.c` の両方を常に `ichiyanagi` 本体にリンクしておき、`--target` オプションの値に応じて呼び分ける構成になる見込み。

---

## 14. ライセンス・著作権表記

Ichiyanagi プロジェクト（コンパイラ本体、標準モジュール、関連ツール一式）は **Apache License 2.0** の下で配布する。

### 14.1 著作権者

```
Copyright 2026 nyan<(nyan4)
```

### 14.2 ソースファイルへのヘッダー記載

すべての `.c` / `.h` ソースファイルの先頭には、以下の Apache License 標準ボイラープレートを記載する。

```c
/*
 * Copyright 2026 nyan<(nyan4)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
```

### 14.3 リポジトリ直下のファイル

- `LICENSE.txt` … Apache License 2.0 全文を配置する
- `NOTICE`（任意）… サードパーティ由来のコードや謝辞を記載する場合はここに追加する
- readme.md 

### 14.4 改変時の注意

Apache License 2.0 第4条(b)により、Ichiyanagi のソースファイルを改変して再配布する場合は、変更した旨を明示するコメントを当該ファイルに追加する必要がある。

---

## 15. 未確定事項（今後の検討課題）

現時点で仕様上の大きな未確定事項はなし。実装を進める中で細部（各標準モジュール関数の厳密な引数仕様、エラーコード体系など）を随時詰めていく。
