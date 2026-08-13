// main.cpp — EX5 阅读器(命令行交互界面)
// 支持:阅读、划线、笔记、摘抄、心得、书评、评分、阅读进度(RFC EX5-001)
#include "ex5book.h"
#include "utf8.h"

#include <windows.h>

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::string fmtTime(long long ts) {
    if (ts <= 0) return "-";
    std::time_t t = (std::time_t)ts;
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

// 把一行按空白切分;第一个 token 是命令,其余是参数
static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

// 取第 n 个 token 之后的全部原文(保留空格),用于笔记/心得正文
static std::string restOf(const std::string& line, int skipTokens) {
    size_t pos = 0;
    for (int i = 0; i < skipTokens; ++i) {
        while (pos < line.size() && isspace((unsigned char)line[pos])) ++pos;
        while (pos < line.size() && !isspace((unsigned char)line[pos])) ++pos;
    }
    while (pos < line.size() && isspace((unsigned char)line[pos])) ++pos;
    return line.substr(pos);
}

static void printHelp() {
    std::cout <<
        "命令一览:\n"
        "  info                          书籍信息\n"
        "  chapters                      章节目录\n"
        "  read <章> [偏移] [字数]        阅读章节(自动记录进度)\n"
        "  mark <章> <起> <止> [批注]     划线(可附批注)\n"
        "  excerpt <章> <起> <止>         摘抄原文\n"
        "  note <章> <内容>               写笔记\n"
        "  think <章> <内容>              写心得\n"
        "  notes                         查看全部划线/摘抄/笔记\n"
        "  thoughts                      查看全部心得\n"
        "  delnote <id>                  删除一条划线/笔记/摘抄\n"
        "  delthink <id>                 删除一条心得\n"
        "  rate <1-5>                    评分\n"
        "  review <内容>                  写书评\n"
        "  reviews                       查看书评与评分\n"
        "  progress                      阅读进度\n"
        "  save                          保存(写回 .ex5 文件)\n"
        "  quit                          保存并退出\n";
}

static void printInfo(const ex5::Book& b) {
    std::cout << "书名: " << b.title() << "\n";
    std::cout << "作者: ";
    for (size_t i = 0; i < b.authors().size(); ++i)
        std::cout << (i ? ", " : "") << b.authors()[i];
    std::cout << "\n";
    if (!b.publisher().empty()) std::cout << "出版: " << b.publisher() << "\n";
    if (!b.version().empty())   std::cout << "版本: " << b.version() << "\n";
    std::cout << "章节数: " << b.chapterCount()
              << "    字数: " << b.wordCount() << "\n";
}

static void printChapterText(const std::string& text, long long start, long long count) {
    auto cps = utf8::decode(text);
    long long total = (long long)cps.size();
    if (start < 0) start = 0;
    if (start >= total) { std::cout << "(偏移超出全文,共 " << total << " 字符)\n"; return; }
    long long end = (count > 0) ? start + count : total;
    if (end > total) end = total;

    // 每 200 字符一段,段首标注字符偏移,方便 mark/excerpt 定位
    const long long kBlock = 200;
    for (long long p = start; p < end; p += kBlock) {
        long long q = p + kBlock; if (q > end) q = end;
        std::cout << "\n[" << p << "-" << q << "] "
                  << utf8::encode(cps, (size_t)p, (size_t)q);
    }
    std::cout << "\n\n-- 本章共 " << total << " 字符 --\n";
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (argc < 2) {
        std::cout << "用法: ex5reader <书籍.ex5>\n";
        return 1;
    }

    // 命令行参数在 Windows 下按系统 ANSI 代码页(GBK)转成 char*,但 Book::open 期望 UTF-8。
    // 用 GetCommandLineW + CommandLineToArgvW 重新拿宽字符版,再 wu8 转 UTF-8,跟 GUI 一致。
    std::string pathUtf8;
    {
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv && wargc >= 2 && wargv[1] && wargv[1][0]) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wargv[1], -1, nullptr, 0, nullptr, nullptr);
            if (n > 0) {
                pathUtf8.resize(n - 1);
                WideCharToMultiByte(CP_UTF8, 0, wargv[1], -1, &pathUtf8[0], n, nullptr, nullptr);
            }
        }
        if (wargv) LocalFree(wargv);
        if (pathUtf8.empty()) pathUtf8 = argv[1];   // 退化路径:纯 ASCII 时也 OK
    }

    ex5::Book book;
    std::string err;
    if (!book.open(pathUtf8, err)) {
        std::cout << "打开失败: " << err << "\n";
        return 1;
    }

    std::cout << "已打开《" << book.title() << "》("
              << book.chapters().size() << " 章)\n";
    int lastChapter = 0; long long lastOffset = 0;
    if (book.lastPosition(lastChapter, lastOffset))
        std::cout << "上次读到: 第 " << lastChapter << " 章,偏移 " << lastOffset
                  << "(输入 read " << lastChapter << " 继续)\n";
    std::cout << "输入 help 查看命令。\n\n";

    std::string line;
    while (true) {
        std::cout << "ex5> ";
        if (!std::getline(std::cin, line)) break;
        auto tok = split(line);
        if (tok.empty()) continue;
        const std::string& cmd = tok[0];

        if (cmd == "quit" || cmd == "exit") break;
        else if (cmd == "help") printHelp();
        else if (cmd == "info") printInfo(book);
        else if (cmd == "chapters") {
            for (auto& c : book.chapters())
                std::cout << "  第 " << c.index << " 章  " << c.title << "\n";
        }
        else if (cmd == "read") {
            if (tok.size() < 2) { std::cout << "用法: read <章> [偏移] [字数]\n"; continue; }
            int chIdx = atoi(tok[1].c_str());
            long long off  = tok.size() > 2 ? atoll(tok[2].c_str()) : 0;
            long long cnt  = tok.size() > 3 ? atoll(tok[3].c_str()) : 0;
            const ex5::Chapter* ch = book.findChapter(chIdx);
            if (!ch) { std::cout << "没有第 " << chIdx << " 章\n"; continue; }
            std::string text = book.chapterText(chIdx);
            if (text.empty()) { std::cout << "(本章没有可读文本资源)\n"; continue; }
            std::cout << "\n===== 第 " << ch->index << " 章  " << ch->title << " =====\n";
            printChapterText(text, off, cnt);
            long long total = (long long)utf8::charCount(text);
            book.recordProgress(chIdx, off, total);
            lastChapter = chIdx; lastOffset = off;
        }
        else if (cmd == "mark" || cmd == "excerpt") {
            if (tok.size() < 4) {
                std::cout << "用法: " << cmd << " <章> <起始偏移> <结束偏移>"
                          << (cmd == "mark" ? " [批注]" : "") << "\n";
                continue;
            }
            int chIdx = atoi(tok[1].c_str());
            long long a = atoll(tok[2].c_str()), b = atoll(tok[3].c_str());
            if (b <= a) { std::cout << "结束偏移必须大于起始偏移\n"; continue; }
            std::string text = book.chapterText(chIdx);
            long long total = (long long)utf8::charCount(text);
            if (a < 0 || b > total) {
                std::cout << "偏移超出范围(本章共 " << total << " 字符)\n"; continue;
            }
            std::string original = utf8::charSubstr(text, (size_t)a, (size_t)b);
            std::string comment = (cmd == "mark") ? restOf(line, 4) : "";
            long long id = book.addNote(comment, chIdx, true, a, b, original);
            if (id < 0) { std::cout << "写入失败\n"; continue; }
            std::cout << (cmd == "mark" ? "已划线" : "已摘抄") << " (#" << id << "): \""
                      << original << "\"";
            if (!comment.empty()) std::cout << " 批注: " << comment;
            std::cout << "\n";
        }
        else if (cmd == "note" || cmd == "think") {
            if (tok.size() < 3) {
                std::cout << "用法: " << cmd << " <章> <内容>\n"; continue;
            }
            int chIdx = atoi(tok[1].c_str());
            std::string content = restOf(line, 2);
            if (cmd == "note") {
                long long id = book.addNote(content, chIdx, false, 0, 0, "");
                std::cout << (id >= 0 ? "笔记已记录 (#" : "写入失败 (#") << id << ")\n";
            } else {
                long long id = book.addInspiration(content, chIdx);
                std::cout << (id >= 0 ? "心得已记录 (#" : "写入失败 (#") << id << ")\n";
            }
        }
        else if (cmd == "notes") {
            auto notes = book.listNotes();
            if (notes.empty()) { std::cout << "(还没有划线/摘抄/笔记)\n"; continue; }
            for (auto& n : notes) {
                std::string kind;
                if (n.hasRange && n.content.empty()) kind = "摘抄";
                else if (n.hasRange)                 kind = "划线";
                else                                 kind = "笔记";
                std::cout << "#" << n.id << " [" << kind << "] 第 " << n.chapterId
                          << " 章  " << fmtTime(n.createTime) << "\n";
                if (!n.original.empty()) {
                    std::cout << "    原文";
                    if (n.hasRange)
                        std::cout << "(" << n.rangeStart << "-" << n.rangeEnd << ")";
                    std::cout << ": " << n.original << "\n";
                }
                if (!n.content.empty())
                    std::cout << "    批注: " << n.content << "\n";
            }
        }
        else if (cmd == "thoughts") {
            auto ins = book.listInspirations();
            if (ins.empty()) { std::cout << "(还没有心得)\n"; continue; }
            for (auto& i : ins)
                std::cout << "#" << i.id << " [心得] 第 " << i.chapterId << " 章  "
                          << fmtTime(i.createTime) << "\n    " << i.content << "\n";
        }
        else if (cmd == "delnote" || cmd == "delthink") {
            if (tok.size() < 2) { std::cout << "用法: " << cmd << " <id>\n"; continue; }
            long long id = atoll(tok[1].c_str());
            bool ok = (cmd == "delnote") ? book.deleteNote(id) : book.deleteInspiration(id);
            std::cout << (ok ? "已删除\n" : "未找到该记录\n");
        }
        else if (cmd == "rate") {
            if (tok.size() < 2) { std::cout << "用法: rate <1-5>\n"; continue; }
            int s = atoi(tok[1].c_str());
            std::cout << (book.addRating(s) ? "评分成功\n" : "评分失败(需 1-5)\n");
        }
        else if (cmd == "review") {
            std::string content = restOf(line, 1);
            if (content.empty()) { std::cout << "用法: review <内容>\n"; continue; }
            long long id = book.addReview(content);
            std::cout << (id >= 0 ? "书评已记录 (#" : "写入失败 (#") << id << ")\n";
        }
        else if (cmd == "reviews") {
            auto r = book.myRating();
            std::cout << "我的评分: " << (r ? std::to_string(*r) + " 星" : "(未评分)") << "\n";
            for (auto& v : book.listReviews())
                std::cout << "#" << v.id << "  " << fmtTime(v.createTime)
                          << "\n    " << v.content << "\n";
        }
        else if (cmd == "progress") {
            int ch; long long off;
            if (book.lastPosition(ch, off)) {
                double pct = book.chapterCount() > 0
                    ? 100.0 * ch / book.chapterCount() : 0.0;
                std::cout << "读到第 " << ch << " 章(偏移 " << off << "),全书进度约 "
                          << std::fixed << std::setprecision(1) << pct << "%\n";
            } else std::cout << "(尚无阅读记录)\n";
        }
        else if (cmd == "save") {
            std::string serr;
            std::cout << (book.save(serr) ? "已保存到 .ex5 文件\n"
                                          : "保存失败: " + serr + "\n");
        }
        else std::cout << "未知命令,输入 help 查看帮助。\n";
    }

    std::string serr;
    if (book.save(serr)) std::cout << "已保存。再见!\n";
    else std::cout << "保存失败: " << serr << "\n";
    return 0;
}
