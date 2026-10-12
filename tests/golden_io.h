// ============================================================================
// tests/golden_io.h · Story 级验收用例的 golden 文件助手（仅测试 TU 使用）
// ----------------------------------------------------------------------------
// 目的：给「新增一条 Story 级验收用例」提供最小、可跑、零依赖的 golden 机制。
// 约定（见 tests/STORY_CASE_TEMPLATE.md）：
//   · golden 目录：tests/golden/
//   · 文件名    ：<caseId>.golden   （一行一条，格式  key=value）
//   · caseId    ：<epic-slug>__<story-slug>，例 epic00__story_golden_sample
//   · seed      ：固定常量 const uint64_t seed = 20250910ULL;（禁运行时随机）
//   · 生成/更新 ：环境变量 GOLDEN_UPDATE=1 时写出（否则只比对）
//   · 失败时    ：打印 key / want / got 逐条差异，并返回非零（由用例计入 CHECK）
//
// 语言约束：C++14 兼容子集；MSVCRT printf 口径（64 位用 %I64u / %I64X）。
// 仅用 <cstdio>/<cstdlib>/<cstring>/<map>/<string>/<vector>，不引入第三方。
// ============================================================================
#ifndef LOWWATER_GOLDEN_IO_H
#define LOWWATER_GOLDEN_IO_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace gold {

// 逐字符去空白（前后）
inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

struct Golden {
    std::string dir;
    std::string caseId;
    std::string path;
    std::map<std::string, std::string> got;
    bool update;

    // dir 默认 "tests/golden"（相对仓库根运行）
    explicit Golden(const char* id, const char* d = "tests/golden")
        : dir(d ? d : "tests/golden"), caseId(id ? id : "unnamed"), update(false) {
        path = dir + "/" + caseId + ".golden";
        const char* u = std::getenv("GOLDEN_UPDATE");
        update = (u != NULL && u[0] != '\0' && u[0] != '0');
    }

    void put(const char* key, const std::string& val) { got[key] = val; }
    void putS (const char* key, const char* val) { put(key, std::string(val ? val : "")); }
    void putI64(const char* key, long long v) {
        char b[32]; std::snprintf(b, sizeof(b), "%I64d", v); put(key, b);
    }
    void putU64(const char* key, unsigned long long v) {
        char b[32]; std::snprintf(b, sizeof(b), "%I64u", v); put(key, b);
    }
    // worldHash 一律 16 位十六进制（大写，无前缀）
    void putHash(const char* key, unsigned long long v) {
        char b[32]; std::snprintf(b, sizeof(b), "%016I64X", v); put(key, b);
    }

    // 写出（GOLDEN_UPDATE=1 路径）
    bool writeOut() const {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        std::fprintf(f, "# golden for %s\n", caseId.c_str());
        std::fprintf(f, "# format: key=value  (generated, do not hand-edit)\n");
        for (std::map<std::string, std::string>::const_iterator it = got.begin(); it != got.end(); ++it)
            std::fprintf(f, "%s=%s\n", it->first.c_str(), it->second.c_str());
        std::fclose(f);
        return true;
    }

    std::map<std::string, std::string> readIn() const {
        std::map<std::string, std::string> want;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return want;
        char line[512];
        while (std::fgets(line, (int)sizeof(line), f)) {
            std::string s = trim(std::string(line));
            if (s.empty() || s[0] == '#') continue;
            const size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            want[trim(s.substr(0, eq))] = trim(s.substr(eq + 1));
        }
        std::fclose(f);
        return want;
    }

    // 比对（或更新）。返回 mismatch 条数；*ok 置为 (mismatch==0)。
    int finish(bool* ok) {
        int mism = 0;
        if (update) {
            const bool w = writeOut();
            std::printf("    [golden] UPDATE %s -> %s (%s)\n",
                        caseId.c_str(), path.c_str(), w ? "written" : "WRITE-FAILED");
            if (ok) *ok = w;
            return w ? 0 : 1;
        }
        std::map<std::string, std::string> want = readIn();
        if (want.empty()) {
            std::printf("    [golden] MISSING %s（首次请以 GOLDEN_UPDATE=1 生成）\n", path.c_str());
            if (ok) *ok = false;
            return (int)got.size() + 1;
        }
        for (std::map<std::string, std::string>::const_iterator it = got.begin(); it != got.end(); ++it) {
            const std::map<std::string, std::string>::const_iterator w = want.find(it->first);
            if (w == want.end()) {
                std::printf("    [golden] %-24s want=<absent> got=%s\n", it->first.c_str(), it->second.c_str());
                ++mism;
            } else if (w->second != it->second) {
                std::printf("    [golden] %-24s want=%s got=%s\n", it->first.c_str(), w->second.c_str(), it->second.c_str());
                ++mism;
            }
        }
        if (ok) *ok = (mism == 0);
        return mism;
    }
};

}  // namespace gold

#endif  // LOWWATER_GOLDEN_IO_H
