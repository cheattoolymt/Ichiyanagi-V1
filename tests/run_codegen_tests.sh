#!/usr/bin/env bash
# Copyright 2026 nyan<(nyan4)
# Licensed under the Apache License, Version 2.0
#
# Ichiyanagi V1 codegen (ネイティブコンパイラ) テストランナー。
# プログラムを --compile でネイティブ ELF にし、実行結果と終了コードを
# 期待値および (可能なら) インタプリタ実行と比較する。
#
# codegen が対応するのは x86-64 Linux のみ。非対応環境では skip する。

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/ichiyanagi"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
skip=0

if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found. Run 'make' first."
    exit 1
fi

# codegen は x86-64 Linux のみ対象。それ以外は全 skip。
arch="$(uname -m)"
os="$(uname -s)"
if [ "$os" != "Linux" ] || [ "$arch" != "x86_64" ]; then
    echo "== Ichiyanagi V1 codegen test suite =="
    echo "  SKIP: codegen targets x86-64 Linux only (host: $os/$arch)"
    echo "== Result: 0 passed, 0 failed, all skipped =="
    exit 0
fi
if ! command -v as >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1; then
    echo "== Ichiyanagi V1 codegen test suite =="
    echo "  SKIP: 'as'/'ld' toolchain not available"
    echo "== Result: 0 passed, 0 failed, all skipped =="
    exit 0
fi

# run_cc <name> <src> <expected_stdout> <expected_exit> [stdin]
run_cc() {
    local name="$1"
    local src="$2"
    local expected="$3"
    local expected_exit="$4"

    local exe="$TMP/$name.bin"
    if ! "$BIN" --compile "$src" -o "$exe" >/dev/null 2>"$TMP/$name.err"; then
        echo "  FAIL: $name (compile failed)"
        sed 's/^/    /' "$TMP/$name.err"
        fail=$((fail+1))
        return
    fi

    local actual actual_exit
    actual="$("$exe" 2>&1)"
    actual_exit=$?

    if [ "$actual" == "$expected" ] && [ "$actual_exit" -eq "$expected_exit" ]; then
        echo "  PASS: $name"
        pass=$((pass+1))
    else
        echo "  FAIL: $name"
        echo "    --- expected (exit=$expected_exit) ---"; echo "$expected" | sed 's/^/    /'
        echo "    --- actual   (exit=$actual_exit) ---"; echo "$actual" | sed 's/^/    /'
        fail=$((fail+1))
    fi
}

# assert_unsupported <name> <src> : codegen が非対応で失敗し、バイナリを残さないこと
assert_unsupported() {
    local name="$1"
    local src="$2"
    local exe="$TMP/$name.bin"
    if "$BIN" --compile "$src" -o "$exe" >/dev/null 2>&1; then
        echo "  FAIL: $name (expected codegen to reject, but it succeeded)"
        fail=$((fail+1))
    elif [ -e "$exe" ]; then
        echo "  FAIL: $name (rejected but left a binary behind)"
        fail=$((fail+1))
    else
        echo "  PASS: $name (correctly rejected)"
        pass=$((pass+1))
    fi
}

echo "== Ichiyanagi V1 codegen test suite =="

# 1. hello (文字列出力)
run_cc "cc-hello" "$ROOT/examples/hello.ich" \
"Hello world!
Ichiyanagi V1 (Tega-Neson) へようこそ" 0

# 2. 算術・優先順位 (整数モデル)
cat > "$TMP/arith.ich" <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    terminal.print(2 + 3 * 4)
    terminal.print((2 + 3) * 4)
    terminal.print(10 / 3)
    terminal.print(0 - 7)
end
EOF
run_cc "cc-arith" "$TMP/arith.ich" \
"14
20
3
-7" 0

# 3. 再帰 (factorial)
cat > "$TMP/fact.ich" <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
fact(n) exec:
    if n le 1 exec:
        ret 1
    end
    ret n * fact(n - 1)
end
main() exec:
    terminal.print(fact(5))
    terminal.print(fact(0))
end
EOF
run_cc "cc-recursion" "$TMP/fact.ich" \
"120
1" 0

# 4. while ループ
cat > "$TMP/while.ich" <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    pvar i = 3
    while i gt 0 exec:
        terminal.print(i)
        i = i - 1
    end
end
EOF
run_cc "cc-while" "$TMP/while.ich" \
"3
2
1" 0

# 5. グローバル変数 + if/else + 論理 (真偽は 0/1)
cat > "$TMP/global.ich" <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
var counter = 0
bump() exec:
    counter = counter + 10
    ret counter
end
main() exec:
    terminal.print(bump())
    terminal.print(bump())
    terminal.print(var counter)
    if counter ge 20 exec:
        terminal.print(1 eq 1 and 2 neq 3)
    else exec:
        terminal.print(999)
    end
end
EOF
run_cc "cc-global-if" "$TMP/global.ich" \
"10
20
20
1" 0

# 6. 終了コード (main の ret を exit code に)
cat > "$TMP/exit.ich" <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    terminal.print("bye")
    ret 42
end
EOF
run_cc "cc-exit-code" "$TMP/exit.ich" "bye" 42

# 7. 総合デモ (examples/compile_demo.ich)
run_cc "cc-demo" "$ROOT/examples/compile_demo.ich" \
"Ichiyanagi native codegen demo
120
3628800
5050
14
20
-42
range-ok" 0

# 8. 非対応構文は拒否される (list/array)
cat > "$TMP/list.ich" <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    var xs = list(1, 2, 3)
    terminal.print(xs.len())
end
EOF
assert_unsupported "cc-reject-list" "$TMP/list.ich"

# 9. 非対応構文は拒否される (pick/error)
cat > "$TMP/pick.ich" <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    pick:
        pvar r = 10 / 0
    error:
        terminal.print("err")
    end
end
EOF
assert_unsupported "cc-reject-pick" "$TMP/pick.ich"

echo ""
echo "== Result: $pass passed, $fail failed, $skip skipped =="
[ "$fail" -eq 0 ]
