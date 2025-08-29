# MiniShell (C++20)

A small Linux-like shell built in C++ that supports pipelines, I/O redirection, background execution, history, minimal job control, and resource limits — implemented with POSIX primitives.

## Features
- **Built-ins:** `cd`, `pwd`, `echo`, `exit`, `export`, `unset`, `env`, `jobs`, `fg`, `bg`, `limit`
- **Pipelines:** `cmd1 | cmd2 | cmd3`
- **I/O redirection:** `<`, `>`, `>>`
- **Background execution:** `&` (e.g., `sleep 5 &`)
- **History & line editing:** GNU Readline (↑/↓, Ctrl+R)
- **Command separators:** `;` (e.g., `echo one ; echo two`)
- **Signals:** Ctrl-C interrupts foreground jobs without killing the shell
- **Startup config:** reads `~/.minishellrc` on launch

## Prerequisites
```bash
sudo apt update
sudo apt install -y build-essential g++ make libreadline-dev
```

## Build and Run
```bash
make run
```

## Examples
```bash
# basics
pwd
echo hello > out.txt
cat out.txt | tr a-z A-Z

# pipelines & redirection
ls -l | grep '^d' | wc -l
tr a-z A-Z < out.txt >> OUT.txt

# background & jobs
sleep 10 &
jobs
fg %1

# environment
export GREETING=hello
env | grep GREETING
unset GREETING

# resource limits (CPU seconds / address space MB)
limit cpu 1
python3 -c "while True: pass"            # gets killed by CPU limit
limit mem 64
python3 -c "x='x'*(200*1024*1024)"       # MemoryError under 64 MB

# separators
echo one ; echo two ; ls -1 | wc -l
```

## How It Works 

- **REPL → Tokenize → Parse → Execute.**  
  Input is tokenized (words, quotes, operators), parsed into a pipeline of stages (with any `<`, `>`, `>>`, `&`), then executed.

### Pipelines
- For **N** stages, create **N−1** pipes.
- `fork()` per stage; `dup2()` pipe ends to `STDIN`/`STDOUT`; `execvp()` the program.

### Redirection
- `open()` the file; `dup2()` to `STDIN`/`STDOUT` before `execvp()`.

### Built-ins
- If a built-in runs **alone** (no redirection/pipes), execute **in-process**.
- If part of a **pipeline/redirection**, run the built-in **in the child** and flush (`write()`/`std::cout.flush()`) to avoid stdio buffering issues.

### Background (`&`)
- Skip `waitpid()` and return the prompt immediately.
- Track the last stage’s PID in a minimal **jobs table**.
- `jobs` lists: `[id] pid  Running/Done  <cmd>`  
- `fg %id` waits for that job in the foreground.  
- `bg %id` sends `SIGCONT` (useful if you manually `STOP` a job).

### Signals
- Shell installs a `SIGINT` handler so **Ctrl-C** prints a newline and leaves the shell alive.
- Children restore default handlers so **Ctrl-C** interrupts foreground jobs.

### Limits
- `limit cpu <sec>` sets `RLIMIT_CPU`.  
- `limit mem <MB>` sets `RLIMIT_AS`.

### Startup RC
- On launch, the shell reads lines from `~/.minishellrc` and executes them.  
  Lines starting with `#` are treated as comments and ignored.

## Project Structure

minishell/
├─ src/
│  └─ main.cpp
├─ include/         # (optional headers)
├─ bin/             # build output
├─ Makefile
└─ README.md


## Make targets

make run — build & run bin/minishell

make — build only

make clean — remove bin/

## Known limitations (v1)

- No logical operators && / || (use ; to chain).

- No glob expansion (e.g., *.txt), no heredoc <<.

- Minimal job control (basic jobs/fg/bg; no terminal process group/Ctrl-Z integration).

- No advanced shell features (subshells, command substitution, arrays, functions, aliasing, etc.)

## Troubleshooting

- pwd/echo seem silent: fixed by using unbuffered write() in built-ins and/or std::cout.setf(std::ios::unitbuf).

- cat out.txt | tr a-z A-Z says syntax error: ensure your tokenizer collapses accidental || into | and your parser accepts a stage with either argv or redirection before |.

- python3 - <<'PY' fails: heredoc << not supported in v1; use python3 -c "<code>" or a pipe.

- On WSL: build/run in your Linux home (~/minishell) for best I/O performance; copy to Windows only for backup.