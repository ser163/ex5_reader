#pragma once
// ex5book.h — EX5 容器读写层(RFC EX5-001, Version 1.1)
//
// .ex5 = ZIP 归档:
//   book_data/info.json       书籍元数据
//   book_data/chapters.json   章节结构
//   book_data/resources.json  资源元数据
//   resources/...             资源文件(章节文本等)
//   read_data.db              用户数据 SQLite 库
//   meta.xml                  协议元数据(版本/加密范围)
//
// v1.1(§5.4 多用户共享批注):未加密文件中,notes/inspiration/reviews/ratings
// 对所有持文件者只读共享(带作者名),仅作者本人可编辑/删除;uuid 列用于跨用户
// 跨设备合并去重;history/records 仍为个人私有数据。

#include <string>
#include <vector>
#include <optional>
#include <utility>

#include "miniz.h"
#include "miniz_zip.h"

struct sqlite3;

namespace ex5 {

struct Chapter {
    int index = 0;
    std::string title;
    std::vector<int> resourceIds;
};

struct Resource {
    int id = 0;
    std::string content;   // resources/ 下的文件名
    std::string type;      // txt/image/html/video/sound/binary
    std::string resType;
};

struct Note {
    long long id = 0;
    std::string uuid;        // v1.1:合并去重用的全局唯一 id(UUID v4)
    std::string content;     // 批注/笔记内容(可能为空)
    std::string type;
    long long createTime = 0;
    long long updateTime = 0;
    int chapterId = 0;
    bool hasRange = false;
    long long rangeStart = 0, rangeEnd = 0;
    std::string original;    // 划线/摘抄的原文
    std::string author;      // 作者显示名(v1.1 共享阅读,join users.name)
    bool own = true;         // 是否当前用户的记录(仅 own 可编辑/删除)
};

struct Inspiration {
    long long id = 0;
    std::string uuid;
    std::string content;
    long long createTime = 0;
    long long updateTime = 0;
    int chapterId = 0;
    std::string author;
    bool own = true;
};

struct Review {
    long long id = 0;
    std::string uuid;
    std::string content;
    long long createTime = 0;
    std::string author;
    bool own = true;
};

struct Rating {
    std::string author;      // 评分者显示名
    int stars = 0;
    long long updateTime = 0;
    bool own = false;        // 是否当前用户的评分
};

class Book {
public:
    Book() = default;
    ~Book();
    Book(const Book&) = delete;
    Book& operator=(const Book&) = delete;
    Book(Book&& o) noexcept { *this = std::move(o); }
    Book& operator=(Book&& o) noexcept {
        if (this != &o) {
            close_();                 // 关闭自身 db + zip,清理工作文件
            o.resetZip_();            // 源 zip reader 先收尾(缓冲随 zipMem_ 移动)
            path_ = std::move(o.path_);
            pathW_ = std::move(o.pathW_);
            dbTmpPathW_ = std::move(o.dbTmpPathW_);
            db_ = o.db_;            o.db_ = nullptr;
            userId_ = o.userId_;
            title_ = std::move(o.title_);
            publisher_ = std::move(o.publisher_);
            version_ = std::move(o.version_);
            authors_ = std::move(o.authors_);
            wordCount_ = o.wordCount_;
            chapterCount_ = o.chapterCount_;
            chapters_ = std::move(o.chapters_);
            resources_ = std::move(o.resources_);
            zipMem_ = std::move(o.zipMem_);
            openMode_ = std::move(o.openMode_);
            o.dbTmpPathW_.clear();   // 防止对方析构时删除工作库
            // 重新挂接 ZIP reader(内存模式直接复用已移动的缓冲,零拷贝)
            if (!path_.empty()) { std::string e; openZip_(e); }
        }
        return *this;
    }

    // 打开 .ex5 文件;加密文件(encrypt_scope != 0)会拒绝并返回 false。
    bool open(const std::string& path, std::string& err);
    // 把 read_data.db 写回 .ex5(替换 ZIP 内同名条目)。
    bool save(std::string& err);
    bool isOpen() const { return db_ != nullptr; }

    // ---- 元数据 ----
    const std::string& path()        const { return path_; }
    const std::string& title()       const { return title_; }
    const std::vector<std::string>& authors() const { return authors_; }
    const std::string& publisher()   const { return publisher_; }
    const std::string& version()     const { return version_; }
    int wordCount()    const { return wordCount_; }
    int chapterCount() const { return chapterCount_; }
    const std::vector<Chapter>& chapters() const { return chapters_; }
    const Chapter* findChapter(int index) const;

    // ---- 阅读 ----
    // 读取章节正文(拼接其 txt/html 资源,UTF-8)。
    std::string chapterText(int chapterIndex) const;
    // 记录一次阅读进度:history 存 JSON 位置,records 存百分比。
    bool recordProgress(int chapterIndex, long long charOffset, long long totalChars);

    // ---- 用户数据 ----
    // 划线/摘抄/笔记统一写入 notes 表:
    //   划线: range + original,content 为可选批注
    //   摘抄: range + original,content 为空
    //   笔记: 仅 content
    long long addNote(const std::string& content, int chapterId,
                      bool hasRange, long long rangeStart, long long rangeEnd,
                      const std::string& original);
    bool deleteNote(long long id);
    // 更新笔记正文(笔记本编辑保存用),同时刷新 update_time;仅作者本人可改(SQL 层强制)
    bool updateNote(long long id, const std::string& content);
    // v1.1 共享阅读:返回**所有用户**的记录,带 author/own 标记
    std::vector<Note> listNotes();

    long long addInspiration(const std::string& content, int chapterId);
    bool deleteInspiration(long long id);
    // 更新心得正文(心得集编辑保存用),同时刷新 update_time;仅作者本人可改
    bool updateInspiration(long long id, const std::string& content);
    std::vector<Inspiration> listInspirations();

    long long addReview(const std::string& content);
    std::vector<Review> listReviews();
    bool addRating(int stars);                       // 1-5,同一用户覆盖旧评分
    std::optional<int> myRating();
    std::vector<Rating> listRatings();               // v1.1:所有用户的评分(共享阅读)

    // 最近一次阅读位置(来自 history.progress 的 JSON)。
    bool lastPosition(int& chapter, long long& offset);

    // 打开方式探测结果:"内存模式(...)"/"文件模式"
    const std::string& openMode() const { return openMode_; }

    // ---- 多用户(RFC §3.4.1 users 表) ----
    struct UserInfo {
        long long id = 0;
        std::string identifier;
        std::string name;
        bool hasCipher = false;      // 是否设置了密码(cipher BLOB: salt16 || SHA256)
    };
    std::vector<UserInfo> listUsers();
    // 新建用户(identifier 取用户名,重名报错);password 为空则不设密码。
    long long createUser(const std::string& name, const std::string& password, std::string& err);
    // 切换当前用户;有密码需校验。成功后后续笔记/进度都记在该用户名下。
    bool switchUser(long long id, const std::string& password, std::string& err);
    long long currentUserId() const { return userId_; }
    std::string currentUserName();

private:
    bool ensureSchema();
    bool ensureUser();
    bool exec(const std::string& sql);
    void close_();   // 关闭数据库并清理工作文件(供析构与移动赋值复用)
    bool openZip_(std::string& err);   // 探测文件大小,选择内存/文件模式并打开归档
    void resetZip_();                  // 关闭归档 reader 并释放内存缓冲

    std::string path_;            // UTF-8 字节(给错误信息显示用)
    std::wstring pathW_;          // Windows 宽字符路径(给所有 _wfopen/_wremove/size_t 之类的 API 用)
    std::wstring dbTmpPathW_;     // read_data.db 临时文件的宽字符路径
    sqlite3* db_ = nullptr;
    long long userId_ = 0;

    mz_zip_archive zr_{};              // 会话期间常开的 ZIP reader
    bool zrOpen_ = false;
    std::string zipMem_;               // 内存模式下的整文件缓冲
    std::string openMode_;

    std::string title_, publisher_, version_;
    std::vector<std::string> authors_;
    int wordCount_ = 0, chapterCount_ = 0;
    std::vector<Chapter> chapters_;
    std::vector<Resource> resources_;
    const Resource* findResource(int id) const;
};

} // namespace ex5
