// ex5book.cpp — EX5 容器读写层实现
#include "ex5book.h"

#include "miniz.h"
#include "miniz_zip.h"
#include "sqlite3.h"
#include "json.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include <windows.h>
#include <bcrypt.h>

using json = nlohmann::json;

namespace ex5 {

static long long nowTs() { return (long long)std::time(nullptr); }

// UTF-8 std::string → std::wstring(给 _wfopen / CreateFileW / std::ifstream 的 wchar_t* 重载用)。
// 绕过 Windows CP_ACP(GBK 等 ANSI 代码页),彻底解决中文路径乱码。
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// std::wstring → std::string(CP_ACP)。仅用于 sqlite3_open 之类只接 const char* 的 C API。
// sqlite3 内部用 fopen,所以这里必须给 ANSI 字节,不能给 UTF-8。
static std::string wide_to_ansi(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n, 0);
    WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(),
                        s.data(), n, nullptr, nullptr);
    return s;
}

// v1.1:UUID v4(notes/inspiration/reviews/ratings 的 uuid 列,跨用户合并去重用)
static std::string genUuid() {
    unsigned char b[16]{};
    BCryptGenRandom(nullptr, b, (ULONG)sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);   // version 4
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);   // variant 10xx
    char s[37];
    snprintf(s, sizeof(s),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}

// ---------- ZIP 帮助函数 ----------

// 从打开的 zip reader 提取一个条目到内存;不存在返回 false。
static bool zipExtractToString(mz_zip_archive* zr, const char* name, std::string& out) {
    int idx = mz_zip_reader_locate_file(zr, name, nullptr, 0);
    if (idx < 0) return false;
    size_t size = 0;
    void* p = mz_zip_reader_extract_to_heap(zr, idx, &size, 0);
    if (!p) return false;
    out.assign((const char*)p, size);
    mz_free(p);
    return true;
}

// ---------- Book ----------

void Book::resetZip_() {
    if (zrOpen_) { mz_zip_reader_end(&zr_); zrOpen_ = false; }
    zipMem_.clear();
}

// 扫同目录下所有 *.work.db,凡是 SQLite 格式的就删掉(EX5 Reader 的临时工作库)。
// 用来清掉老版本写出的"乱码 UTF-16 名字"残留(老版本用 std::ofstream(const char*)
// 把 UTF-8 字节当 CP_ACP 解读,NTFS 上文件名是 U+FFFD/U+E0FF 之类,资源管理器显示为 "???xxx")。
// 不用重建老路径的 wide char 字节(不同 MSVC 版本行为不同,猜不中),直接走 NTFS 通配符
// 枚举 + SQLite header 验证:是 EX5 的 work.db 就删,不是就跳过,完全安全。
static void cleanupLegacyWorkDb_(const std::wstring& ex5PathW) {
    if (ex5PathW.empty()) return;
    // 截目录部分(去掉最后的 "\xxx.ex5")
    const wchar_t* p = wcsrchr(ex5PathW.c_str(), L'\\');
    if (!p) p = wcsrchr(ex5PathW.c_str(), L'/');
    if (!p) return;
    std::wstring dir(ex5PathW.c_str(), (size_t)(p - ex5PathW.c_str()) + 1);  // 含末尾 '\'
    std::wstring pattern = dir + L"*.work.db";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        // 跳过目录
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        // 拼出完整宽字符路径(目录 + cFileName 仍可能含 U+FFFD 之类,NTFS 接受)
        std::wstring full = dir + fd.cFileName;
        // 读前 16 字节,验证是不是 SQLite 头
        std::ifstream f(full.c_str(), std::ios::binary);
        if (!f) continue;
        char hdr[16] = {};
        f.read(hdr, sizeof(hdr));
        f.close();
        static const char kSqliteHdr[] = "SQLite format 3";
        if (memcmp(hdr, kSqliteHdr, sizeof(kSqliteHdr) - 1) == 0) {
            _wremove(full.c_str());   // 是 EX5 的 work.db(无论是新正确命名还是老乱码命名),删
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void Book::close_() {
    resetZip_();
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
    if (!dbTmpPathW_.empty()) { _wremove(dbTmpPathW_.c_str()); dbTmpPathW_.clear(); }
    // 顺手清掉同目录下老的"乱码命名" work.db 残留(老版本 std::ofstream(const char*) 写出的)
    cleanupLegacyWorkDb_(pathW_);
}

Book::~Book() {
    close_();
}

// 探测文件大小:统一先整体读入内存再用 mz_zip_reader_init_mem 解析。
// 这样所有 Windows 文件 IO 都走 _wfopen(wchar_t 路径),彻底绕开 CP_ACP 编码坑;
// 同时 mz_zip_reader_init_file 不再被调用,也避免了 miniz 内部 fopen(const char*) 的同样问题。
bool Book::openZip_(std::string& err) {
    memset(&zr_, 0, sizeof(zr_));
    zipMem_.clear();

    // MSVC 扩展:std::ifstream 接受 const wchar_t*,内部用 _wfopen,不走 ANSI 代码页。
    std::ifstream f(pathW_.c_str(), std::ios::binary | std::ios::ate);
    if (!f) { err = "无法打开文件: " + path_; return false; }
    std::streamoff size = f.tellg();
    if (size < 0) { err = "无法读取文件大小: " + path_; return false; }
    f.seekg(0);
    zipMem_.resize((size_t)size);
    if (!f.read(zipMem_.data(), size)) {
        err = "读取文件失败: " + path_;
        zipMem_.clear();
        return false;
    }
    f.close();

    if (!mz_zip_reader_init_mem(&zr_, zipMem_.data(), zipMem_.size(), 0)) {
        err = "无法解析 .ex5(不是有效的 ZIP 归档): " + path_;
        zipMem_.clear();
        return false;
    }
    openMode_ = "内存模式(全书 " + std::to_string((long long)(size / 1024)) + " KB 已载入)";
    zrOpen_ = true;
    return true;
}

bool Book::open(const std::string& path, std::string& err) {
    path_ = path;
    // 同一份路径的宽字符版本(UTF-8 → UTF-16),给所有 _wfopen / std::ifstream(wchar_t*) /
    // _wremove / _wrename 用,绕开系统 ANSI 代码页(CP_ACP=936 GBK)对 UTF-8 字节的错乱解析。
    pathW_ = utf8_to_wide(path);

    if (!openZip_(err)) return false;
    mz_zip_archive& zr = zr_;

    // meta.xml:加密检查(encrypt_scope != 0 的本实现不支持)
    std::string meta;
    if (zipExtractToString(&zr, "meta.xml", meta)) {
        auto tag = [&](const char* t) -> std::string {
            std::string o = std::string("<") + t + ">", c = std::string("</") + t + ">";
            size_t a = meta.find(o), b = meta.find(c);
            if (a == std::string::npos || b == std::string::npos || b <= a) return "";
            return meta.substr(a + o.size(), b - a - o.size());
        };
        std::string ver = tag("version");
        if (!ver.empty() && ver != "1.0" && ver != "1.1") {
            resetZip_();
            err = "不支持的协议版本: " + ver;
            return false;
        }
        std::string scope = tag("encrypt_scope");
        if (!scope.empty() && scope != "0") {
            resetZip_();
            err = "该文件启用了加密(encrypt_scope=" + scope + "),本阅读器暂不支持解密";
            return false;
        }
    }

    // book_data/*.json
    std::string infoStr, chaptersStr, resourcesStr;
    if (!zipExtractToString(&zr, "book_data/info.json", infoStr) ||
        !zipExtractToString(&zr, "book_data/chapters.json", chaptersStr) ||
        !zipExtractToString(&zr, "book_data/resources.json", resourcesStr)) {
        resetZip_();
        err = "归档中缺少 book_data/ 下的 info.json / chapters.json / resources.json";
        return false;
    }

    try {
        // null 安全的字符串读取(协议中可选字段允许为 null)
        auto optStr = [](const json& j, const char* key) -> std::string {
            if (!j.contains(key) || !j[key].is_string()) return "";
            return j[key].get<std::string>();
        };

        json info = json::parse(infoStr);
        title_       = info.value("title", std::string("(未命名)"));
        publisher_   = optStr(info, "publisher");
        version_     = optStr(info, "version");
        wordCount_   = info.value("word_count", 0);
        chapterCount_ = info.value("chapter_count", 0);
        for (auto& a : info.value("authors", json::array()))
            if (a.is_string()) authors_.push_back(a.get<std::string>());

        for (auto& c : json::parse(chaptersStr)) {
            Chapter ch;
            ch.index = c.value("index", 0);
            ch.title = optStr(c, "title");
            for (auto& r : c.value("resource_ids", json::array()))
                ch.resourceIds.push_back(r.get<int>());
            chapters_.push_back(std::move(ch));
        }
        for (auto& r : json::parse(resourcesStr)) {
            Resource res;
            res.id      = r.value("resource_id", 0);
            res.content = optStr(r, "content");
            res.type    = optStr(r, "type");
            res.resType = optStr(r, "resType");
            resources_.push_back(std::move(res));
        }
    } catch (const std::exception& e) {
        resetZip_();
        err = std::string("JSON 解析失败: ") + e.what();
        return false;
    }

    // read_data.db -> 临时文件(SQLite 需要可随机访问的真实文件)
    dbTmpPathW_ = pathW_ + L".work.db";
    std::string dbData;
    bool hasDb = zipExtractToString(&zr, "read_data.db", dbData);
    // 注意:ZIP reader 会话期间保持常开(zr_),章节正文按需从中读取

    if (hasDb) {
        // MSVC 扩展:std::ofstream 接受 const wchar_t*
        std::ofstream f(dbTmpPathW_.c_str(), std::ios::binary);
        f.write(dbData.data(), (std::streamsize)dbData.size());
        f.close();
    }
    // sqlite3_open 只接 const char*;把宽字符路径转回 CP_ACP 字节给 fopen 用
    std::string dbTmpAnsi = wide_to_ansi(dbTmpPathW_);
    if (sqlite3_open(dbTmpAnsi.c_str(), &db_) != SQLITE_OK) {
        err = std::string("无法打开 read_data.db: ") + sqlite3_errmsg(db_);
        return false;
    }
    if (!ensureSchema() || !ensureUser()) {
        err = std::string("read_data.db 初始化失败: ") + sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool Book::save(std::string& err) {
    if (!db_) { err = "尚未打开书籍"; return false; }
    exec("COMMIT;");
    sqlite3_close(db_);
    db_ = nullptr;

    // 关闭当前 zip reader;但保留 zipMem_(已经整文件读入内存),后面 save 内部用来重建 reader,
    // 不再走磁盘 fopen —— 彻底绕开 CP_ACP 编码坑。
    if (zrOpen_) { mz_zip_reader_end(&zr_); zrOpen_ = false; }

    // 重建 ZIP:复制除 read_data.db 外的所有条目,再加入新的 db
    mz_zip_archive zr, zw;
    memset(&zr, 0, sizeof(zr));
    memset(&zw, 0, sizeof(zw));
    // 临时输出 zip 的宽字符路径(给 _wfopen / _wremove / _wrename 用),
    // 同步保留 std::string 版本仅供错误信息显示。
    std::wstring tmpZipW = pathW_ + L".tmp";
    std::string  tmpZip  = path_  + ".tmp";

    if (!mz_zip_reader_init_mem(&zr, zipMem_.data(), zipMem_.size(), 0)) {
        err = "保存失败:无法重新打开内存中的归档";
        return false;
    }
    // 写到 heap buffer,后面再用 _wfopen 落盘 —— 绕开 mz_zip_writer_init_file 的 fopen 编码问题
    if (!mz_zip_writer_init_heap(&zw, 0, 0)) {
        mz_zip_reader_end(&zr);
        err = "保存失败:无法创建临时归档";
        return false;
    }
    void* heapBuf = nullptr;
    mz_uint n = mz_zip_reader_get_num_files(&zr);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zr, i, &st)) continue;
        if (std::string(st.m_filename) == "read_data.db") continue;
        if (std::string(st.m_filename) == "meta.xml") {
            // v1.1:保存时把协议版本提升到 1.1(其余内容保持原样)
            std::string meta;
            if (zipExtractToString(&zr, "meta.xml", meta)) {
                size_t a = meta.find("<version>"), b = meta.find("</version>");
                if (a != std::string::npos && b != std::string::npos && b > a)
                    meta = meta.substr(0, a + 9) + "1.1" + meta.substr(b);
                if (!mz_zip_writer_add_mem(&zw, "meta.xml", meta.data(), meta.size(),
                                           MZ_DEFAULT_COMPRESSION)) {
                    mz_zip_reader_end(&zr); mz_zip_writer_end(&zw);
                    _wremove(tmpZipW.c_str());
                    err = "保存失败:写入 meta.xml 出错";
                    return false;
                }
                continue;
            }
        }
        if (!mz_zip_writer_add_from_zip_reader(&zw, &zr, i)) {
            mz_zip_reader_end(&zr); mz_zip_writer_end(&zw);
            _wremove(tmpZipW.c_str());
            err = std::string("保存失败:复制条目 ") + st.m_filename + " 出错";
            return false;
        }
    }
    // 从 dbTmpPathW_ 读 read_data.db
    std::ifstream f(dbTmpPathW_.c_str(), std::ios::binary);
    std::string dbData((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (!mz_zip_writer_add_mem(&zw, "read_data.db", dbData.data(), dbData.size(),
                               MZ_DEFAULT_COMPRESSION)) {
        mz_zip_reader_end(&zr); mz_zip_writer_end(&zw);
        _wremove(tmpZipW.c_str());
        err = "保存失败:写入 read_data.db 出错";
        return false;
    }
    mz_zip_reader_end(&zr);
    size_t heapSize = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zw, &heapBuf, &heapSize)) {
        mz_zip_writer_end(&zw);
        _wremove(tmpZipW.c_str());
        err = "保存失败:归档收尾出错";
        return false;
    }
    mz_zip_writer_end(&zw);

    // 把 heap buffer 用 _wfopen 写到 tmpZipW(宽字符路径,绕开 ANSI 代码页)
    FILE* fp = _wfopen(tmpZipW.c_str(), L"wb");
    if (!fp) {
        mz_free(heapBuf);
        err = "保存失败:无法创建临时归档文件(" + tmpZip + ")";
        return false;
    }
    if (heapSize > 0 && fwrite(heapBuf, 1, heapSize, fp) != heapSize) {
        fclose(fp); mz_free(heapBuf);
        _wremove(tmpZipW.c_str());
        err = "保存失败:写入临时归档出错";
        return false;
    }
    fclose(fp);
    mz_free(heapBuf);

    // 替换原文件:用宽字符 _wremove + _wrename,绕开 ANSI 编码坑
    if (_wremove(pathW_.c_str()) != 0 || _wrename(tmpZipW.c_str(), pathW_.c_str()) != 0) {
        err = "保存失败:替换原文件出错(" + tmpZip + " 已生成,可手动改名)";
        return false;
    }

    // 重新打开工作数据库与 ZIP reader 继续会话
    std::string dbTmpAnsi = wide_to_ansi(dbTmpPathW_);
    if (sqlite3_open(dbTmpAnsi.c_str(), &db_) != SQLITE_OK) {
        err = std::string("保存后重开数据库失败: ") + sqlite3_errmsg(db_);
        return false;
    }
    std::string zerr;
    openZip_(zerr);   // 重新挂接归档(从磁盘读新版,失败不影响已保存结果)
    return true;
}

// ---------- 元数据 / 阅读 ----------

const Chapter* Book::findChapter(int index) const {
    for (auto& c : chapters_) if (c.index == index) return &c;
    return nullptr;
}

const Resource* Book::findResource(int id) const {
    for (auto& r : resources_) if (r.id == id) return &r;
    return nullptr;
}

std::string Book::chapterText(int chapterIndex) const {
    const Chapter* ch = findChapter(chapterIndex);
    if (!ch || !zrOpen_) return {};

    std::string out;
    for (int rid : ch->resourceIds) {
        const Resource* res = findResource(rid);
        if (!res) continue;
        if (res->type != "txt" && res->type != "html") continue;
        std::string name = "resources/" + res->content, buf;
        mz_zip_archive& zr = const_cast<mz_zip_archive&>(zr_);
        if (zipExtractToString(&zr, name.c_str(), buf)) {
            out += buf;
            if (!out.empty() && out.back() != '\n') out += '\n';
        }
    }
    return out;
}

bool Book::recordProgress(int chapterIndex, long long charOffset, long long totalChars) {
    if (!db_) return false;
    long long t = nowTs();
    double pct = 0.0;
    if (chapterCount_ > 0)
        pct = 100.0 * (double)chapterIndex / (double)chapterCount_;

    // history:每个会话一行,progress 为 JSON 位置摘要(RFC 3.4.2)
    std::ostringstream pj;
    pj << "{\"chapter\":" << chapterIndex << ",\"offset\":" << charOffset << "}";
    std::string pjson = pj.str();

    sqlite3_stmt* st = nullptr;
    long long historyId = 0;
    if (sqlite3_prepare_v2(db_,
            "SELECT id FROM history WHERE user_id=? AND status=0 ORDER BY id DESC LIMIT 1",
            -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, userId_);
        if (sqlite3_step(st) == SQLITE_ROW) historyId = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st); st = nullptr;
    }
    if (historyId == 0) {
        sqlite3_prepare_v2(db_,
            "INSERT INTO history(read_count,user_id,start_time,status,progress) VALUES(1,?,?,0,?)",
            -1, &st, nullptr);
        sqlite3_bind_int64(st, 1, userId_);
        sqlite3_bind_int64(st, 2, t);
        sqlite3_bind_text(st, 3, pjson.c_str(), -1, SQLITE_TRANSIENT);
        bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st); st = nullptr;
        if (!ok) return false;
        historyId = sqlite3_last_insert_rowid(db_);
    } else {
        sqlite3_prepare_v2(db_,
            "UPDATE history SET end_time=?, progress=? WHERE id=?", -1, &st, nullptr);
        sqlite3_bind_int64(st, 1, t);
        sqlite3_bind_text(st, 2, pjson.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 3, historyId);
        sqlite3_step(st);
        sqlite3_finalize(st); st = nullptr;
    }

    // records:本次阅读片段,progress 为 0-100 的数值(RFC 3.4.3)
    sqlite3_prepare_v2(db_,
        "INSERT INTO records(history_id,user_id,start_time,end_time,progress,duration,"
        "start_chapter,end_chapter,record_time) VALUES(?,?,?,?,?,0,?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, historyId);
    sqlite3_bind_int64(st, 2, userId_);
    sqlite3_bind_int64(st, 3, t);
    sqlite3_bind_int64(st, 4, t);
    sqlite3_bind_double(st, 5, pct);
    sqlite3_bind_int(st, 6, chapterIndex);
    sqlite3_bind_int(st, 7, chapterIndex);
    sqlite3_bind_int64(st, 8, t);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    (void)totalChars;
    return ok;
}

bool Book::lastPosition(int& chapter, long long& offset) {
    chapter = 0; offset = 0;
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db_,
            "SELECT progress FROM history WHERE user_id=? ORDER BY id DESC LIMIT 1",
            -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, userId_);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char* p = (const char*)sqlite3_column_text(st, 0);
            if (p) {
                try {
                    json j = json::parse(p);
                    chapter = j.value("chapter", 0);
                    offset  = j.value("offset", 0LL);
                    found = chapter > 0;
                } catch (...) {}
            }
        }
        sqlite3_finalize(st);
    }
    return found;
}

// ---------- 用户数据 ----------

long long Book::addNote(const std::string& content, int chapterId,
                        bool hasRange, long long rangeStart, long long rangeEnd,
                        const std::string& original) {
    if (!db_) return -1;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO notes(uuid,content,type,create_time,update_time,user_id,chapter_id,"
        "range_start,range_end,original) VALUES(?,?,'txt',?,?,?,?,?,?,?)",
        -1, &st, nullptr);
    long long t = nowTs();
    std::string u = genUuid();
    sqlite3_bind_text(st, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    if (content.empty()) sqlite3_bind_null(st, 2);
    else sqlite3_bind_text(st, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, t);
    sqlite3_bind_int64(st, 4, t);
    sqlite3_bind_int64(st, 5, userId_);
    if (chapterId > 0) sqlite3_bind_int(st, 6, chapterId); else sqlite3_bind_null(st, 6);
    if (hasRange) { sqlite3_bind_int64(st, 7, rangeStart); sqlite3_bind_int64(st, 8, rangeEnd); }
    else { sqlite3_bind_null(st, 7); sqlite3_bind_null(st, 8); }
    if (original.empty()) sqlite3_bind_null(st, 9);
    else sqlite3_bind_text(st, 9, original.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? sqlite3_last_insert_rowid(db_) : -1;
}

bool Book::deleteNote(long long id) {
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM notes WHERE id=? AND user_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int64(st, 2, userId_);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Book::updateNote(long long id, const std::string& content) {
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE notes SET content=?, update_time=? WHERE id=? AND user_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)time(nullptr));
    sqlite3_bind_int64(st, 3, id);
    sqlite3_bind_int64(st, 4, userId_);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return ok;
}

std::vector<Note> Book::listNotes() {
    std::vector<Note> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    // v1.1 共享阅读:返回所有用户的记录,join users 取作者名,own 标记编辑权限
    if (sqlite3_prepare_v2(db_,
            "SELECT n.id,n.uuid,n.content,n.type,n.create_time,n.update_time,n.chapter_id,"
            "n.range_start,n.range_end,n.original,u.name,(n.user_id=?) "
            "FROM notes n JOIN users u ON u.id=n.user_id ORDER BY n.id",
            -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int64(st, 1, userId_);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Note n;
        n.id = sqlite3_column_int64(st, 0);
        if (sqlite3_column_type(st, 1) != SQLITE_NULL)
            n.uuid = (const char*)sqlite3_column_text(st, 1);
        if (sqlite3_column_type(st, 2) != SQLITE_NULL)
            n.content = (const char*)sqlite3_column_text(st, 2);
        n.type = (const char*)sqlite3_column_text(st, 3);
        n.createTime = sqlite3_column_int64(st, 4);
        n.updateTime = sqlite3_column_int64(st, 5);
        if (sqlite3_column_type(st, 6) != SQLITE_NULL)
            n.chapterId = sqlite3_column_int(st, 6);
        if (sqlite3_column_type(st, 7) != SQLITE_NULL) {
            n.hasRange = true;
            n.rangeStart = sqlite3_column_int64(st, 7);
            n.rangeEnd   = sqlite3_column_int64(st, 8);
        }
        if (sqlite3_column_type(st, 9) != SQLITE_NULL)
            n.original = (const char*)sqlite3_column_text(st, 9);
        if (sqlite3_column_type(st, 10) != SQLITE_NULL)
            n.author = (const char*)sqlite3_column_text(st, 10);
        n.own = sqlite3_column_int(st, 11) != 0;
        out.push_back(std::move(n));
    }
    sqlite3_finalize(st);
    return out;
}

long long Book::addInspiration(const std::string& content, int chapterId) {
    if (!db_) return -1;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO inspiration(uuid,type,content,create_time,update_time,user_id,chapter_id)"
        " VALUES(?,'txt',?,?,?,?,?)",
        -1, &st, nullptr);
    long long t = nowTs();
    std::string u = genUuid();
    sqlite3_bind_text(st, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, t);
    sqlite3_bind_int64(st, 4, t);
    sqlite3_bind_int64(st, 5, userId_);
    if (chapterId > 0) sqlite3_bind_int(st, 6, chapterId); else sqlite3_bind_null(st, 6);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? sqlite3_last_insert_rowid(db_) : -1;
}

bool Book::deleteInspiration(long long id) {
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM inspiration WHERE id=? AND user_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int64(st, 2, userId_);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return ok;
}

bool Book::updateInspiration(long long id, const std::string& content) {
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE inspiration SET content=?, update_time=? WHERE id=? AND user_id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)time(nullptr));
    sqlite3_bind_int64(st, 3, id);
    sqlite3_bind_int64(st, 4, userId_);
    bool ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db_) > 0;
    sqlite3_finalize(st);
    return ok;
}

std::vector<Inspiration> Book::listInspirations() {
    std::vector<Inspiration> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT i.id,i.uuid,i.content,i.create_time,i.update_time,i.chapter_id,"
            "u.name,(i.user_id=?) "
            "FROM inspiration i JOIN users u ON u.id=i.user_id ORDER BY i.id",
            -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int64(st, 1, userId_);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Inspiration i;
        i.id = sqlite3_column_int64(st, 0);
        if (sqlite3_column_type(st, 1) != SQLITE_NULL)
            i.uuid = (const char*)sqlite3_column_text(st, 1);
        if (sqlite3_column_type(st, 2) != SQLITE_NULL)
            i.content = (const char*)sqlite3_column_text(st, 2);
        i.createTime = sqlite3_column_int64(st, 3);
        i.updateTime = sqlite3_column_int64(st, 4);
        if (sqlite3_column_type(st, 5) != SQLITE_NULL)
            i.chapterId = sqlite3_column_int(st, 5);
        if (sqlite3_column_type(st, 6) != SQLITE_NULL)
            i.author = (const char*)sqlite3_column_text(st, 6);
        i.own = sqlite3_column_int(st, 7) != 0;
        out.push_back(std::move(i));
    }
    sqlite3_finalize(st);
    return out;
}

long long Book::addReview(const std::string& content) {
    if (!db_) return -1;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO reviews(uuid,content,user_id,create_time,update_time) VALUES(?,?,?,?,?)",
        -1, &st, nullptr);
    long long t = nowTs();
    std::string u = genUuid();
    sqlite3_bind_text(st, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, userId_);
    sqlite3_bind_int64(st, 4, t);
    sqlite3_bind_int64(st, 5, t);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? sqlite3_last_insert_rowid(db_) : -1;
}

std::vector<Review> Book::listReviews() {
    std::vector<Review> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT r.id,r.uuid,r.content,r.create_time,u.name,(r.user_id=?) "
            "FROM reviews r JOIN users u ON u.id=r.user_id ORDER BY r.id",
            -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int64(st, 1, userId_);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Review r;
        r.id = sqlite3_column_int64(st, 0);
        if (sqlite3_column_type(st, 1) != SQLITE_NULL)
            r.uuid = (const char*)sqlite3_column_text(st, 1);
        r.content = (const char*)sqlite3_column_text(st, 2);
        r.createTime = sqlite3_column_int64(st, 3);
        if (sqlite3_column_type(st, 4) != SQLITE_NULL)
            r.author = (const char*)sqlite3_column_text(st, 4);
        r.own = sqlite3_column_int(st, 5) != 0;
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return out;
}

bool Book::addRating(int stars) {
    if (!db_ || stars < 1 || stars > 5) return false;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM ratings WHERE user_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, userId_);
    sqlite3_step(st);
    sqlite3_finalize(st); st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO ratings(uuid,user_id,rating,create_time,update_time) VALUES(?,?,?,?,?)",
        -1, &st, nullptr);
    long long t = nowTs();
    std::string u = genUuid();
    sqlite3_bind_text(st, 1, u.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, userId_);
    sqlite3_bind_int(st, 3, stars);
    sqlite3_bind_int64(st, 4, t);
    sqlite3_bind_int64(st, 5, t);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

std::vector<Rating> Book::listRatings() {
    std::vector<Rating> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT r.rating,r.update_time,u.name,(r.user_id=?) "
            "FROM ratings r JOIN users u ON u.id=r.user_id ORDER BY r.update_time",
            -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int64(st, 1, userId_);
    while (sqlite3_step(st) == SQLITE_ROW) {
        Rating r;
        r.stars = sqlite3_column_int(st, 0);
        r.updateTime = sqlite3_column_int64(st, 1);
        if (sqlite3_column_type(st, 2) != SQLITE_NULL)
            r.author = (const char*)sqlite3_column_text(st, 2);
        r.own = sqlite3_column_int(st, 3) != 0;
        out.push_back(std::move(r));
    }
    sqlite3_finalize(st);
    return out;
}

std::optional<int> Book::myRating() {
    if (!db_) return std::nullopt;
    sqlite3_stmt* st = nullptr;
    std::optional<int> out;
    if (sqlite3_prepare_v2(db_,
            "SELECT rating FROM ratings WHERE user_id=? ORDER BY id DESC LIMIT 1",
            -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, userId_);
        if (sqlite3_step(st) == SQLITE_ROW) out = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return out;
}

// ---------- 内部 ----------

bool Book::exec(const std::string& sql) {
    return sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}

// ---------- 多用户 ----------

// 密码存储格式(cipher BLOB): 16 字节随机盐 || SHA-256(salt || password)
static bool hashPassword(const std::string& password, const unsigned char* salt16,
                         std::vector<unsigned char>& outBlob) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hh = nullptr;
    unsigned char hash[32];
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    if (BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0) == 0) {
        if (BCryptHashData(hh, (PUCHAR)salt16, 16, 0) == 0 &&
            BCryptHashData(hh, (PUCHAR)password.data(), (ULONG)password.size(), 0) == 0 &&
            BCryptFinishHash(hh, hash, 32, 0) == 0) {
            outBlob.assign(salt16, salt16 + 16);
            outBlob.insert(outBlob.end(), hash, hash + 32);
            ok = true;
        }
        BCryptDestroyHash(hh);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static bool verifyPassword(const std::string& password,
                           const unsigned char* blob, int blobLen) {
    if (!blob || blobLen != 48) return false;
    std::vector<unsigned char> recomputed;
    if (!hashPassword(password, blob, recomputed)) return false;
    if (recomputed.size() != 48) return false;
    unsigned char diff = 0;
    for (int i = 0; i < 48; ++i) diff |= recomputed[(size_t)i] ^ blob[i];
    return diff == 0;
}

std::vector<Book::UserInfo> Book::listUsers() {
    std::vector<UserInfo> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT id,identifier,name,cipher FROM users ORDER BY id", -1, &st, nullptr) != SQLITE_OK)
        return out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        UserInfo u;
        u.id = sqlite3_column_int64(st, 0);
        const char* idf = (const char*)sqlite3_column_text(st, 1);
        if (idf) u.identifier = idf;
        if (sqlite3_column_type(st, 2) != SQLITE_NULL)
            u.name = (const char*)sqlite3_column_text(st, 2);
        u.hasCipher = sqlite3_column_type(st, 3) == SQLITE_BLOB &&
                      sqlite3_column_bytes(st, 3) == 48;
        out.push_back(std::move(u));
    }
    sqlite3_finalize(st);
    return out;
}

long long Book::createUser(const std::string& name, const std::string& password, std::string& err) {
    if (!db_) { err = "尚未打开书籍"; return -1; }
    if (name.empty()) { err = "用户名不能为空"; return -1; }
    // identifier 取用户名,users.identifier 有 UNIQUE 约束,重名即冲突
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id FROM users WHERE identifier=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st); st = nullptr;
    if (exists) { err = "用户已存在: " + name; return -1; }

    std::vector<unsigned char> blob;
    if (!password.empty()) {
        unsigned char salt[16];
        if (BCryptGenRandom(nullptr, salt, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 ||
            !hashPassword(password, salt, blob)) {
            err = "密码加密失败";
            return -1;
        }
    }
    sqlite3_prepare_v2(db_,
        "INSERT INTO users(identifier,name,cipher) VALUES(?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    if (blob.empty()) sqlite3_bind_null(st, 3);
    else sqlite3_bind_blob(st, 3, blob.data(), (int)blob.size(), SQLITE_TRANSIENT);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) { err = std::string("创建用户失败: ") + sqlite3_errmsg(db_); return -1; }
    return sqlite3_last_insert_rowid(db_);
}

bool Book::switchUser(long long id, const std::string& password, std::string& err) {
    if (!db_) { err = "尚未打开书籍"; return false; }
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT cipher FROM users WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        err = "用户不存在";
        return false;
    }
    int blobLen = sqlite3_column_bytes(st, 0);
    const unsigned char* blob = (const unsigned char*)sqlite3_column_blob(st, 0);
    bool hasCipher = sqlite3_column_type(st, 0) == SQLITE_BLOB && blobLen == 48;
    bool pass = true;
    if (hasCipher) pass = verifyPassword(password, blob, blobLen);
    sqlite3_finalize(st);
    if (!pass) { err = "密码错误"; return false; }
    userId_ = id;
    return true;
}

std::string Book::currentUserName() {
    if (!db_ || userId_ <= 0) return "";
    sqlite3_stmt* st = nullptr;
    std::string name;
    if (sqlite3_prepare_v2(db_, "SELECT name FROM users WHERE id=?", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, userId_);
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL)
            name = (const char*)sqlite3_column_text(st, 0);
        sqlite3_finalize(st);
    }
    return name;
}

// 按 RFC EX5-001 §3.4 建表(IF NOT EXISTS,兼容已有库)
bool Book::ensureSchema() {
    static const char* kSchema = R"SQL(
BEGIN;
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  identifier TEXT NOT NULL UNIQUE,
  name TEXT,
  gender TEXT,
  birth_date INTEGER,
  lock INTEGER DEFAULT 0 CHECK (lock IN (0, 1)),
  cipher BLOB
);
CREATE TABLE IF NOT EXISTS history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  read_count INTEGER DEFAULT 1,
  user_id INTEGER NOT NULL,
  start_time INTEGER NOT NULL,
  end_time INTEGER,
  duration INTEGER,
  status INTEGER DEFAULT 0 CHECK (status IN (0, 1, 2)),
  progress TEXT,
  FOREIGN KEY (user_id) REFERENCES users(id)
);
CREATE TABLE IF NOT EXISTS records (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  history_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  start_time INTEGER NOT NULL,
  end_time INTEGER NOT NULL,
  progress REAL NOT NULL CHECK (progress >= 0 AND progress <= 100),
  duration INTEGER NOT NULL,
  start_chapter INTEGER,
  end_chapter INTEGER,
  record_time INTEGER NOT NULL,
  FOREIGN KEY (history_id) REFERENCES history(id),
  FOREIGN KEY (user_id) REFERENCES users(id)
);
CREATE TABLE IF NOT EXISTS notes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  uuid TEXT UNIQUE,
  content TEXT,
  type TEXT NOT NULL CHECK (type IN ('txt','image','html','video','sound','binary')),
  create_time INTEGER NOT NULL,
  update_time INTEGER,
  user_id INTEGER NOT NULL,
  chapter_id INTEGER,
  history_id INTEGER,
  records_id INTEGER,
  range_start INTEGER,
  range_end INTEGER,
  original TEXT,
  FOREIGN KEY (user_id) REFERENCES users(id),
  FOREIGN KEY (history_id) REFERENCES history(id),
  FOREIGN KEY (records_id) REFERENCES records(id)
);
CREATE TABLE IF NOT EXISTS inspiration (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  uuid TEXT UNIQUE,
  type TEXT NOT NULL CHECK (type IN ('txt','image','html','video','sound','binary')),
  content TEXT,
  create_time INTEGER NOT NULL,
  update_time INTEGER,
  user_id INTEGER NOT NULL,
  chapter_id INTEGER,
  history_id INTEGER,
  records_id INTEGER,
  FOREIGN KEY (user_id) REFERENCES users(id),
  FOREIGN KEY (history_id) REFERENCES history(id),
  FOREIGN KEY (records_id) REFERENCES records(id)
);
CREATE TABLE IF NOT EXISTS reviews (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  uuid TEXT UNIQUE,
  content TEXT NOT NULL,
  user_id INTEGER NOT NULL,
  create_time INTEGER NOT NULL,
  update_time INTEGER,
  FOREIGN KEY (user_id) REFERENCES users(id)
);
CREATE TABLE IF NOT EXISTS ratings (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  uuid TEXT UNIQUE,
  user_id INTEGER NOT NULL,
  rating INTEGER NOT NULL CHECK (rating >= 1 AND rating <= 5),
  create_time INTEGER NOT NULL,
  update_time INTEGER,
  FOREIGN KEY (user_id) REFERENCES users(id)
);
COMMIT;
)SQL";
    if (!exec(kSchema)) return false;
    // v1.1 迁移:旧库(v1.0 建表)缺 uuid 列 —— ALTER 补列并为存量行回填 UUID
    auto hasColumn = [&](const char* table, const char* col) {
        bool found = false;
        sqlite3_stmt* st = nullptr;
        std::string q = std::string("PRAGMA table_info(") + table + ")";
        if (sqlite3_prepare_v2(db_, q.c_str(), -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char* name = (const char*)sqlite3_column_text(st, 1);
                if (name && col == std::string(name)) { found = true; break; }
            }
            sqlite3_finalize(st);
        }
        return found;
    };
    for (const char* t : {"notes", "inspiration", "reviews", "ratings"}) {
        if (!hasColumn(t, "uuid")) {
            exec(std::string("ALTER TABLE ") + t + " ADD COLUMN uuid TEXT");
        }
        // 回填存量行的 uuid
        std::vector<long long> ids;
        sqlite3_stmt* st = nullptr;
        std::string q = std::string("SELECT id FROM ") + t + " WHERE uuid IS NULL OR uuid=''";
        if (sqlite3_prepare_v2(db_, q.c_str(), -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) ids.push_back(sqlite3_column_int64(st, 0));
            sqlite3_finalize(st);
        }
        for (long long rid : ids) {
            std::string u = genUuid();
            sqlite3_stmt* up = nullptr;
            std::string uq = std::string("UPDATE ") + t + " SET uuid=? WHERE id=?";
            if (sqlite3_prepare_v2(db_, uq.c_str(), -1, &up, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(up, 1, u.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(up, 2, rid);
                sqlite3_step(up);
                sqlite3_finalize(up);
            }
        }
    }
    return true;
}

bool Book::ensureUser() {
    // 本地单用户:identifier = "local"
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id FROM users WHERE identifier='local'", -1, &st, nullptr);
    if (sqlite3_step(st) == SQLITE_ROW) {
        userId_ = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
        return true;
    }
    sqlite3_finalize(st); st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO users(identifier,name) VALUES('local','本地读者')", -1, &st, nullptr);
    bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (ok) userId_ = sqlite3_last_insert_rowid(db_);
    return ok;
}

} // namespace ex5
