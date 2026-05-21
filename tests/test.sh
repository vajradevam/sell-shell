#!/bin/bash
# sell-shell test suite

SHELL=./sell-shell
PASS=0
FAIL=0
TMPDIR=/tmp/ss_test
mkdir -p "$TMPDIR"

ok()   { PASS=$((PASS+1)); echo "  $(tput setaf 2)PASS$(tput sgr0)  $1"; }
fail() { FAIL=$((FAIL+1)); echo "  $(tput setaf 1)FAIL$(tput sgr0)  $1"; }

strip() { sed -e 's/\x1b\[[0-9;]*m//g' -e 's/\x1b\[[0-9;]*[HK]//g' -e 's/\x1b(b//g' -e 's/\x01//g' -e 's/\x02//g'; }

run() {
    local desc="$1" expect="$2"; shift 2
    local out; out=$(printf "%s\n" "$@" | "$SHELL" 2>&1 | strip | grep -v '^sell-shell' | grep -v '^\s*$')
    echo "$out" | grep -qE "$expect" && ok "$desc" || { fail "$desc"; echo "       want: $expect"; echo "       got: $out"; }
}

runf() {
    local desc="$1" expect="$2" file="$3"
    local out; out=$("$SHELL" "$file" 2>&1 | strip | grep -v '^\s*$' | grep -v '^\[[0-9]')
    echo "$out" | grep -qE "$expect" && ok "$desc" || { fail "$desc"; echo "       want: $expect"; echo "       got: $out"; }
}

echo "=== sell-shell test suite ==="
echo ""

# --- Basics ---
run "echo"         "hello"     "echo hello world"
run "pwd"          "/home"     "pwd"
run "echo args"    "one two"   "echo one two"
run "exit code 0"  "^0$"       "echo ok" "echo $?"

# --- Pipes ---
run "single pipe"       "HELLO"           "echo hello | tr a-z A-Z"
run "pipe w quotes"     "^e$"             "echo 'a b c d e' | tr ' ' '\\n' | sort -r | head -1"
run "triple pipe"       "^4$"             "echo abc | cat | cat | wc -c | tr -d ' '"

# --- Redirections ---
run "> redirect"        "outtest"          "echo outtest > $TMPDIR/stdout.txt" "cat $TMPDIR/stdout.txt"
run ">> append"         "line2"            "echo line1 > $TMPDIR/append.txt" "echo line2 >> $TMPDIR/append.txt" "tail -1 $TMPDIR/append.txt"
run "< redirect"        "intest"           "echo intest > $TMPDIR/stdin.txt" "cat < $TMPDIR/stdin.txt"
run "2> redirect"       "errtest"          "echo errtest 2> $TMPDIR/err.txt" "cat $TMPDIR/err.txt"

# --- Builtins ---
run "cd"               "^/tmp$"           "cd /tmp" "pwd"
run "cd ~"             "/home"            "cd ~" "pwd"
run "export"           "hello"            "export MYVAL=hello" "echo \$MYVAL"
run "unset"            "^$"               "export X=1" "unset X" "echo \$X"
run "echo -n"          "nonl"             "echo -n nonl" "echo"
run "type builtin"     "cd is"            "type cd"
run "type alias"       "aliased"          "alias ll='ls -la'" "type ll"

# --- Variables ---
run "\$VAR"            "varval"           "export V=varval" "echo \$V"
run "\${VAR}"          "braceval"         "export B=braceval" "echo \${B}"
run "dbl quotes var"   "dblval"           'export D=dblval' 'echo "$D"'

# --- Quoting ---
run "sgl quotes space" "a b c"            "echo 'a b c'"
run "dbl quotes space" "x y z"            'echo "x y z"'

# --- Script execution ---
echo "echo script_works" > "$TMPDIR/test_script.sh"
run "source"             "script_works"   "source $TMPDIR/test_script.sh"
runf "script argument"   "script_works"   "$TMPDIR/test_script.sh"

# --- Background job ---
BG_OUT=$(printf "sleep 0.05 &\nsleep 0.1\njobs\n" | "$SHELL" 2>&1 | strip)
echo "$BG_OUT" | grep -qE '\[1\]' && ok "background job" || fail "background job"

# --- History ---
run "history"            "hist_test"      "echo hist_test" "history"

# --- Alias ---
run "alias listing"      "myls"           "alias myls=ls" "alias myls"

# --- Combined ---
run "pipe + redirect"    "TesT"           "echo test > $TMPDIR/combo.txt" "cat $TMPDIR/combo.txt | tr t T"
run "multi pipe file"    "a-b-c"          "echo a b c | tr ' ' '-' > $TMPDIR/multi.txt" "cat $TMPDIR/multi.txt"

# --- if/then/fi ---
cat > "$TMPDIR/if_test.sh" << 'SH'
if test 1 = 1; then
  echo "if_worked"
fi
SH
runf "if/then/fi"        "if_worked"      "$TMPDIR/if_test.sh"

# --- while loop ---
cat > "$TMPDIR/while_test.sh" << 'SH'
export C=1
while test $C -le 3; do
  echo "n:$C"
  export C=4
done
SH
runf "while loop"        "n:1"            "$TMPDIR/while_test.sh"

# --- for loop ---
cat > "$TMPDIR/for_test.sh" << 'SH'
for word in alpha beta gamma; do
  echo "w:$word"
done
SH
runf "for loop"          "w:gamma"        "$TMPDIR/for_test.sh"

echo ""
echo "=== results: $PASS passed, $FAIL failed ==="
exit $FAIL
