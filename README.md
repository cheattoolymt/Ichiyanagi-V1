# Ichiyanagi V1

**コードネーム**: Tega-Neson / **バージョン**: 1 (`ich-ver=1`) / **ライセンス**: Apache License 2.0

自作のプログラミング言語 **Ichiyanagi**（いちやなぎ）の V1 実装です。
コンパイラ／ランタイムは C で実装されており、Windows / macOS / Linux でユニバーサルに
動作することを目標としています。

> 仕様書全文は [`docs/SPEC.md`](docs/SPEC.md) を参照してください。

---

## 特徴

- **動的型**（Python 的に型注釈なしでどんな値でも保持できる）
- **英単語ベースの演算子**（`gt` / `lt` / `eq` / `and` / `or` など）
- **`exec:` 〜 `end` で統一されたブロック構文**
- **`pick:` / `error:` による例外処理**
- **`var`（グローバル）/ `pvar`（ローカル）の明確な分離**
- **`hold` / `drop` による手動ヒープ管理**
- **7 つの標準モジュール**（terminal / math / string / file / time / sys, および `list` / `array` 組み込み）

---

## ビルド

### Make を使う場合

```bash
make          # build/ichiyanagi を生成
make test     # テストスイートを実行
make clean    # 生成物を削除
```

### CMake を使う場合

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

必要環境: C11 対応の C コンパイラ（gcc / clang / MSVC）と `libm`。

---

## 使い方

```bash
# プログラムを実行（インタプリタ）
./build/ichiyanagi examples/hello.ich

# プログラムに引数を渡す（sys.args() で取得できる）
./build/ichiyanagi examples/stdlib.ich foo bar

# ★ ネイティブ実行ファイルへコンパイル（x86-64 Linux / ELF）
./build/ichiyanagi --compile examples/compile_demo.ich -o compile_demo
./compile_demo

# ★ x86-64 アセンブリだけを出力（デバッグ／学習用）
./build/ichiyanagi --emit-asm examples/hello.ich -o hello.s

# トークンをダンプ（デバッグ用）
./build/ichiyanagi --tokens examples/hello.ich

# バージョン表示 / ヘルプ
./build/ichiyanagi --version
./build/ichiyanagi --help
```

`-o` を省略するとソース名から出力名を決めます（`--compile` は拡張子を除いた名前、
`--emit-asm` は `.s`）。`--compile` / `--emit-asm` の短縮形として `-c` / `-S` も使えます。

---

## ネイティブコンパイル（codegen）

`--compile` は AST から x86-64 アセンブリ（System V AMD64 ABI）を**自前生成**し、
Linux 純正の `as` / `ld` に引き渡して **libc 非依存の静的 ELF 実行ファイル**を生成します
（仕様書 13 章のパイプライン `[1] Lexer → [2] Parser → [3] Codegen → as/ld → ELF` を実装）。

### 対応環境

- **OS**: Linux のみ / **CPU**: x86-64 のみ（仕様 13.2 / 13.6 の V1 方針どおり）
- ビルド／実行時に `as` と `ld`（binutils）が必要です

### コンパイル可能な言語サブセット（V1）

| 機能 | 対応 |
|---|---|
| 数値（64bit 整数モデル） | ✅ |
| 算術 `+ - * /`（整数除算） | ✅ |
| 比較 `gt lt eq neq ge le` | ✅ |
| 論理 `not and or`（短絡評価） | ✅ |
| `var` / `pvar` 宣言・代入、`var x` 参照 | ✅ |
| 関数定義・呼び出し・再帰・`ret` | ✅（引数は最大 6 個） |
| `if` / `else` / `while` | ✅ |
| `terminal.print` / `terminal.printn`（数値・文字列リテラル） | ✅ |
| `list` / `array`、`hold` / `drop`、`pick` / `error`、上記以外のモジュール | ❌（インタプリタを利用） |

> codegen は整数モデルのため、`10 / 3` は `3`、真偽値は `1`/`0` として出力されます
> （インタプリタはそれぞれ `3.33333` / `true`・`false`）。非対応の構文を `--compile`
> すると、その旨のエラーを表示してコンパイルを中止します（インタプリタでは全機能が動作します）。

`main()` の `ret` の値がプロセスの終了コードになります。

---

## Hello, World

```
!"Ichiyanagi Code"

spc var ich-ver=1

main() exec:
    terminal.print("Hello world!")
end
```

- すべてのソースは先頭に `!"Ichiyanagi Code"` が必要です
  （`!"ichiyanagi code"` / `!"iciyanagi code"` の表記ゆれも許容）。
- `spc var ich-ver=1` で言語バージョンを指定します。

---

## 言語ガイド（概要）

### 変数

```
var g = 10        \ グローバル変数（トップレベル）
pvar l = 5        \ ローカル変数（関数内）
pvar copy = var g \ グローバル値をローカルにコピー（値渡し）
```

### 関数

```
add(a, b) exec:
    ret a + b
end
```

### 制御構文

```
if a gt b exec:
    result = a
else exec:
    result = b
end

while n gt 0 exec:
    terminal.print(n)
    n = n - 1
end
```

### 演算子

| 種別 | 演算子 |
|---|---|
| 比較 | `gt` `lt` `eq` `neq` `ge` `le` |
| 算術 | `+`（加算/文字列連結） `-` `*` `/` |
| 論理 | `not` `and` `or` |

優先順位（強い順）: `( )` → `* /` → `+ -` → 比較 → `not` → `and` → `or`

### コメント

```
\ 単一行コメント

\"
複数行
コメント
"
```

### コレクション

```
var xs = list(1, 2, 3)   \ 可変長リスト
xs.push(4)
xs.pop()
xs.len()
xs[0]

var a = array(3)         \ 固定長配列
a[0] = 10
a.len()
```

### エラー処理

```
pick:
    result = 10 / 0
error:
    terminal.print(spc var err-msg)   \ エラーメッセージ
    terminal.print(spc var err-code)  \ エラー種別コード
end
```

### 手動ヒープ管理

```
hold buffer = 256   \ 256 バイト確保
drop buffer         \ 解放
```

---

## 標準モジュール

| モジュール | 主な関数 |
|---|---|
| `terminal` | `print` / `printn` / `input(prompt, answer=var)` |
| `math` | `sqrt` / `pow` / `abs` / `floor` / `ceil` |
| `string` | `len` / `upper` / `lower` / `split` / `trim` |
| `file` | `read` / `write` |
| `time` | `now` / `sleep` |
| `sys` | `args` / `env` / `exit` |
| 組み込み | `list(...)` / `array(size)` |

---

## プロジェクト構成

```
Ichiyanagi-V1/
├── LICENSE                 Apache License 2.0 全文
├── NOTICE                  著作権・謝辞
├── README.md
├── Makefile
├── CMakeLists.txt
├── src/
│   ├── main.c              エントリポイント（CLI）
│   ├── lexer.c/.h          字句解析
│   ├── parser.c/.h         構文解析（AST 生成）
│   ├── ast.c/.h            AST 定義
│   ├── value.c/.h          動的値の表現
│   ├── interp.c/.h          AST ツリーウォーク評価器
│   ├── codegen.c/.h         x86-64 ネイティブコード生成（Linux/ELF）
│   ├── module.c/.h          標準モジュール実装
│   ├── error.c/.h          pick/error ランタイム
│   └── common.c/.h          共通ユーティリティ
├── examples/               サンプルプログラム（*.ich）
├── tests/                  テストスイート
└── docs/
    └── SPEC.md             言語仕様書
```

### 実装メモ

Ichiyanagi V1 は **2 つの実行経路**を持ちます。

1. **AST ツリーウォーク評価器（`interp.c`）** — 全 OS / 全アーキテクチャで動作し、
   言語機能をすべて実装しています（既定の実行方法）。
2. **ネイティブコンパイラ（`codegen.c`）** — 仕様書 13 章のとおり AST から x86-64
   アセンブリ（System V AMD64 ABI）を自前生成し、`as` / `ld` で ELF 実行ファイルを
   生成します（`--compile`）。V1 では Linux / x86-64 のみ、かつコンパイル可能な言語
   サブセット（上記「ネイティブコンパイル」章の表を参照）が対象です。

インタプリタはリファレンス実装として全機能を担保し、コンパイラは仕様 13 章の
「AST → アセンブリ → ネイティブ ELF」パイプラインを実際に動くバイナリとして実現します。
Windows / macOS 対応や ARM64 向け `codegen_arm64.c`、`list`/`array` などの動的値の
コンパイル対応は今後のバージョンの拡張課題です（仕様 13.5 / 13.6）。

---

## ライセンス

Ichiyanagi プロジェクト（コンパイラ本体、標準モジュール、関連ツール一式）は
**Apache License 2.0** の下で配布します。

```
Copyright 2026 nyan<(nyan4)
```
