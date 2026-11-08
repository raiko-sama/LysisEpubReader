#include "config.h"
#include "epub.h"
#include "htmltext.h"
#include "util.h"
#include "bridge.h"

#include <miniz.h>
#include <tinyxml2.h>
#include <esp32-hal-psram.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <cstring>
#include <cctype>
#include <unordered_map>

using namespace tinyxml2;

#if DIAG_LOG && !defined(HOST_TEST) && !defined(HOST_GUI)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define EPUB_DIAG(fmt, ...) do { char _b[128]; snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); brd_log(_b); } while (0)
#define EPUB_STACK() do { char _b[64]; snprintf(_b, sizeof(_b), "  stack=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL)); brd_log(_b); } while (0)
#else
#define EPUB_DIAG(...) ((void)0)
#define EPUB_STACK() ((void)0)
#endif

#define ZIP_MAX_XML     (512u * 1024u)   /* container/opf/ncx/nav */
#define ZIP_MAX_CHAPTER (2u * 1024u * 1024u)
#define ZIP_MAX_IMAGE   (8u * 1024u * 1024u)
#define ZIP_MAX_RATIO   200

struct Chapter {
    std::string href;   // resolved path inside zip
    std::string title;
};

struct Epub {
    mz_zip_archive zip;
    bool  is_open = false;
    std::string path;
    std::string opf_dir;
    std::string title;
    std::string author;
    std::vector<Chapter> ch;
    std::string cover_href;
    std::string nav_href;   /* EPUB3 toc document */
    std::vector<std::pair<std::string, std::string>> manifest; // id -> resolved href
    std::vector<uint8_t> scratch;   /* reusable extraction buffer (grow-only) */
    std::vector<std::pair<std::string, int>> zip_cache; /* name -> index */
    int lookup_misses = 0;          /* instrumentation: central directory scans */
};

// ---------------------------------------------------------------- helpers

static std::string percent_decode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size() && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
            auto hex = [](char c) { return isdigit((unsigned char)c) ? c - '0' : (tolower(c) - 'a' + 10); };
            out.push_back((char)(hex(in[i + 1]) * 16 + hex(in[i + 2])));
            i += 2;
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

static std::string normalize(const std::string& dir, const std::string& href)
{
    std::string path = href;
    if (!path.empty() && path[0] == '/') path = path.substr(1);
    else if (!dir.empty()) path = dir + "/" + path;

    std::vector<std::string> parts;
    parts.reserve(8);
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    std::string out;
    out.reserve(path.size());
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out += "/";
        out += parts[i];
    }
    return out;
}

static std::string svg_image_href(const std::string& data)
{
    const char* p = data.c_str();
    const char* end = p + data.size();
    while ((p = strstr(p, "href")) != nullptr) {
        const char* q = p + 4;
        while (q < end && (*q == ' ' || *q == '=')) q++;
        if (q < end && (*q == '"' || *q == '\'')) {
            char quote = *q++;
            const char* e = q;
            while (e < end && *e != quote) e++;
            if (e > q) return std::string(q, (size_t)(e - q));
        }
        p += 4;
    }
    return std::string();
}

/* resolve an entry name to its zip index; the name->index map is cached
   per book so repeated extractions (chapters, images) skip the central
   directory search */
static int zip_find_index(Epub* e, const std::string& name)
{
    auto cmp = [](const std::pair<std::string, int>& z, const std::string& n) {
        return z.first < n;
    };
    auto pos = std::lower_bound(e->zip_cache.begin(), e->zip_cache.end(), name, cmp);
    if (pos != e->zip_cache.end() && pos->first == name) return pos->second;
    e->lookup_misses++;
    int idx = mz_zip_reader_locate_file(&e->zip, name.c_str(), nullptr, 0);
    if (idx < 0) {
        // case-insensitive fallback (exact length match only)
        mz_uint n = mz_zip_reader_get_num_files(&e->zip);
        size_t nl = name.size();
        for (mz_uint i = 0; i < n; i++) {
            char buf[512];
            if (!mz_zip_reader_get_filename(&e->zip, i, buf, sizeof(buf))) continue;
            if (strlen(buf) == nl && u_icmp(buf, name.c_str(), nl) == 0) { idx = (int)i; break; }
        }
    }
    if (idx >= 0) e->zip_cache.insert(pos, { name, idx });
    return idx;
}

static bool zip_read(Epub* e, const std::string& name, std::string& out, size_t max_uncomp)
{
    int idx = zip_find_index(e, name);
    if (idx < 0) return false;
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&e->zip, (mz_uint)idx, &st)) return false;
    size_t need = (size_t)st.m_uncomp_size;
    if (!need) return false;
    EPUB_DIAG("zip_read: %s idx=%d comp=%llu uncomp=%llu method=%u",
              name.c_str(), idx,
              (unsigned long long)st.m_comp_size,
              (unsigned long long)st.m_uncomp_size, (unsigned)st.m_method);
    /* Q1: refuse BEFORE allocating: size cap and zip-bomb ratio */
    if (max_uncomp && need > max_uncomp) return false;
#if OPT_ZIP_GUARD
    if (st.m_comp_size > 0 && need / st.m_comp_size > ZIP_MAX_RATIO) return false;
#endif
    out.resize(need);
    EPUB_STACK();
#if DIAG_LOG
    {
        /* bring-up: hex-dump the raw compressed entry (local header + data)
           so the exact bytes can be replayed on the host */
        char hex[96];
        FILE* f = fopen(e->path.c_str(), "rb");
        size_t want = 30 + (size_t)st.m_comp_size + 128;
        uint8_t* raw = (uint8_t*)malloc(want);
        if (f && raw) {
            fseek(f, (long)st.m_local_header_ofs, SEEK_SET);
            size_t got = fread(raw, 1, want, f);
            for (size_t i = 0; i < got && i < 192; i += 16) {
                int n = 0;
                for (size_t j = i; j < i + 16 && j < got; j++)
                    n += snprintf(hex + n, sizeof(hex) - n, "%02x ", raw[j]);
                EPUB_DIAG("  raw[%03u] %s", (unsigned)i, hex);
            }
        }
        if (f) fclose(f);
        free(raw);
    }
#endif
#if DIAG_LOG
    {
        /* bisection probe: the raw read path WITHOUT the tinfl */
        void* probe = malloc((size_t)st.m_comp_size);
        if (probe) {
            int okr = mz_zip_reader_extract_to_mem(&e->zip, (mz_uint)idx, probe,
                                                   (size_t)st.m_comp_size,
                                                   MZ_ZIP_FLAG_COMPRESSED_DATA);
            EPUB_DIAG("zip_read: compressed-data probe ok=%d", okr);
            free(probe);
        }
        EPUB_STACK();
    }
#endif
    /* miniz verifies the entry CRC32 after extraction (MZ_CRC_ERROR) */
    if (!mz_zip_reader_extract_to_mem(&e->zip, (mz_uint)idx, &out[0], need, 0)) return false;
    EPUB_DIAG("zip_read: %s extracted", name.c_str());
    EPUB_STACK();
    return true;
}

static char* dup_psram(const std::string& s, size_t* out_len)
{
    char* p = (char*)ps_malloc(s.size() + 1);
    if (!p) p = (char*)malloc(s.size() + 1);
    if (!p) return nullptr;
    memcpy(p, s.data(), s.size());
    p[s.size()] = 0;
    if (out_len) *out_len = s.size();
    return p;
}

// ---------------------------------------------------------------- open

static bool parse_container(Epub* e, std::string& opf_path)
{
    EPUB_DIAG("epub: container parse...");
    EPUB_STACK();
    std::string xml;
    if (!zip_read(e, "META-INF/container.xml", xml, ZIP_MAX_XML)) return false;
    EPUB_DIAG("epub: container.xml read (%u bytes)", (unsigned)xml.size());
    EPUB_STACK();
    XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS) return false;
    EPUB_DIAG("epub: container parsed");
    EPUB_STACK();
    XMLElement* root = doc.FirstChildElement("container");
    if (!root) return false;
    for (XMLElement* rf = root->FirstChildElement("rootfiles"); rf; rf = rf->NextSiblingElement("rootfiles")) {
        for (XMLElement* r = rf->FirstChildElement("rootfile"); r; r = r->NextSiblingElement("rootfile")) {
            const char* fp = r->Attribute("full-path");
            if (fp) { opf_path = fp; return true; }
        }
    }
    return false;
}

static bool parse_opf(Epub* e, const std::string& opf_path)
{
    EPUB_DIAG("epub: opf parse: %s", opf_path.c_str());
    EPUB_STACK();
    std::string xml;
    if (!zip_read(e, opf_path, xml, ZIP_MAX_XML)) return false;
    EPUB_DIAG("epub: opf read (%u bytes)", (unsigned)xml.size());
    EPUB_STACK();

    size_t slash = opf_path.rfind('/');
    e->opf_dir = (slash == std::string::npos) ? "" : opf_path.substr(0, slash);

    XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != XML_SUCCESS) return false;
    EPUB_DIAG("epub: opf parsed");
    EPUB_STACK();
    XMLElement* package = doc.FirstChildElement("package");
    if (!package) return false;

    if (XMLElement* meta = package->FirstChildElement("metadata")) {
        for (XMLElement* el = meta->FirstChildElement(); el; el = el->NextSiblingElement()) {
            const char* n = el->Name();
            if (!n) continue;
            const char* t = el->GetText();
            if (!t) continue;
            if (!strcmp(n, "dc:title") || !strcmp(n, "title")) { if (e->title.empty()) e->title = t; }
            else if (!strcmp(n, "dc:creator") || !strcmp(n, "creator")) { if (e->author.empty()) e->author = t; }
        }
    }

    std::string ncx_href;
    std::string cover_id;
    std::unordered_map<std::string, std::pair<int, int>> manifest_idx;

    XMLElement* manifest = package->FirstChildElement("manifest");
    if (manifest) {
        for (XMLElement* item = manifest->FirstChildElement("item"); item; item = item->NextSiblingElement("item")) {
            const char* id = item->Attribute("id");
            const char* href = item->Attribute("href");
            const char* media = item->Attribute("media-type");
            const char* props = item->Attribute("properties");
            if (!id || !href) continue;
            std::string resolved = normalize(e->opf_dir, percent_decode(href));
            int mpos = (int)e->manifest.size();
            e->manifest.push_back({ id, resolved });
            auto mi = manifest_idx.find(id);
            if (mi == manifest_idx.end()) manifest_idx.emplace(id, std::make_pair(mpos, mpos));
            else mi->second.second = mpos;
            if (media && !strcmp(media, "application/x-dtbncx+xml")) ncx_href = resolved;
            if (props && strstr(props, "cover-image")) e->cover_href = resolved;
            if (props && strstr(props, "nav")) e->nav_href = resolved;
        }
        if (XMLElement* meta = package->FirstChildElement("metadata")) {
            for (XMLElement* m = meta->FirstChildElement("meta"); m; m = m->NextSiblingElement("meta")) {
                const char* n = m->Attribute("name");
                const char* c = m->Attribute("content");
                if (n && c && !strcmp(n, "cover")) cover_id = c;
            }
        }
        if (!cover_id.empty()) {
            auto mi = manifest_idx.find(cover_id);
            if (mi != manifest_idx.end()) e->cover_href = e->manifest[mi->second.second].second;
        }
    }

    XMLElement* spine = package->FirstChildElement("spine");
    if (spine) {
        for (XMLElement* ref = spine->FirstChildElement("itemref"); ref; ref = ref->NextSiblingElement("itemref")) {
            const char* idref = ref->Attribute("idref");
            if (!idref) continue;
            auto mi = manifest_idx.find(idref);
            if (mi != manifest_idx.end()) {
                Chapter c;
                c.href = e->manifest[mi->second.first].second;
                e->ch.push_back(c);
            }
        }
        if (e->ch.empty()) {
            // fall back to spine order from manifest xhtml entries
            for (auto& kv : e->manifest) {
                if (kv.second.size() > 5 &&
                    (!strcasecmp(kv.second.c_str() + kv.second.size() - 5, ".html") ||
                     !strcasecmp(kv.second.c_str() + kv.second.size() - 6, ".xhtml"))) {
                    Chapter c; c.href = kv.second; e->ch.push_back(c);
                }
            }
        }
    }

    // NCX titles
    if (!ncx_href.empty()) {
        std::string ncx;
        if (zip_read(e, ncx_href, ncx, ZIP_MAX_XML)) {
            XMLDocument nd;
            if (nd.Parse(ncx.c_str(), ncx.size()) == XML_SUCCESS) {
                /* iterative navPoint walk (explicit stack): a pathological
                   TOC must not blow the loop task stack */
                const int MAXWALK = 256;
                XMLElement* stk[MAXWALK];
                int sp = 0;
                if (XMLElement* ncxel = nd.FirstChildElement("ncx")) {
                    if (XMLElement* navMap = ncxel->FirstChildElement("navMap")) {
                        XMLElement* np = navMap->FirstChildElement("navPoint");
                        for (; np; np = np->NextSiblingElement("navPoint")) {
                            if (sp < MAXWALK) stk[sp++] = np;
                        }
                    }
                }
                while (sp > 0) {
                    XMLElement* np = stk[--sp];
                    std::string label, src;
                    if (XMLElement* nl = np->FirstChildElement("navLabel")) {
                        if (XMLElement* t = nl->FirstChildElement("text")) {
                            if (t->GetText()) label = t->GetText();
                        }
                    }
                    if (XMLElement* c = np->FirstChildElement("content")) {
                        if (c->Attribute("src")) src = c->Attribute("src");
                    }
                    size_t hash = src.find('#');
                    if (hash != std::string::npos) src.resize(hash);
                    if (!src.empty()) {
                        std::string full = normalize(e->opf_dir, percent_decode(src));
                        for (auto& ch : e->ch) {
                            if (ch.href == full && ch.title.empty() && !label.empty()) ch.title = label;
                        }
                    }
                    /* push the nested navPoints for the next levels */
                    for (XMLElement* child = np->FirstChildElement("navPoint");
                         child; child = child->NextSiblingElement("navPoint")) {
                        if (sp < MAXWALK) stk[sp++] = child;
                    }
                }
            }
        }
    }

    // EPUB3 nav document: fill chapter titles still empty (no NCX needed)
    if (!e->nav_href.empty()) {
        std::string nav;
        if (zip_read(e, e->nav_href, nav, ZIP_MAX_XML)) {
            size_t pos = 0;
            while ((pos = nav.find("<a", pos)) != std::string::npos) {
                size_t tag_end = nav.find('>', pos);
                if (tag_end == std::string::npos) break;
                std::string tag = nav.substr(pos, tag_end - pos + 1);
                std::string href;
                size_t hpos = tag.find("href");
                if (hpos != std::string::npos) {
                    size_t eq = tag.find('=', hpos);
                    if (eq != std::string::npos) {
                        size_t q1 = eq + 1;
                        while (q1 < tag.size() && isspace((unsigned char)tag[q1])) q1++;
                        if (q1 < tag.size() && (tag[q1] == '"' || tag[q1] == '\'')) {
                            char qc = tag[q1++];
                            size_t q2 = tag.find(qc, q1);
                            if (q2 != std::string::npos) href = tag.substr(q1, q2 - q1);
                        }
                    }
                }
                size_t text_end = nav.find("</a>", tag_end);
                std::string label;
                if (text_end != std::string::npos) {
                    label = nav.substr(tag_end + 1, text_end - tag_end - 1);
                    /* strip nested markup */
                    std::string clean;
                    bool in = false;
                    for (char c : label) {
                        if (c == '<') in = true;
                        else if (c == '>') in = false;
                        else if (!in) clean.push_back(c);
                    }
                    label = clean;
                }
                size_t hash = href.find('#');
                if (hash != std::string::npos) href.resize(hash);
                if (!href.empty() && !label.empty()) {
                    std::string full = normalize(e->opf_dir, percent_decode(href));
                    for (auto& c : e->ch) {
                        if (c.href == full && c.title.empty()) { c.title = label; break; }
                    }
                }
                pos = tag_end + 1;
            }
        }
    }
    return true;
}

extern "C" Epub* epub_open(const char* path)
{
    Epub* e = new Epub();
    e->is_open = false;
    e->path = path;
    EPUB_DIAG("epub: zip init: %s", path);
    EPUB_STACK();
    if (!mz_zip_reader_init_file(&e->zip, path, 0)) { delete e; return nullptr; }
    e->is_open = true;
    EPUB_DIAG("epub: zip init OK (files=%d)", (int)mz_zip_reader_get_num_files(&e->zip));
    EPUB_STACK();

    std::string opf_path;
    if (!parse_container(e, opf_path) || !parse_opf(e, opf_path)) {
        EPUB_DIAG("epub: open FAILED (container/opf)");
        mz_zip_reader_end(&e->zip);
        delete e;
        return nullptr;
    }
    EPUB_DIAG("epub: open OK (ch=%d)", (int)e->ch.size());
    EPUB_STACK();
    return e;
}

extern "C" void epub_close(Epub* e)
{
    if (!e) return;
    if (e->is_open) mz_zip_reader_end(&e->zip);
    delete e;
}

extern "C" const char* epub_title(Epub* e) { return e ? e->title.c_str() : ""; }
extern "C" const char* epub_author(Epub* e) { return e ? e->author.c_str() : ""; }
extern "C" int epub_chapter_count(Epub* e) { return e ? (int)e->ch.size() : 0; }
extern "C" const char* epub_chapter_title(Epub* e, int i)
{
    if (!e || i < 0 || i >= (int)e->ch.size()) return "";
    return e->ch[i].title.c_str();
}
extern "C" const char* epub_chapter_href(Epub* e, int i)
{
    if (!e || i < 0 || i >= (int)e->ch.size()) return "";
    return e->ch[i].href.c_str();
}
extern "C" const char* epub_cover_href(Epub* e) { return e ? e->cover_href.c_str() : ""; }

extern "C" char* epub_chapter_text(Epub* e, int i, size_t* out_len)
{
    if (!e || i < 0 || i >= (int)e->ch.size()) return nullptr;
    std::string html;
    if (!zip_read(e, e->ch[i].href, html, ZIP_MAX_CHAPTER)) return nullptr;
    return ht_to_text(html.c_str(), html.size(), out_len);
}

extern "C" char* epub_entry_extract(Epub* e, const char* name, size_t* out_len)
{
    if (!e) return nullptr;
    std::string data;
    if (!zip_read(e, normalize(e->opf_dir, percent_decode(name)), data, ZIP_MAX_IMAGE)) return nullptr;
    return dup_psram(data, out_len);
}

extern "C" char* epub_image(Epub* e, const char* href, size_t* out_len)
{
    if (!e || !href) return nullptr;
    std::string data;
    std::string resolved = normalize(e->opf_dir, percent_decode(href));
    if (!zip_read(e, resolved, data, ZIP_MAX_IMAGE)) return nullptr;
    return dup_psram(data, out_len);
}

extern "C" char* epub_image_from(Epub* e, int chapter, const char* href, size_t* out_len)
{
    if (!e || !href || chapter < 0 || chapter >= (int)e->ch.size()) return nullptr;
    std::string ch_href = e->ch[chapter].href;
    size_t slash = ch_href.rfind('/');
    std::string ch_dir = (slash == std::string::npos) ? std::string() : ch_href.substr(0, slash);
    std::string data;
    std::string resolved = normalize(ch_dir, percent_decode(href));
    if (!zip_read(e, resolved, data, ZIP_MAX_IMAGE)) return nullptr;
    return dup_psram(data, out_len);
}

extern "C" char* epub_cover(Epub* e, size_t* out_len)
{
    if (!e || e->cover_href.empty()) return nullptr;
    std::string data;
    if (!zip_read(e, e->cover_href, data, ZIP_MAX_IMAGE)) return nullptr;
    if (!data.empty() && data[0] == '<') {
        std::string href = svg_image_href(data);
        if (!href.empty() && href.rfind("data:", 0) != 0) {
            size_t slash = e->cover_href.rfind('/');
            std::string dir = (slash == std::string::npos) ? std::string() : e->cover_href.substr(0, slash);
            std::string resolved = normalize(dir, percent_decode(href));
            std::string inner;
            if (zip_read(e, resolved, inner, ZIP_MAX_IMAGE)) data.swap(inner);
        }
    }
    return dup_psram(data, out_len);
}

extern "C" int epub_lookup_misses(Epub* e) { return e ? e->lookup_misses : 0; }
