# make_sample.py — 按 RFC EX5-001 生成一本示例 .ex5 书
import json, sqlite3, zipfile, os, time

OUT = os.path.join(os.path.dirname(__file__), "..", "samples", "sample_book.ex5")

CHAPTERS_TEXT = [
    ("第一章  起风了",
     "风从海上来,带着盐的味道。\n"
     "老船长站在甲板上,望着远处的地平线,一句话也不说。\n"
     "他已经出海四十年了,见过无数次风暴,也见过无数次平静的黎明。\n"
     "但每一次起风的时候,他的心依然会像年轻时那样剧烈地跳动。\n"
     "大海不会因为你的衰老而对你温柔,他说,它只会因为你的敬畏而让你活下来。\n"
     "年轻的船员们围过来,听他讲那些遥远的故事。\n"
     "在他们的眼睛里,老船长看到了当年的自己。\n"),
    ("第二章  灯塔",
     "灯塔立在最北的礁石上,像一根插进天空的针。\n"
     "守塔人每天黄昏点灯,黎明熄灯,三十年从未间断。\n"
     "有人问他,如今有了卫星导航,灯塔还有什么用呢?\n"
     "守塔人笑了笑,说:机器会坏,信号会断,但光不会撒谎。\n"
     "夜里出海的人,只要看见那束光,就知道家还在。\n"
     "灯光一圈一圈地扫过海面,像时间本身的指针。\n"
     "许多年后,守塔人不在了,灯塔依然亮着。\n"),
    ("第三章  归航",
     "归航的那一天,港口挤满了人。\n"
     "船还没有靠岸,欢呼声已经越过了海面。\n"
     "老船长最后一个下船,他回头看了一眼陪伴自己半生的船。\n"
     "潮水涨了又落,船身轻轻摇晃,像是在告别。\n"
     "他知道,自己不会再出海了。\n"
     "但他并不悲伤,因为大海已经住进了他的心里。\n"
     "从此以后,每当风起,他都能听见远方的涛声。\n"),
]

info = {
    "title": "海与灯",
    "authors": ["示例作者"],
    "translators": [],
    "pub_date": int(time.time()),
    "version": "1.0",
    "publisher": "EX5 示例出版社",
    "cover_id": 0,
    "chapter_count": len(CHAPTERS_TEXT),
    "word_count": sum(len(t) for _, t in CHAPTERS_TEXT),
}

chapters, resources = [], []
for i, (title, text) in enumerate(CHAPTERS_TEXT):
    rid = 901 + i                      # 内容资源区间 901-1001000(RFC 3.2.3)
    fname = f"chapter{i+1}.txt"
    chapters.append({"index": i + 1, "title": title, "resource_ids": [rid]})
    resources.append({"resource_id": rid, "content": fname, "type": "txt", "resType": None})

# read_data.db:按 RFC 建空表(阅读器也会自建,这里保持容器完整)
db_path = os.path.join(os.path.dirname(__file__), "_tmp_read_data.db")
if os.path.exists(db_path):
    os.remove(db_path)
conn = sqlite3.connect(db_path)
c = conn.cursor()
c.execute("CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, identifier TEXT NOT NULL UNIQUE, name TEXT, gender TEXT, birth_date INTEGER, lock INTEGER DEFAULT 0 CHECK (lock IN (0,1)), cipher BLOB)")
c.execute("CREATE TABLE history (id INTEGER PRIMARY KEY AUTOINCREMENT, read_count INTEGER DEFAULT 1, user_id INTEGER NOT NULL, start_time INTEGER NOT NULL, end_time INTEGER, duration INTEGER, status INTEGER DEFAULT 0 CHECK (status IN (0,1,2)), progress TEXT, FOREIGN KEY (user_id) REFERENCES users(id))")
c.execute("CREATE TABLE records (id INTEGER PRIMARY KEY AUTOINCREMENT, history_id INTEGER NOT NULL, user_id INTEGER NOT NULL, start_time INTEGER NOT NULL, end_time INTEGER NOT NULL, progress REAL NOT NULL CHECK (progress >= 0 AND progress <= 100), duration INTEGER NOT NULL, start_chapter INTEGER, end_chapter INTEGER, record_time INTEGER NOT NULL, FOREIGN KEY (history_id) REFERENCES history(id), FOREIGN KEY (user_id) REFERENCES users(id))")
c.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY AUTOINCREMENT, content TEXT, type TEXT NOT NULL CHECK (type IN ('txt','image','html','video','sound','binary')), create_time INTEGER NOT NULL, update_time INTEGER, user_id INTEGER NOT NULL, chapter_id INTEGER, history_id INTEGER, records_id INTEGER, range_start INTEGER, range_end INTEGER, original TEXT, FOREIGN KEY (user_id) REFERENCES users(id), FOREIGN KEY (history_id) REFERENCES history(id), FOREIGN KEY (records_id) REFERENCES records(id))")
c.execute("CREATE TABLE inspiration (id INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT NOT NULL CHECK (type IN ('txt','image','html','video','sound','binary')), content TEXT, create_time INTEGER NOT NULL, update_time INTEGER, user_id INTEGER NOT NULL, chapter_id INTEGER, history_id INTEGER, records_id INTEGER, FOREIGN KEY (user_id) REFERENCES users(id), FOREIGN KEY (history_id) REFERENCES history(id), FOREIGN KEY (records_id) REFERENCES records(id))")
c.execute("CREATE TABLE reviews (id INTEGER PRIMARY KEY AUTOINCREMENT, content TEXT NOT NULL, user_id INTEGER NOT NULL, create_time INTEGER NOT NULL, update_time INTEGER, FOREIGN KEY (user_id) REFERENCES users(id))")
c.execute("CREATE TABLE ratings (id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL, rating INTEGER NOT NULL CHECK (rating >= 1 AND rating <= 5), create_time INTEGER NOT NULL, update_time INTEGER, FOREIGN KEY (user_id) REFERENCES users(id))")
conn.commit()
conn.close()

meta = ('<?xml version="1.0" encoding="UTF-8"?>'
        '<meta><version>1.0</version><encryption>AES-256</encryption>'
        '<encrypt_scope>0</encrypt_scope></meta>')

with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("book_data/info.json", json.dumps(info, ensure_ascii=False))
    z.writestr("book_data/chapters.json", json.dumps(chapters, ensure_ascii=False))
    z.writestr("book_data/resources.json", json.dumps(resources, ensure_ascii=False))
    for i, (_, text) in enumerate(CHAPTERS_TEXT):
        z.writestr(f"resources/chapter{i+1}.txt", text)
    z.write(db_path, "read_data.db")
    z.writestr("meta.xml", meta)

os.remove(db_path)
print("生成:", os.path.abspath(OUT))
