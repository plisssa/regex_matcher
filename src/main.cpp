#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
using namespace std;

static unordered_map<string, int> cmap;
static int nextId = 1;

static int getId(const string& cp)
{
    auto it = cmap.find(cp);
    if (it != cmap.end()) {
        return it->second;
    }
    int id = nextId++;
    cmap[cp] = id;
    return id;
}

static bool nextCp(const string& s, size_t& i, string& cp)
{
    if (i >= s.size()) {
        return false;
    }
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t len = 1;
    if ((c & 0x80u) == 0u) {
        len = 1;
    } else if ((c & 0xe0u) == 0xc0u) {
        len = 2;
    } else if ((c & 0xf0u) == 0xe0u) {
        len = 3;
    } else if ((c & 0xf8u) == 0xf0u) {
        len = 4;
    }
    if (i + len > s.size()) {
        len = 1;
    }
    cp = s.substr(i, len);
    i += len;
    return true;
}

static vector<int> transformText(const string& in)
{
    vector<int> result;
    size_t i = 0;
    string cp;
    while (nextCp(in, i, cp)) {
        result.push_back(getId(cp));
    }
    return result;
}

enum StateType {
    ST_CHAR,
    ST_SPLIT,
    ST_JMP,
    ST_MATCH
};

struct State {
    int type;
    int ch;
    int out1;
    int out2;
};

struct PatchRef {
    int state;
    int slot;
};

struct Frag {
    int start;
    vector<PatchRef> out;
};

// Per-thread scratch state for the Pike VM. Kept out of Engine so that engines
// stay const and can be shared across worker threads without data races.
struct Scratch {
    vector<int> seen;
    int seenTag = 0;
    vector<int> current;
    vector<int> next;
};

struct Engine {
    vector<State> states;
    vector<string> tokens;
    int tokenPos = 0;
    int startState = -1;
    int matchState = -1;
    bool bad = false;
    bool nullable = false;
    unordered_set<int> firstChars;

    int addState(int type, int ch = 0, int out1 = -1, int out2 = -1)
    {
        states.push_back({ type, ch, out1, out2 });
        return static_cast<int>(states.size()) - 1;
    }

    static vector<PatchRef> onePatch(int state, int slot)
    {
        return vector<PatchRef> { { state, slot } };
    }

    void patch(const vector<PatchRef>& refs, int target)
    {
        for (const PatchRef& ref : refs) {
            if (ref.slot == 1) {
                states[ref.state].out1 = target;
            } else {
                states[ref.state].out2 = target;
            }
        }
    }

    static vector<PatchRef> appendPatch(vector<PatchRef> left, const vector<PatchRef>& right)
    {
        left.insert(left.end(), right.begin(), right.end());
        return left;
    }

    Frag literal(int ch)
    {
        int state = addState(ST_CHAR, ch);
        return { state, onePatch(state, 1) };
    }

    Frag epsilon()
    {
        int state = addState(ST_JMP);
        return { state, onePatch(state, 1) };
    }

    Frag concatenate(Frag left, Frag right)
    {
        patch(left.out, right.start);
        return { left.start, right.out };
    }

    Frag alternate(Frag left, Frag right)
    {
        int state = addState(ST_SPLIT, 0, left.start, right.start);
        return { state, appendPatch(left.out, right.out) };
    }

    Frag repeatStar(Frag inner)
    {
        int entry = addState(ST_SPLIT, 0, inner.start, -1); // out1 = body, out2 = exit
        int loop  = addState(ST_SPLIT, 0, inner.start, -1); // out1 = body, out2 = exit
        patch(inner.out, loop);                             // after body -> loop decision
        return { entry, appendPatch(onePatch(entry, 2), onePatch(loop, 2)) };
    }

    void tokenize(const string& pattern)
    {
        size_t i = 0;
        string cp;

        while (nextCp(pattern, i, cp)) {
            if (cp.size() == 1 && cp[0] == '\\') {
                string escaped;
                if (!nextCp(pattern, i, escaped)) {
                    bad = true;
                    return;
                }
                tokens.push_back(string("L") + escaped);
            } else if (cp.size() == 1 && (cp[0] == '(' || cp[0] == ')' || cp[0] == '|' || cp[0] == '*')) {
                tokens.push_back(cp);
            } else {
                tokens.push_back(string("L") + cp);
            }
        }
    }

    bool at(const string& token) const
    {
        return tokenPos < static_cast<int>(tokens.size()) && tokens[tokenPos] == token;
    }

    bool finished() const
    {
        return tokenPos >= static_cast<int>(tokens.size());
    }

    Frag parseExpr()
    {
        Frag result = parseConcat();
        while (!bad && at("|")) {
            ++tokenPos;
            Frag right = parseConcat();
            result = alternate(result, right);
        }
        return result;
    }

    Frag parseConcat()
    {
        bool hasAny = false;
        Frag result = epsilon();

        while (!bad && !finished() && !at(")") && !at("|")) {
            Frag part = parseRepeat();
            if (!hasAny) {
                result = part;
                hasAny = true;
            } else {
                result = concatenate(result, part);
            }
        }

        return result;
    }

    Frag parseRepeat()
    {
        if (at("*") || at(")") || at("|") || finished()) {
            bad = true;
            return epsilon();
        }

        Frag result = parseAtom();
        if (!bad && at("*")) {
            ++tokenPos;
            result = repeatStar(result);
            if (at("*")) {
                bad = true;
            }
        }
        return result;
    }

    Frag parseAtom()
    {
        if (at("(")) {
            ++tokenPos;
            Frag result = parseExpr();
            if (!at(")")) {
                bad = true;
                return result;
            }
            ++tokenPos;
            return result;
        }

        if (!finished() && !tokens[tokenPos].empty() && tokens[tokenPos][0] == 'L') {
            string cp = tokens[tokenPos].substr(1);
            ++tokenPos;
            return literal(getId(cp));
        }

        bad = true;
        return epsilon();
    }

    bool compile(const string& pattern)
    {
        tokenize(pattern);
        if (bad) {
            return false;
        }

        Frag root = parseExpr();
        if (bad || !finished()) {
            return false;
        }

        matchState = addState(ST_MATCH);
        patch(root.out, matchState);
        startState = root.start;
        computeStartInfo();
        return true;
    }

    void addThread(Scratch& sc, vector<int>& list, int state) const
    {
        if (state < 0 || sc.seen[state] == sc.seenTag) {
            return;
        }
        sc.seen[state] = sc.seenTag;

        const State& current = states[state];
        if (current.type == ST_SPLIT) {
            addThread(sc, list, current.out1);
            addThread(sc, list, current.out2);
        } else if (current.type == ST_JMP) {
            addThread(sc, list, current.out1);
        } else {
            list.push_back(state);
        }
    }


    void computeStartInfo()
    {
        Scratch sc;
        sc.seen.assign(states.size(), 0);
        sc.seenTag = 1;
        vector<int> list;
        addThread(sc, list, startState);
        for (int state : list) {
            const State& current = states[state];
            if (current.type == ST_MATCH) {
                nullable = true;
            } else if (current.type == ST_CHAR) {
                firstChars.insert(current.ch);
            }
        }
    }

    // Pike VM: leftmost-first greedy NFA simulation, matching Perl/Python `re`
    // semantics (as used by the reference implementation via re.findall).
    //
    // Threads in `current` are kept in priority order (highest priority first;
    // this order is produced by addThread, which follows SPLIT out1 before out2,
    // so the first alternative of `|` and the "keep consuming" branch of `*`
    // have higher priority). When a MATCH thread is reached we record the match
    // and CUT all lower-priority threads (break). Threads with higher priority
    // that already advanced into `next` keep running, so a higher-priority match
    // (and, for greedy operators, a longer one) overrides a shorter one.
    //
    // Returns the end position of the chosen match starting at `start`, or -1 if
    // there is no acceptable match. When `allowEmpty` is false, an empty match
    // (end == start) is not recorded.
    int findMatchEnd(const vector<int>& text, int start, bool allowEmpty, Scratch& sc) const
    {
        const int size = static_cast<int>(text.size());
        sc.current.clear();
        int pos = start;
        int candidate = -1;

        ++sc.seenTag;
        if (sc.seenTag == INT_MAX) {
            fill(sc.seen.begin(), sc.seen.end(), 0);
            sc.seenTag = 1;
        }
        addThread(sc, sc.current, startState);

        while (true) {
            sc.next.clear();
            ++sc.seenTag;
            if (sc.seenTag == INT_MAX) {
                fill(sc.seen.begin(), sc.seen.end(), 0);
                sc.seenTag = 1;
            }

            const int ch = (pos < size) ? text[pos] : -1;
            for (int state : sc.current) {
                const State& cur = states[state];
                if (cur.type == ST_MATCH) {
                    if (allowEmpty || pos > start) {
                        candidate = pos;
                        break; // accepted a match: cut lower-priority threads
                    }
                    continue;
                }
                if (cur.type == ST_CHAR && pos < size && cur.ch == ch) {
                    addThread(sc, sc.next, cur.out1);
                }
            }

            if (pos >= size || sc.next.empty()) {
                return candidate;
            }

            sc.current.swap(sc.next);
            ++pos;
        }
    }

    int countLine(const vector<int>& text, Scratch& sc) const
    {
        int answer = 0;
        int pos = 0;
        bool allowEmpty = true;
        const int size = static_cast<int>(text.size());

        while (pos <= size) {
            if (!nullable && allowEmpty) {
                if (pos >= size || firstChars.find(text[pos]) == firstChars.end()) {
                    ++pos;
                    allowEmpty = true;
                    continue;
                }
            }

            int end = findMatchEnd(text, pos, allowEmpty, sc);
            if (end >= 0) {
                ++answer;
                if (end == pos) {
                    allowEmpty = false;
                } else {
                    pos = end;
                    allowEmpty = true;
                }
            } else {
                ++pos;
                allowEmpty = true;
            }
        }

        return answer;
    }
};

static bool isStripChar(unsigned char c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\f';
}

static string stripLine(const string& line)
{
    size_t left = 0;
    size_t right = line.size();

    while (left < right && isStripChar(static_cast<unsigned char>(line[left]))) {
        ++left;
    }

    while (right > left && isStripChar(static_cast<unsigned char>(line[right - 1]))) {
        --right;
    }

    return line.substr(left, right - left);
}

int main(int argc, char** argv)
{
    ios::sync_with_stdio(false);

    if (argc != 4) {
        return 1;
    }

    ifstream regexInput(argv[1]);
    ifstream textInput(argv[2]);
    ofstream output(argv[3]);

    if (!regexInput || !textInput || !output) {
        return 1;
    }

    vector<Engine> engines;
    vector<char> valid;
    string line;

    while (getline(regexInput, line)) {
        line = stripLine(line);
        if (line.empty()) {
            continue;
        }
        Engine engine;
        bool ok = engine.compile(line);
        valid.push_back(ok ? 1 : 0);
        engines.push_back(std::move(engine));
    }

    // Scratch.seen is indexed by state id within a single engine; one buffer per
    // thread is reused across engines, so it must fit the largest NFA.
    size_t maxStates = 1;
    for (const Engine& e : engines) {
        maxStates = max(maxStates, e.states.size());
    }

    // getId mutates the global codepoint map, so transform every line up front,
    // single-threaded, before any worker starts. The matching phase never calls
    // getId, so engines and the global map are read-only while threads run.
    vector<vector<int>> texts;
    while (getline(textInput, line)) {
        line = stripLine(line);
        if (line.empty()) {
            continue;
        }
        texts.push_back(transformText(line));
    }

    const size_t numLines = texts.size();
    const size_t numEngines = engines.size();
    vector<string> outputs(numLines);

    // The (line x regex) grid is embarrassingly parallel. Workers pull line
    // indices off a shared cursor for dynamic load balancing across cores, each
    // formatting its line's full output into outputs[idx]; lines are then written
    // in order, so the result is byte-identical to the sequential version.
    auto worker = [&](size_t /*tid*/, std::atomic<size_t>* cursor) {
        Scratch sc;
        sc.seen.assign(maxStates, 0);
        sc.seenTag = 0;
        while (true) {
            size_t idx = cursor->fetch_add(1);
            if (idx >= numLines) {
                break;
            }
            const vector<int>& text = texts[idx];
            string& out = outputs[idx];
            out.clear();
            for (size_t i = 0; i < numEngines; ++i) {
                if (i != 0) {
                    out.push_back(' ');
                }
                if (valid[i]) {
                    out += std::to_string(engines[i].countLine(text, sc));
                } else {
                    out += "-1";
                }
            }
        }
    };

    std::atomic<size_t> cursor { 0 };

    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        hw = 1;
    }
    size_t nThreads = std::min<size_t>(hw, numLines == 0 ? 1 : numLines);

    if (nThreads <= 1) {
        worker(0, &cursor);
    } else {
        vector<std::thread> pool;
        pool.reserve(nThreads);
        for (size_t t = 0; t < nThreads; ++t) {
            pool.emplace_back(worker, t, &cursor);
        }
        for (std::thread& th : pool) {
            th.join();
        }
    }

    for (size_t i = 0; i < numLines; ++i) {
        output << outputs[i] << '\n';
    }

    return 0;
}
