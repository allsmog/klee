# JSON Output + HTML Coverage Reports Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add JSON test case output, JSON execution summary, and HTML coverage reports to KLEE so results are machine-readable and human-visible.

**Architecture:** Three new output modes added to `KleeHandler` in `tools/klee/main.cpp`, following the existing pattern of `WriteKTests`/`WriteXMLTests`/`WriteCov`. JSON is hand-serialized (no external dependency). HTML is template-based with inline CSS. All data already exists in KLEE — we're just serializing differently.

**Tech Stack:** C++17, KLEE tools layer (`tools/klee/main.cpp`)

---

## File Structure

| File | Responsibility | Change |
|------|---------------|--------|
| `tools/klee/main.cpp` | KleeHandler — all test case output | Add 3 new cl::opt flags, 3 new write methods, wire into processTestCase + main |

---

### Task 1: Add `--write-json-tests` flag and JSON test case output

**Files:**
- Modify: `tools/klee/main.cpp`

- [ ] **Step 1: Add command-line flag** (after `WriteXMLTests` ~line 305):
```cpp
cl::opt<bool> WriteJSONTests("write-json-tests",
    cl::desc("Write test cases as JSON files (default=false)"),
    cl::init(false), cl::cat(TestGenCat));
```

- [ ] **Step 2: Add `writeTestCaseJSON` method to `KleeHandler`** (after `writeTestCaseXML`):
```cpp
void writeTestCaseJSON(bool isError, const char *errorMessage,
                       const char *errorSuffix,
                       const std::vector<std::pair<std::string,
                           std::vector<unsigned char>>> &assignments,
                       unsigned id) {
  auto f = openTestFile("json", id);
  if (!f) return;

  *f << "{\n";
  *f << "  \"testId\": " << id << ",\n";

  if (isError && errorMessage) {
    *f << "  \"error\": {\n";
    *f << "    \"type\": \"" << (errorSuffix ? errorSuffix : "unknown") << "\",\n";
    // Escape quotes in message
    std::string msg(errorMessage);
    for (size_t pos = 0; (pos = msg.find('"', pos)) != std::string::npos; pos += 2)
      msg.insert(pos, "\\");
    *f << "    \"message\": \"" << msg << "\"\n";
    *f << "  },\n";
  } else {
    *f << "  \"error\": null,\n";
  }

  *f << "  \"inputs\": [\n";
  for (size_t i = 0; i < assignments.size(); i++) {
    const auto &a = assignments[i];
    *f << "    {\"name\": \"" << a.first << "\", \"size\": " << a.second.size()
       << ", \"data\": [";
    for (size_t j = 0; j < a.second.size(); j++) {
      if (j) *f << ", ";
      *f << (unsigned)a.second[j];
    }
    *f << "]}";
    if (i + 1 < assignments.size()) *f << ",";
    *f << "\n";
  }
  *f << "  ]\n";
  *f << "}\n";
}
```

- [ ] **Step 3: Wire into `processTestCase`** (after `WriteXMLTests` block ~line 631):
```cpp
if (WriteJSONTests) {
  writeTestCaseJSON(errorMessage != nullptr, errorMessage, errorSuffix,
                    assignments, test_id);
  atLeastOneGenerated = true;
}
```

- [ ] **Step 4: Build and verify:**
```bash
cmake --build build -j$(sysctl -n hw.ncpu)
```

- [ ] **Step 5: Test by running KLEE with `--write-json-tests`:**
```bash
./build/bin/klee --write-json-tests --solver-backend=z3 --external-calls=none /tmp/klee_test.bc
cat klee-last/test000001.json
```
Expected: valid JSON with testId, error, inputs fields.

- [ ] **Step 6: Commit:**
```bash
git add tools/klee/main.cpp
git commit -m "Add --write-json-tests flag for JSON test case output"
```

### Task 2: Add `--write-json-summary` flag and execution summary

**Files:**
- Modify: `tools/klee/main.cpp`

- [ ] **Step 1: Add command-line flag:**
```cpp
cl::opt<bool> WriteJSONSummary("write-json-summary",
    cl::desc("Write execution summary as JSON (default=false)"),
    cl::init(false), cl::cat(TestGenCat));
```

- [ ] **Step 2: Add `writeExecutionSummary` method to `KleeHandler`:**
```cpp
void writeExecutionSummary(const Interpreter *interpreter) {
  auto path = m_outputDirectory.path() + "/summary.json";
  std::error_code ec;
  llvm::raw_fd_ostream f(path, ec, llvm::sys::fs::OF_Text);
  if (ec) return;

  auto stats = interpreter->getStatistics();
  f << "{\n";
  f << "  \"totalInstructions\": " << m_numTotalTests << ",\n";
  f << "  \"generatedTests\": " << m_numGeneratedTests << ",\n";
  f << "  \"errors\": " << (m_numTotalTests - m_numGeneratedTests) << ",\n";
  f << "  \"wallTimeSeconds\": "
    << std::fixed << std::setprecision(2)
    << time::getWallTime().toMicroseconds() / 1000000.0 << "\n";
  f << "}\n";
}
```

Note: The exact stats available depend on what `KleeHandler` tracks. We use `m_numTotalTests` and `m_numGeneratedTests` which are already member variables. Additional stats from `StatsTracker` can be added later.

- [ ] **Step 3: Call at end of main** (after interpreter->runFunctionAsMain, near where stats are printed):
```cpp
if (WriteJSONSummary) {
  handler->writeExecutionSummary(interpreter.get());
}
```

- [ ] **Step 4: Build, test, commit.**

### Task 3: Add `--write-html-cov` flag and HTML coverage report

**Files:**
- Modify: `tools/klee/main.cpp`

- [ ] **Step 1: Add flag, tracking member, and per-test coverage accumulator:**

Flag:
```cpp
cl::opt<bool> WriteHTMLCov("write-html-cov",
    cl::desc("Generate HTML coverage report (default=false)"),
    cl::init(false), cl::cat(TestGenCat));
```

Add to `KleeHandler` class:
```cpp
std::map<std::string, std::set<unsigned>> m_allCoveredLines;
```

- [ ] **Step 2: Accumulate coverage in `processTestCase`** (after the `WriteCov` block):
```cpp
if (WriteHTMLCov) {
  std::map<const std::string*, std::set<unsigned>> cov;
  m_interpreter->getCoveredLines(state, cov);
  for (const auto &entry : cov) {
    m_allCoveredLines[*entry.first].insert(entry.second.begin(), entry.second.end());
  }
}
```

- [ ] **Step 3: Add `writeHTMLCoverageReport` method:**

Generates `coverage/index.html` with inline CSS. For each source file with coverage data, reads the source file and annotates each line as covered (green) or uncovered (default). Creates a summary table at the top.

```cpp
void writeHTMLCoverageReport() {
  auto dirPath = m_outputDirectory.path() + "/coverage";
  llvm::sys::fs::create_directory(dirPath);

  std::error_code ec;
  llvm::raw_fd_ostream f(dirPath + "/index.html", ec, llvm::sys::fs::OF_Text);
  if (ec) return;

  f << "<!DOCTYPE html><html><head><meta charset='utf-8'>\n";
  f << "<title>KLEE Coverage Report</title>\n";
  f << "<style>\n";
  f << "body { font-family: monospace; margin: 20px; }\n";
  f << "h1 { color: #333; }\n";
  f << "table { border-collapse: collapse; margin: 20px 0; }\n";
  f << "th, td { border: 1px solid #ddd; padding: 6px 12px; text-align: left; }\n";
  f << "th { background: #f5f5f5; }\n";
  f << ".covered { background: #dfd; }\n";
  f << ".uncovered { background: #fdd; }\n";
  f << ".linenum { color: #999; padding-right: 10px; user-select: none; }\n";
  f << "pre { margin: 0; }\n";
  f << ".file-section { margin: 30px 0; }\n";
  f << "</style></head><body>\n";
  f << "<h1>KLEE Coverage Report</h1>\n";

  // Summary table
  f << "<table><tr><th>File</th><th>Covered Lines</th></tr>\n";
  for (const auto &entry : m_allCoveredLines) {
    f << "<tr><td><a href='#" << entry.first << "'>" << entry.first
      << "</a></td><td>" << entry.second.size() << "</td></tr>\n";
  }
  f << "</table>\n";

  // Per-file annotated source
  for (const auto &entry : m_allCoveredLines) {
    f << "<div class='file-section' id='" << entry.first << "'>\n";
    f << "<h2>" << entry.first << "</h2>\n";

    // Try to read source file
    std::ifstream src(entry.first);
    if (src.is_open()) {
      f << "<table>\n";
      std::string line;
      unsigned lineNum = 1;
      while (std::getline(src, line)) {
        bool isCovered = entry.second.count(lineNum) > 0;
        f << "<tr class='" << (isCovered ? "covered" : "") << "'>";
        f << "<td class='linenum'>" << lineNum << "</td>";
        f << "<td><pre>";
        // HTML-escape the line
        for (char c : line) {
          if (c == '<') f << "&lt;";
          else if (c == '>') f << "&gt;";
          else if (c == '&') f << "&amp;";
          else f << c;
        }
        f << "</pre></td></tr>\n";
        lineNum++;
      }
      f << "</table>\n";
    } else {
      f << "<p>(source file not available)</p>\n";
    }
    f << "</div>\n";
  }

  f << "</body></html>\n";
}
```

- [ ] **Step 4: Call at end of main** (near where summary is written):
```cpp
if (WriteHTMLCov) {
  handler->writeHTMLCoverageReport();
}
```

- [ ] **Step 5: Build, test with a program that has debug info:**
```bash
./build/bin/klee --write-html-cov --solver-backend=z3 --external-calls=none /tmp/klee_test.bc
open klee-last/coverage/index.html
```

- [ ] **Step 6: Commit:**
```bash
git add tools/klee/main.cpp
git commit -m "Add --write-html-cov flag for HTML coverage reports"
```

### Task 4: End-to-end verification

- [ ] **Step 1: Run all three flags together:**
```bash
./build/bin/klee --write-json-tests --write-json-summary --write-html-cov \
  --solver-backend=z3 --external-calls=none /tmp/bridge_test.bc
```

- [ ] **Step 2: Verify JSON test files are valid:**
```bash
python3 -c "import json; [json.load(open(f)) for f in __import__('glob').glob('klee-last/*.json')]"
```

- [ ] **Step 3: Verify summary.json exists and is valid:**
```bash
python3 -c "import json; print(json.load(open('klee-last/summary.json')))"
```

- [ ] **Step 4: Verify HTML report exists:**
```bash
ls klee-last/coverage/index.html
```

- [ ] **Step 5: Run previous tests to confirm no regressions:**
```bash
./build/bin/klee --solver-backend=z3 --external-calls=none /tmp/klee_test.bc
./build/bin/klee --solver-backend=z3 --external-calls=none /tmp/string_test.bc
./build/bin/klee --external-calls=none /tmp/alloc_mismatch.bc
```

- [ ] **Step 6: Final commit (if not already committed per-task).**
