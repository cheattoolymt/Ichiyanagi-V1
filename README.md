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
# プログラムを実行
./build/ichiyanagi examples/hello.ich

# プログラムに引数を渡す（sys.args() で取得できる）
./build/ichiyanagi examples/stdlib.ich foo bar

# トークンをダンプ（デバッグ用）
./build/ichiyanagi --tokens examples/hello.ich

# バージョン表示 / ヘルプ
./build/ichiyanagi --version
./build/ichiyanagi --help
```

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
│   ├── interp.c/.h         AST ツリーウォーク評価器
│   ├── module.c/.h         標準モジュール実装
│   ├── error.c/.h          pick/error ランタイム
│   └── common.c/.h         共通ユーティリティ
├── examples/               サンプルプログラム（*.ich）
├── tests/                  テストスイート
└── docs/
    └── SPEC.md             言語仕様書
```

### 実装メモ

仕様書 13 章では「AST から x86-64 アセンブリを直接生成する」構成が示されています。
この V1 実装は、まず全 OS で確実に動作する **AST ツリーウォーク評価器** として言語機能を
完全実装したものです（lexer → parser → AST → evaluator のパイプラインは仕様どおり）。
ネイティブコード生成バックエンド（Linux ELF から着手）は今後のバージョンで
`codegen.c` として追加していく予定です。

---

## ライセンス

Ichiyanagi プロジェクト（コンパイラ本体、標準モジュール、関連ツール一式）は
**Apache License 2.0** の下で配布します。

```
Copyright 2026 nyan<(nyan4)
```
