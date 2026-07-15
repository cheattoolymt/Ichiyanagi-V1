#!/usr/bin/env bash
# Copyright 2026 nyan<(nyan4)
# Licensed under the Apache License, Version 2.0
#
# Ichiyanagi V1 テストランナー。
# examples/*.ich と tests/cases を実行し、期待出力と比較する。

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/ichiyanagi"
CASES="$ROOT/tests/cases"

pass=0
fail=0

if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found. Run 'make' first."
    exit 1
fi

run_case() {
    local name="$1"
    local src="$2"
    local expected="$3"
    local stdin_data="${4:-}"

    local actual
    if [ -n "$stdin_data" ]; then
        actual="$(printf '%s' "$stdin_data" | "$BIN" "$src" 2>&1)"
    else
        actual="$("$BIN" "$src" 2>&1)"
    fi

    if [ "$actual" == "$expected" ]; then
        echo "  PASS: $name"
        pass=$((pass+1))
    else
        echo "  FAIL: $name"
        echo "    --- expected ---"
        echo "$expected" | sed 's/^/    /'
        echo "    --- actual ---"
        echo "$actual" | sed 's/^/    /'
        fail=$((fail+1))
    fi
}

echo "== Ichiyanagi V1 test suite =="

# 1. hello
run_case "hello" "$ROOT/examples/hello.ich" \
"Hello world!
Ichiyanagi V1 (Tega-Neson) へようこそ"

# 2. 算術と優先順位
cat > /tmp/ich_arith.ich <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    terminal.print(2 + 3 * 4)
    terminal.print((2 + 3) * 4)
    terminal.print(10 / 2 - 1)
    terminal.print(2 * 3 + 4 * 5)
end
EOF
run_case "arithmetic" /tmp/ich_arith.ich \
"14
20
4
26"

# 3. 比較・論理
cat > /tmp/ich_logic.ich <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    if 5 gt 3 exec:
        terminal.print("gt-ok")
    end
    if 2 le 2 and 3 ge 3 exec:
        terminal.print("andge-ok")
    end
    if not 0 exec:
        terminal.print("not-ok")
    end
    if 1 eq 2 or 1 neq 2 exec:
        terminal.print("or-ok")
    end
end
EOF
run_case "logic" /tmp/ich_logic.ich \
"gt-ok
andge-ok
not-ok
or-ok"

# 4. 文字列連結
cat > /tmp/ich_str.ich <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    var a = "Hello"
    var b = 'World'
    terminal.print(a + " " + b)
    terminal.print(string.upper("abc") + string.lower("XYZ"))
end
EOF
run_case "string-concat" /tmp/ich_str.ich \
"Hello World
ABCxyz"

# 5. 制御構文とwhile
cat > /tmp/ich_ctrl.ich <<'EOF'
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
run_case "while-loop" /tmp/ich_ctrl.ich \
"3
2
1"

# 6. 関数と再帰
cat > /tmp/ich_func.ich <<'EOF'
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
run_case "recursion" /tmp/ich_func.ich \
"120
1"

# 7. list / array
cat > /tmp/ich_list.ich <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    var xs = list(1, 2, 3)
    xs.push(4)
    terminal.print(xs.len())
    terminal.print(xs[0])
    terminal.print(xs.pop())
    terminal.print(xs.len())
    var a = array(2)
    a[0] = 9
    a[1] = 8
    terminal.print(a[0] + a[1])
end
EOF
run_case "list-array" /tmp/ich_list.ich \
"4
1
4
3
17"

# 8. pick / error (ゼロ除算)
cat > /tmp/ich_err.ich <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
main() exec:
    pick:
        pvar r = 10 / 0
        terminal.print("skipped")
    error:
        terminal.print(spc var err-msg)
        terminal.print(spc var err-code)
    end
    terminal.print("after")
end
EOF
run_case "pick-error" /tmp/ich_err.ich \
"division by zero
1
after"

# 9. 表記ゆれ許容
cat > /tmp/ich_variant.ich <<'EOF'
!"iciyanagi code"
spc var ich-ver=1
main() exec:
    terminal.print("variant-ok")
end
EOF
run_case "decl-variant" /tmp/ich_variant.ich "variant-ok"

# 10. terminal.input (answer=)
cat > /tmp/ich_input.ich <<'EOF'
!"Ichiyanagi Code"
spc var ich-ver=1
var ans
main() exec:
    terminal.input("Q:", answer=ans)
    terminal.print("you said: " + var ans)
end
EOF
run_case "terminal-input" /tmp/ich_input.ich \
"Q:you said: teganeson" "teganeson"

echo ""
echo "== Result: $pass passed, $fail failed =="
[ "$fail" -eq 0 ]
