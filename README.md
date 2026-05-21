# sell-shell

A Unix shell written in C with job control, pipes, redirections, tab completion, scripting, and a customizable prompt.

## Build

```sh
make
```

Requires: `gcc`, `libreadline`

## Run

```sh
./sell-shell              # interactive mode
./sell-shell script.ssh   # script mode
```

## Features

### Pipelines
```sh
echo hello | tr a-z A-Z | rev
```

### Redirections
```sh
echo log > out.txt
echo more >> out.txt
sort < input.txt
make 2> errors.log
make &> everything.log
```

### Builtins

| Command | Description |
|---------|-------------|
| `cd [dir]` | Change directory (`-`, `~` supported) |
| `exit [n]` | Exit with optional code |
| `export [VAR=val]` | Set/list environment variables |
| `unset VAR` | Remove environment variable |
| `alias [name=val]` | List/set command aliases |
| `unalias name` | Remove alias |
| `echo [-n] [args]` | Print arguments |
| `pwd` | Print working directory |
| `type cmd` | Show command type (builtin/alias/PATH) |
| `source file` | Execute script file |
| `history` | Show command history |
| `jobs` | List background jobs |
| `fg [%n]` | Bring job to foreground |
| `bg [%n]` | Send job to background |

### Job Control
```
sleep 10 &
jobs
fg %1
bg %1
```

- Background jobs with `&`
- `SIGTSTP` (Ctrl-Z) to suspend
- `SIGINT` (Ctrl-C) handled gracefully

### Custom Prompt

Set `PS1` with escape sequences:

| Sequence | Expands to |
|----------|------------|
| `\u` | Username |
| `\h` | Hostname (short) |
| `\H` | Full hostname |
| `\w` | Working directory (`~` for home) |
| `\W` | Basename of working directory |
| `\d` | Date (Wed May 21) |
| `\t` | Time (HH:MM:SS) |
| `\n` | Newline |
| `\s` | Shell name |
| `\$` | `#` if root, `$` otherwise |
| `\e` | ESC (for ANSI colors) |
| `\[` | Start non-printing characters |
| `\]` | End non-printing characters |

ANSIColor prompt example:
```sh
export PS1='\[\e[32m\]\u@\h\[\e[0m\]:\[\e[34m\]\w\[\e[0m\]\$ '
```

### Tab Completion

- Tab completes commands from `PATH`
- Tab completes file paths
- Tab completes `$VAR` names after `$`
- Tab completes builtins and aliases

### Scripting

Run scripts with `source` or as an argument:

```sh
./sell-shell myscript.ssh
```

Supports `if`/`else`/`elif`/`fi`, `while`/`do`/`done`, `for`/`in`/`do`/`done`:

```sh
# if example
if echo hello && true; then
  echo "it worked"
fi

# for example
for word in one two three; do
  echo $word
done

# while example
count=0
while test $count -lt 3; do
  echo $count
  count=$((count + 1))
done
```

### History

- Persistent history saved to `~/.sell-shell_history`
- Up/down arrow to navigate
- `history` command to list

## Project Structure

```
├── Makefile              Build system
├── README.md
├── src/
│   ├── main.c            Entry point, readline loop, signal handling
│   ├── parser.c          Quote-aware tokenizer, alias expansion, pipeline builder
│   ├── executor.c        Fork/exec, pipe setup, redirect application, builtins
│   ├── jobs.c            Job tracking, fg/bg, SIGTSTP handling
│   ├── prompt.c          PS1 expansion with colors, git branch display
│   ├── completion.c      Tab completion for commands, files, vars
│   ├── script.c          Script execution with if/while/for control flow
│   └── sell-shell.h      Common types and declarations
├── scripts/
│   ├── demo_interactive.ssh
│   └── demo_scripting.ssh
└── tests/
    └── test.sh
```
