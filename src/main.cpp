#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <readline/readline.h>
#include <readline/history.h>

using namespace std;

// ---------- Tokenizer (quotes, < > >> | &, collapses accidental "||") ----------
vector<string> tokenize(const string &line) {
    vector<string> tokens;
    string cur;
    bool in_quotes = false;
    char q = 0;

    auto push_cur = [&](){
        if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
    };

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\r') continue;

        if (in_quotes) {
            if (c == q) { in_quotes = false; }
            else {
                if (c == '\\' && i + 1 < line.size() && line[i+1] == q) { cur.push_back(q); ++i; }
                else cur.push_back(c);
            }
            continue;
        }
        if (c == '\'' || c == '\"') { in_quotes = true; q = c; continue; }

        if (isspace(static_cast<unsigned char>(c))) { push_cur(); continue; }

        if (c == '>') {
            push_cur();
            if (i + 1 < line.size() && line[i+1] == '>') { tokens.push_back(">>"); ++i; }
            else tokens.push_back(">");
            continue;
        }
        if (c == '|') {
            push_cur();
            while (i + 1 < line.size() && line[i+1] == '|') ++i; // collapse
            tokens.push_back("|");
            continue;
        }
        if (c == '<' || c == '&') { push_cur(); tokens.push_back(string(1, c)); continue; }

        cur.push_back(c);
    }
    push_cur();
    return tokens;
}

// ---------- Split by ';' (outside quotes) ----------
vector<string> split_commands(const string& line) {
    vector<string> parts;
    string cur;
    bool in_quotes = false; char q = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == q) in_quotes = false;
            cur.push_back(c);
            continue;
        }
        if (c == '\'' || c == '\"') { in_quotes = true; q = c; cur.push_back(c); continue; }
        if (c == ';') {
            // end segment
            // trim
            auto s = cur;
            // trim spaces
            auto l = s.find_first_not_of(" \t\r\n");
            auto r = s.find_last_not_of(" \t\r\n");
            if (l != string::npos) parts.push_back(s.substr(l, r - l + 1));
            cur.clear();
        } else cur.push_back(c);
    }
    // last segment
    auto s = cur;
    auto l = s.find_first_not_of(" \t\r\n");
    auto r = s.find_last_not_of(" \t\r\n");
    if (l != string::npos) parts.push_back(s.substr(l, r - l + 1));
    return parts;
}

struct Command {
    vector<string> argv;
    string infile, outfile;
    bool append = false;
};

struct Parsed {
    vector<Command> pipeline; // split by |
    bool background = false;
};

// ---------- Parser ----------
Parsed parse(const vector<string>& tok) {
    Parsed P;
    Command cur;

    auto stage_is_empty = [&](){
        return cur.argv.empty() && cur.infile.empty() && cur.outfile.empty();
    };

    for (size_t i = 0; i < tok.size(); ++i) {
        const string& t = tok[i];
        if (t == "|") {
            if (stage_is_empty()) throw runtime_error("syntax error near '|'");
            P.pipeline.push_back(cur);
            cur = Command{};
            continue;
        }
        if (t == "&") {
            if (i != tok.size() - 1) throw runtime_error("misplaced '&'");
            P.background = true;
            continue;
        }
        if (t == "<") {
            if (i + 1 >= tok.size()) throw runtime_error("missing filename after '<'");
            cur.infile = tok[++i];
            continue;
        }
        if (t == ">" || t == ">>") {
            if (i + 1 >= tok.size()) throw runtime_error("missing filename after '>'");
            cur.outfile = tok[++i];
            cur.append = (t == ">>");
            continue;
        }
        cur.argv.push_back(t);
    }
    if (!stage_is_empty()) P.pipeline.push_back(cur);
    return P;
}

// ---------- Built-ins & helpers ----------
bool is_builtin(const string& cmd) {
    return cmd=="cd"||cmd=="pwd"||cmd=="echo"||cmd=="exit"||
           cmd=="export"||cmd=="unset"||cmd=="env"||
           cmd=="jobs"||cmd=="fg"||cmd=="bg"||cmd=="limit";
}

int builtin_cd(const vector<string>& a) {
    const char* path = (a.size()>=2) ? a[1].c_str() : getenv("HOME");
    if (!path) { cerr << "cd: HOME not set\n"; return 1; }
    if (chdir(path) != 0) { perror("cd"); return 1; }
    return 0;
}
int builtin_pwd() {
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) cout << buf << "\n";
    else perror("pwd");
    return 0;
}
int builtin_echo(const vector<string>& a) {
    // write() to be safe w.r.t. _exit in child
    string s;
    for (size_t i=1;i<a.size();++i){ if(i>1) s.push_back(' '); s += a[i]; }
    s.push_back('\n');
    ssize_t _ = ::write(STDOUT_FILENO, s.c_str(), s.size()); (void)_;
    return 0;
}
extern "C" char **environ;
int builtin_env() {
    for (char **e = environ; *e; ++e) cout << *e << "\n";
    return 0;
}
int builtin_export(const vector<string>& a) {
    if (a.size() < 2) { cerr << "export KEY=VALUE\n"; return 1; }
    auto pos = a[1].find('=');
    if (pos == string::npos) { cerr << "export: format KEY=VALUE\n"; return 1; }
    string k = a[1].substr(0,pos), v = a[1].substr(pos+1);
    if (setenv(k.c_str(), v.c_str(), 1) != 0) perror("setenv");
    return 0;
}
int builtin_unset(const vector<string>& a) {
    if (a.size() < 2) { cerr << "unset KEY\n"; return 1; }
    if (unsetenv(a[1].c_str()) != 0) perror("unsetenv");
    return 0;
}

// ---------- Jobs (minimal) ----------
struct Job { int id; pid_t pid; string cmd; bool running; };
static vector<Job> g_jobs;
static int g_next_id = 1;

void reap_zombies() {
    int st=0; pid_t p;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
        for (auto &j : g_jobs) if (j.pid == p) j.running = false;
    }
}
int builtin_jobs() {
    reap_zombies();
    for (auto &j : g_jobs)
        cout << "[" << j.id << "] " << j.pid << "  "
             << (j.running ? "Running" : "Done") << "  " << j.cmd << "\n";
    return 0;
}
int builtin_fg(const vector<string>& a) {
    if (a.size() < 2) { cerr << "fg: usage: fg %jobid\n"; return 1; }
    int want = stoi(a[1].substr(a[1][0]=='%'?1:0));
    for (auto &j : g_jobs) if (j.id == want) {
        if (!j.running) { cerr << "fg: job already done\n"; return 1; }
        int st=0; if (waitpid(j.pid, &st, 0) < 0) perror("waitpid");
        j.running = false; return 0;
    }
    cerr << "fg: no such job\n"; return 1;
}
int builtin_bg(const vector<string>& a) {
    if (a.size() < 2) { cerr << "bg: usage: bg %jobid\n"; return 1; }
    int want = stoi(a[1].substr(a[1][0]=='%'?1:0));
    for (auto &j : g_jobs) if (j.id == want) {
        if (j.running) { cerr << "bg: already running\n"; return 1; }
        if (kill(j.pid, SIGCONT) < 0) { perror("kill"); return 1; }
        j.running = true; return 0;
    }
    cerr << "bg: no such job\n"; return 1;
}

// ---------- Limits ----------
int builtin_limit(const vector<string>& a) {
    if (a.size() < 3) { cerr << "limit cpu <sec> | mem <MB>\n"; return 1; }
    if (a[1] == "cpu") {
        rlimit r; r.rlim_cur = r.rlim_max = static_cast<rlim_t>(stoul(a[2]));
        if (setrlimit(RLIMIT_CPU, &r) != 0) perror("setrlimit CPU");
        return 0;
    } else if (a[1] == "mem") {
        size_t mb = stoul(a[2]);
        rlimit r; r.rlim_cur = r.rlim_max = static_cast<rlim_t>(mb * 1024ull * 1024ull);
        if (setrlimit(RLIMIT_AS, &r) != 0) perror("setrlimit AS");
        return 0;
    }
    cerr << "limit cpu <sec> | mem <MB>\n"; return 1;
}

// ---------- Builtin dispatcher ----------
int run_builtin(const vector<string>& args) {
    const string& cmd = args[0];
    if (cmd=="exit") exit(0);
    if (cmd=="cd")   return builtin_cd(args);
    if (cmd=="pwd")  return builtin_pwd();
    if (cmd=="echo") return builtin_echo(args);
    if (cmd=="env")  return builtin_env();
    if (cmd=="export") return builtin_export(args);
    if (cmd=="unset")  return builtin_unset(args);
    if (cmd=="jobs") return builtin_jobs();
    if (cmd=="fg")   return builtin_fg(args);
    if (cmd=="bg")   return builtin_bg(args);
    if (cmd=="limit")return builtin_limit(args);
    return 0;
}

// ---------- Signals ----------
void sigint_handler(int) {
    const char nl = '\n'; ssize_t ignored = write(STDOUT_FILENO, &nl, 1); (void)ignored;
}

// ---------- Exec helpers ----------
vector<char*> build_argv(const vector<string>& v) {
    vector<char*> a; a.reserve(v.size()+1);
    for (auto &s : v) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr); return a;
}
int open_in(const string& p){ int fd=open(p.c_str(),O_RDONLY); if(fd<0) perror(("open < "+p).c_str()); return fd; }
int open_out(const string& p,bool app){ int fd=open(p.c_str(),O_WRONLY|O_CREAT|(app?O_APPEND:O_TRUNC),0644); if(fd<0) perror(("open > "+p).c_str()); return fd; }

// ---------- Pipeline executor ----------
int execute_pipeline(Parsed& P) {
    int n = (int)P.pipeline.size();
    if (n==0) return 0;

    // run builtin in-process if single cmd, no redirs/pipes
    if (n==1 && is_builtin(P.pipeline[0].argv[0])
        && P.pipeline[0].infile.empty() && P.pipeline[0].outfile.empty()) {
        return run_builtin(P.pipeline[0].argv);
    }

    vector<int> pipes(max(0,(n-1)*2), -1);
    for (int i=0;i<n-1;++i) if (pipe(&pipes[2*i])<0){ perror("pipe"); return 1; }

    vector<pid_t> pids; pids.reserve(n);

    // parent ignores SIGINT while managing children
    struct sigaction oldsa{}, sa{};
    sa.sa_handler = sigint_handler; sigemptyset(&sa.sa_mask); sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, &oldsa);

    for (int i=0;i<n;++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            // child
            signal(SIGINT, SIG_DFL);

            if (i>0) dup2(pipes[2*(i-1)], STDIN_FILENO);
            if (i<n-1) dup2(pipes[2*i+1], STDOUT_FILENO);
            for (int fd : pipes) if (fd!=-1) close(fd);

            const auto& C = P.pipeline[i];
            int infd=-1, outfd=-1;
            if (!C.infile.empty()) { infd=open_in(C.infile); if(infd<0) _exit(1); dup2(infd,STDIN_FILENO); close(infd); }
            if (!C.outfile.empty()){ outfd=open_out(C.outfile,C.append); if(outfd<0) _exit(1); dup2(outfd,STDOUT_FILENO); close(outfd); }

            if (is_builtin(C.argv[0])) {
                run_builtin(C.argv);
                cout.flush(); cerr.flush(); fflush(nullptr);
                exit(0);
            } else {
                auto argv = build_argv(C.argv);
                execvp(argv[0], argv.data());
                perror(("execvp "+C.argv[0]).c_str());
                _exit(127);
            }
        } else {
            pids.push_back(pid);
        }
    }

    for (int fd : pipes) if (fd!=-1) close(fd);

    if (P.background) {
        // track last process as the job
        pid_t tracked = pids.back();
        string cmdline;
        for (size_t i=0;i<P.pipeline.size();++i){
            for (auto &t : P.pipeline[i].argv) { if(!cmdline.empty()) cmdline+=' '; cmdline+=t; }
            if (i+1<P.pipeline.size()) cmdline += " |";
        }
        int id = g_next_id++;
        g_jobs.push_back({id, tracked, cmdline, true});
        cout << "[" << id << "] " << tracked << "\n";
        // do not wait
    } else {
        int status=0;
        for (pid_t pid : pids) { int st=0; if (waitpid(pid,&st,0)<0) perror("waitpid"); status=st; }
        // restore handler
        sigaction(SIGINT, &oldsa, nullptr);
        return status;
    }
    // restore for background path too
    sigaction(SIGINT, &oldsa, nullptr);
    return 0;
}

// ---------- Process a single input line ----------
void process_line(const string& line) {
    auto tok = tokenize(line);
    if (tok.empty()) return;
    try {
        auto P = parse(tok);
        if (!P.pipeline.empty()) execute_pipeline(P);
    } catch (const exception& e) {
        cerr << "minishell: " << e.what() << "\n";
    }
}

// ---------- Load ~/.minishellrc ----------
void load_rc() {
    const char* home = getenv("HOME");
    if (!home) return;
    string rc = string(home) + "/.minishellrc";
    ifstream in(rc);
    if (!in.good()) return;
    string line;
    while (getline(in, line)) {
        string s = line;
        auto l = s.find_first_not_of(" \t\r\n");
        if (l == string::npos) continue;
        if (s[l] == '#') continue;
        process_line(s);
    }
}

// ---------- Main REPL (readline) ----------
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
	std::cout.setf(std::ios::unitbuf); // auto-flush after each << operation

    struct sigaction sa{};
    sa.sa_handler = sigint_handler; sigemptyset(&sa.sa_mask); sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);

    load_rc();

    while (true) {
        reap_zombies();

        // Prompt: cwd basename
        char cwd[4096]; string prompt = "minishell$ ";
        if (getcwd(cwd, sizeof(cwd))) {
            string path(cwd); auto pos = path.find_last_of('/');
            string base = (pos==string::npos)? path : path.substr(pos+1);
            prompt = base + " $ ";
        }

        char* raw = readline(prompt.c_str());
        if (!raw) { cout << "\n"; break; }
        string line(raw); free(raw);
        if (line.empty()) continue;

        add_history(line.c_str());

        // support ';' chains
        auto parts = split_commands(line);
        for (auto &seg : parts) process_line(seg);
    }
    return 0;
}
