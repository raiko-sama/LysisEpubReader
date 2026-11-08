#include "config.h"
#if defined(__GNUC__) && defined(OPT_HOT_O2) && (OPT_HOT_O2)
#pragma GCC optimize("O2")   /* I5: the HTML tokenizer runs on every chapter */
#endif
#include "htmltext.h"
#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct { const char* name; const char* utf8; } entity_t;

// Entity table matching the ones referenced by the original firmware
// (strings seen in the binary) plus common HTML4 entities.
static const entity_t entities[] = {
    {"amp","&"},{"lt","<"},{"gt",">"},{"quot","\""},{"apos","'"},
    {"nbsp"," "},{"iexcl","!"},{"cent","c"},{"pound","GBP"},{"curren","c"},
    {"yen","Y"},{"brvbar","|"},{"sect","S"},{"uml","\""},{"copy","(c)"},
    {"ordf","a"},{"laquo","\""},{"not","!"},{"shy","-"},{"reg","(R)"},
    {"macr","-"},{"deg","deg"},{"plusmn","+-"},{"sup2","2"},{"sup3","3"},
    {"acute","'"},{"micro","u"},{"para","P"},{"middot","."},{"cedil",","},
    {"sup1","1"},{"ordm","o"},{"raquo","\""},{"frac14","1/4"},{"frac12","1/2"},
    {"frac34","3/4"},{"iquest","?"},{"Agrave","\xC3\x80"},{"Aacute","\xC3\x81"},{"Acirc","\xC3\x82"},
    {"Atilde","\xC3\x83"},{"Auml","\xC3\x84"},{"Aring","\xC3\x85"},{"AElig","\xC3\x86"},{"Ccedil","\xC3\x87"},
    {"Egrave","\xC3\x88"},{"Eacute","\xC3\x89"},{"Ecirc","\xC3\x8A"},{"Euml","\xC3\x8B"},{"Igrave","\xC3\x8C"},
    {"Iacute","\xC3\x8D"},{"Icirc","\xC3\x8E"},{"Iuml","\xC3\x8F"},{"ETH","\xC3\x90"},{"Ntilde","\xC3\x91"},
    {"Ograve","\xC3\x92"},{"Oacute","\xC3\x93"},{"Ocirc","\xC3\x94"},{"Otilde","\xC3\x95"},{"Ouml","\xC3\x96"},
    {"times","x"},{"Oslash","\xC3\x98"},{"Ugrave","\xC3\x99"},{"Uacute","\xC3\x9A"},{"Ucirc","\xC3\x9B"},
    {"Uuml","\xC3\x9C"},{"Yacute","\xC3\x9D"},{"THORN","\xC3\x9E"},{"szlig","\xC3\x9F"},{"agrave","\xC3\xA0"},
    {"aacute","\xC3\xA1"},{"acirc","\xC3\xA2"},{"atilde","\xC3\xA3"},{"auml","\xC3\xA4"},{"aring","\xC3\xA5"},
    {"aelig","\xC3\xA6"},{"ccedil","\xC3\xA7"},{"egrave","\xC3\xA8"},{"eacute","\xC3\xA9"},{"ecirc","\xC3\xAA"},
    {"euml","\xC3\xAB"},{"igrave","\xC3\xAC"},{"iacute","\xC3\xAD"},{"icirc","\xC3\xAE"},{"iuml","\xC3\xAF"},
    {"eth","\xC3\xB0"},{"ntilde","\xC3\xB1"},{"ograve","\xC3\xB2"},{"oacute","\xC3\xB3"},{"ocirc","\xC3\xB4"},
    {"otilde","\xC3\xB5"},{"ouml","\xC3\xB6"},{"divide","/"},{"oslash","\xC3\xB8"},{"ugrave","\xC3\xB9"},
    {"uacute","\xC3\xBA"},{"ucirc","\xC3\xBB"},{"uuml","\xC3\xBC"},{"yacute","\xC3\xBD"},{"thorn","\xC3\xBE"},
    {"yuml","\xC3\xBF"},{"OElig","OE"},{"oelig","oe"},{"Scaron","S"},{"scaron","s"},
    {"Yuml","Y"},{"fnof","f"},{"circ","^"},{"tilde","~"},{"ensp"," "},
    {"emsp"," "},{"thinsp"," "},{"zwnj",""},{"zwj",""},{"lrm",""},{"rlm",""},
    {"ndash","-"},{"mdash","--"},{"lsquo","'"},{"rsquo","'"},{"sbquo",","},
    {"ldquo","\""},{"rdquo","\""},{"bdquo","\""},{"dagger","+"},{"Dagger","++"},
    {"bull","*"},{"hellip","..."},{"permil","%%"},{"prime","'"},{"Prime","\""},
    {"lsaquo","<"},{"rsaquo",">"},{"oline","-"},{"frasl","/"},{"euro","EUR"},
    {"trade","(TM)"},{"notin","!"},{"minus","-"},
    {"lowast","*"},{"radic","sqrt"},{"infin","inf"},{"there4","th"},{"asymp","~="},
    {"equiv","=="},{"oplus","(o)"},{"otimes","(x)"},{"lceil","["},{"rceil","]"},
    {"lfloor","["},{"rfloor","]"},{"spades","S"},{"clubs","C"},{"hearts","H"},
    {"diams","D"},{"Alpha","A"},{"Beta","B"},{"Gamma","G"},{"Delta","D"},
    {"Epsilon","E"},{"Zeta","Z"},{"Eta","E"},{"Theta","Th"},{"Iota","I"},
    {"Kappa","K"},{"Lambda","L"},{"Mu","M"},{"Nu","N"},{"Xi","X"},
    {"Omicron","O"},{"Pi","P"},{"Rho","R"},{"Sigma","S"},{"Tau","T"},
    {"Upsilon","U"},{"Phi","Ph"},{"Chi","Ch"},{"Psi","Ps"},{"Omega","O"},
    {"alpha","a"},{"beta","b"},{"gamma","g"},{"delta","d"},{"epsilon","e"},
    {"zeta","z"},{"eta","e"},{"theta","th"},{"iota","i"},{"kappa","k"},
    {"lambda","l"},{"mu","m"},{"nu","n"},{"xi","x"},{"omicron","o"},
    {"pi","p"},{"rho","r"},{"sigmaf","s"},{"sigma","s"},{"tau","t"},
    {"upsilon","u"},{"phi","ph"},{"chi","ch"},{"psi","ps"},{"omega","o"},
    {"thetasym","th"},{"upsih","u"},{"piv","p"},{"crarr","<-"},{"forall","V"},
    {"part","d"},{"exist","E"},{"empty","0"},{"nabla","N"},{"isin","in"},
    {"prod","P"},{"sum","S"},{"perp","_|_"},{"and","and"},{"or","or"},
    {"cap","^"},{"cup","U"},{"int","S"},{"sim","~"},{"cong","~="},
    {"ne","!="},{"le","<="},{"ge",">="},{"sub","sub"},{"sup","sup"},
    {"nsub","!sub"},{"sube","sube"},{"supe","supe"},{"ang","/_"},
};

/* binary search over the sorted entity table: O(log n) instead of the
   previous linear scan (~170 entries per entity) */
#define ENTITY_N (sizeof(entities) / sizeof(entities[0]))
static const entity_t* ent_sorted[ENTITY_N];
static int ent_sorted_ok = 0;

static int cmp_ent(const void* a, const void* b)
{
    const entity_t* const* ea = (const entity_t* const*)a;
    const entity_t* const* eb = (const entity_t* const*)b;
    return strcmp((*ea)->name, (*eb)->name);
}

static void ent_init(void)
{
    if (ent_sorted_ok) return;
    for (size_t i = 0; i < ENTITY_N; i++) ent_sorted[i] = &entities[i];
    qsort(ent_sorted, ENTITY_N, sizeof(ent_sorted[0]), cmp_ent);
    ent_sorted_ok = 1;
}

static const char* entity_lookup(const char* name, int len)
{
    if (len <= 0 || len >= 16) return NULL;
    ent_init();
    char key[16];
    memcpy(key, name, len);
    key[len] = 0;
    entity_t probe;
    probe.name = key;
    probe.utf8 = NULL;
    const entity_t* kp = &probe;
    const entity_t* const* found = (const entity_t* const*)bsearch(
        &kp, ent_sorted, ENTITY_N, sizeof(ent_sorted[0]), cmp_ent);
    return found ? (*found)->utf8 : NULL;
}

typedef struct {
    char* buf;
    size_t cap, len;
} outbuf_t;

static void ob_putc(outbuf_t* o, char c)
{
    if (o->len + 1 < o->cap) o->buf[o->len++] = c;
}

static void ob_puts(outbuf_t* o, const char* s)
{
    size_t n = strlen(s);
    size_t avail = o->cap > o->len + 1 ? o->cap - o->len - 1 : 0;
    if (n > avail) n = avail;
    memcpy(o->buf + o->len, s, n);
    o->len += n;
}

static void ob_put_utf8(outbuf_t* o, unsigned cp)
{
    if (cp < 0x80) { ob_putc(o, (char)cp); return; }
    if (cp < 0x800) {
        ob_putc(o, (char)(0xC0 | (cp >> 6)));
        ob_putc(o, (char)(0x80 | (cp & 0x3F)));
        return;
    }
    ob_putc(o, (char)(0xE0 | (cp >> 12)));
    ob_putc(o, (char)(0x80 | ((cp >> 6) & 0x3F)));
    ob_putc(o, (char)(0x80 | (cp & 0x3F)));
}

static int is_latin1_accent(unsigned cp)
{
    return cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7;
}

static void ob_newline(outbuf_t* o, int blank)
{
    size_t guard = 0;
    while (o->len && (o->buf[o->len - 1] == ' ' || o->buf[o->len - 1] == '\t') && guard++ < 64) o->len--;
    ob_putc(o, '\n');
    if (blank) {
        if (o->len < 2 || o->buf[o->len - 2] != '\n') ob_putc(o, '\n');
    }
}

/* emit the pending space (if any) before a real character */
static void ob_flush_space(outbuf_t* o, int* pending)
{
    if (*pending) {
        if (o->len && o->buf[o->len - 1] != '\n' && o->buf[o->len - 1] != ' ') ob_putc(o, ' ');
        *pending = 0;
    }
}

#define TAG_IS(tag, len, literal) \
    ((len) == (int)(sizeof(literal) - 1) && u_icmp((tag), (literal), (size_t)(sizeof(literal) - 1)) == 0)

/* single codepoint -> ASCII approximation (same mapping as u_ascii_fold) */
static char fold_cp(uint32_t cp)
{
    if (cp >= 0xC0 && cp <= 0xC5) return 'A';
    if (cp == 0xC7) return 'C';
    if (cp >= 0xC8 && cp <= 0xCB) return 'E';
    if (cp >= 0xCC && cp <= 0xCF) return 'I';
    if (cp == 0xD1) return 'N';
    if (cp >= 0xD2 && cp <= 0xD6) return 'O';
    if (cp == 0xD8) return 'O';
    if (cp >= 0xD9 && cp <= 0xDC) return 'U';
    if (cp == 0xDD) return 'Y';
    if (cp >= 0xE0 && cp <= 0xE5) return 'a';
    if (cp == 0xE7) return 'c';
    if (cp >= 0xE8 && cp <= 0xEB) return 'e';
    if (cp >= 0xEC && cp <= 0xEF) return 'i';
    if (cp == 0xF1) return 'n';
    if (cp >= 0xF2 && cp <= 0xF6) return 'o';
    if (cp == 0xF8) return 'o';
    if (cp >= 0xF9 && cp <= 0xFC) return 'u';
    if (cp == 0xFD || cp == 0xFF) return 'y';
    if (cp >= 0x2018 && cp <= 0x201F) return '\'';
    if (cp == 0x2013 || cp == 0x2014) return '-';
    if (cp == 0x2026) return '.';
    if (cp == 0x00A0) return ' ';
    if (cp == 0x00AB || cp == 0x00BB) return '"';
    return '?';
}

char* ht_to_text(const char* html, size_t len, size_t* out_len)
{
    outbuf_t o;
    o.cap = len + 1024;
    o.buf = (char*)u_alloc(o.cap);
    if (!o.buf) return NULL;
    o.len = 0;

    const char* p = html;
    const char* end = html + len;
    int skip = 0;            /* inside <script>/<style>/<head> */
    int pending_space = 0;

    while (p < end) {
        if (skip) {
            /* skip everything until the closing script/style/head tag */
            const char* e = p;
            while (e + 9 < end) {
                if (e[0] == '<' && e[1] == '/' && u_icmp(e + 2, "script", 6) == 0) break;
                if (e[0] == '<' && e[1] == '/' && u_icmp(e + 2, "style", 5) == 0) break;
                if (e[0] == '<' && e[1] == '/' && u_icmp(e + 2, "head", 4) == 0) break;
                e++;
            }
            if (e + 9 >= end) break;
            p = e;
            skip = 0;
            continue;
        }
        if (*p == '<') {
            const char* close = memchr(p, '>', end - p);
            if (!close) break;
            const char* t = p + 1;
            int closing = 0;
            if (t < close && *t == '/') { closing = 1; t++; }
            while (t < close && isspace((unsigned char)*t)) t++;
            const char* name = t;
            int nlen = 0;
            while (t < close && (isalnum((unsigned char)*t) || *t == ':' || *t == '-' || *t == '!')) { t++; nlen++; }

            if (nlen > 0 && name[0] == '!') {
                /* comment or doctype */
                if (nlen >= 3 && strncmp(name, "!--", 3) == 0) {
                    /* find --> */
                    const char* e = p + 4;
                    while (e + 2 < end && !(e[0] == '-' && e[1] == '-' && e[2] == '>')) e++;
                    p = (e + 2 < end) ? e + 3 : end;
                    continue;
                }
                p = close + 1;
                continue;
            }

            if (TAG_IS(name, nlen, "script") || TAG_IS(name, nlen, "style") || TAG_IS(name, nlen, "head")) {
                skip = !closing;
                p = close + 1;
                continue;
            }
            if (skip) { p = close + 1; continue; }

            if ((TAG_IS(name, nlen, "img") || TAG_IS(name, nlen, "image") ||
                 TAG_IS(name, nlen, "object") || TAG_IS(name, nlen, "embed")) && !closing) {
                const char* q = p + 1;
                const char* src = NULL;
                int slen = 0;
                while (q + 3 < close) {
                    const char* attr = NULL;
                    int alen = 0;
                    if ((q[0] == 's' || q[0] == 'S') && u_icmp(q, "src", 3) == 0) { attr = q + 3; alen = 3; }
                    else if ((q[0] == 'h' || q[0] == 'H') && u_icmp(q, "href", 4) == 0) { attr = q + 4; alen = 4; }
                    else if ((q[0] == 'd' || q[0] == 'D') && u_icmp(q, "data", 4) == 0) { attr = q + 4; alen = 4; }
                    if (attr && alen) {
                        const char* e = attr;
                        while (e < close && (*e == ' ' || *e == '=')) e++;
                        if (e < close && (*e == '"' || *e == '\'')) {
                            char qc = *e++;
                            src = e;
                            while (e < close && *e != qc) e++;
                            slen = (int)(e - src);
                        }
                        break;
                    }
                    q++;
                }
                if (src && slen > 0) {
                    ob_newline(&o, 0);
                    ob_putc(&o, '\x01');
                    for (int i = 0; i < slen && o.len + 2 < o.cap; i++) ob_putc(&o, src[i]);
                    ob_putc(&o, '\x02');
                    ob_newline(&o, 0);
                }
                p = close + 1;
                continue;
            }

            if (TAG_IS(name, nlen, "br") || TAG_IS(name, nlen, "hr")) {
                ob_newline(&o, 0);
                p = close + 1;
                continue;
            }

            static const struct { const char* name; int len; } block[] = {
                {"p",1},{"div",3},{"h1",2},{"h2",2},{"h3",2},{"h4",2},{"h5",2},{"h6",2},
                {"li",2},{"tr",2},{"table",5},{"section",7},{"article",7},{"blockquote",10},
                {"pre",3},{"figcaption",10},{"figure",6},{"header",6},{"footer",6},{"nav",3},
                {"ul",2},{"ol",2},{"dl",2},{"dt",2},{"dd",2},{"td",2}
            };
            for (size_t i = 0; i < sizeof(block) / sizeof(block[0]); i++) {
                if (nlen == block[i].len && u_icmp(name, block[i].name, (size_t)block[i].len) == 0) {
                    int blank = (!closing &&
                                 (TAG_IS(name, nlen, "p") || TAG_IS(name, nlen, "h1") || TAG_IS(name, nlen, "h2") ||
                                  TAG_IS(name, nlen, "h3") || TAG_IS(name, nlen, "div") || TAG_IS(name, nlen, "section") ||
                                  TAG_IS(name, nlen, "article") || TAG_IS(name, nlen, "blockquote") || TAG_IS(name, nlen, "li")));
                    ob_newline(&o, blank);
                    pending_space = 0;
                    break;
                }
            }
            p = close + 1;
            continue;
        }

        if (*p == '&') {
            const char* semi = memchr(p, ';', end - p);
            if (semi && semi - p <= 10) {
                int elen = (int)(semi - p - 1);
                if (elen > 0) {
                    if (p[1] == '#') {
                        long cp = 0;
                        if (p[2] == 'x' || p[2] == 'X') {
                            for (int i = 3; i <= elen; i++) {
                                char c = p[i];
                                cp = cp * 16 + (isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10));
                            }
                        } else {
                            for (int i = 2; i <= elen; i++) cp = cp * 10 + (p[i] - '0');
                        }
                        if (cp > 0 && cp < 0x80) { ob_flush_space(&o, &pending_space); ob_putc(&o, (char)cp); }
                        else if (cp >= 0x80 && cp < 0x100) {
                            if (is_latin1_accent((unsigned)cp)) {
                                ob_flush_space(&o, &pending_space);
                                ob_put_utf8(&o, (unsigned)cp);
                            } else {
                            char rep = '?';
                            if (cp >= 0xC0 && cp <= 0xC5) rep = 'A';
                            else if (cp == 0xC7) rep = 'C';
                            else if (cp >= 0xC8 && cp <= 0xCB) rep = 'E';
                            else if (cp >= 0xCC && cp <= 0xCF) rep = 'I';
                            else if (cp == 0xD1) rep = 'N';
                            else if (cp >= 0xD2 && cp <= 0xD6) rep = 'O';
                            else if (cp == 0xD8) rep = 'O';
                            else if (cp >= 0xD9 && cp <= 0xDC) rep = 'U';
                            else if (cp == 0xDD) rep = 'Y';
                            else if (cp >= 0xE0 && cp <= 0xE5) rep = 'a';
                            else if (cp == 0xE7) rep = 'c';
                            else if (cp >= 0xE8 && cp <= 0xEB) rep = 'e';
                            else if (cp >= 0xEC && cp <= 0xEF) rep = 'i';
                            else if (cp == 0xF1) rep = 'n';
                            else if (cp >= 0xF2 && cp <= 0xF6) rep = 'o';
                            else if (cp == 0xF8) rep = 'o';
                            else if (cp >= 0xF9 && cp <= 0xFC) rep = 'u';
                            else if (cp == 0xFD || cp == 0xFF) rep = 'y';
                            else if (cp == 0xA0) rep = ' ';
                            else if (cp == 0xDF) rep = 's';
                            else if (cp == 0xE6) rep = 'a';
                            else if (cp == 0xC6) rep = 'A';
                            ob_flush_space(&o, &pending_space);
                            ob_putc(&o, rep);
                            }
                        } else if (cp == 0x2018 || cp == 0x2019) { ob_flush_space(&o, &pending_space); ob_putc(&o, '\''); }
                        else if (cp == 0x201C || cp == 0x201D) { ob_flush_space(&o, &pending_space); ob_putc(&o, '"'); }
                        else if (cp == 0x2013 || cp == 0x2014) { ob_flush_space(&o, &pending_space); ob_putc(&o, '-'); }
                        else if (cp == 0x2026) { ob_flush_space(&o, &pending_space); ob_puts(&o, "..."); }
                        else if (cp == 0x00A0) { ob_flush_space(&o, &pending_space); ob_putc(&o, ' '); }
                        else { ob_flush_space(&o, &pending_space); ob_putc(&o, '?'); }
                        p = semi + 1;
                        continue;
                    }
                    const char* rep = entity_lookup(p + 1, elen);
                    if (rep) {
                        if (rep[0] == ' ') pending_space = 1;
                        else { ob_flush_space(&o, &pending_space); ob_puts(&o, rep); }
                        p = semi + 1;
                        continue;
                    }
                }
            }
            ob_putc(&o, '&');
            p++;
            continue;
        }

        unsigned char c = (unsigned char)*p;
        if (c == '<') { p++; continue; }
        if (isspace(c)) {
            pending_space = 1;
            p++;
            continue;
        }
        if (pending_space) {
            if (o.len && o.buf[o.len - 1] != '\n' && o.buf[o.len - 1] != ' ') ob_putc(&o, ' ');
            pending_space = 0;
        }
        if (c >= 0xC0 && p + 1 < end) {
            /* decode a full UTF-8 sequence and fold it to ASCII */
            int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
            uint32_t cp = 0;
            int valid = 1;
            for (int k = 0; k < n; k++) {
                if (p + k >= end || (k > 0 && ((unsigned char)p[k] & 0xC0) != 0x80)) { valid = 0; break; }
                cp = (cp << 6) | ((unsigned char)p[k] & (k == 0 ? (c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F) : 0x3F));
            }
            if (valid && cp >= 0x80) {
                ob_flush_space(&o, &pending_space);
                if (is_latin1_accent(cp)) ob_put_utf8(&o, cp);
                else ob_putc(&o, fold_cp(cp));
                p += n;
                continue;
            }
            ob_flush_space(&o, &pending_space);
            ob_putc(&o, '?');
            p++;
            continue;
        }
        if (c >= 0x80 && c < 0xC0) {   /* stray continuation byte */
            ob_flush_space(&o, &pending_space);
            ob_putc(&o, '?');
            p++;
            continue;
        }
        ob_putc(&o, (char)c);
        p++;
    }
    ob_putc(&o, 0);
    o.len = strlen(o.buf);   /* the fold may have shrunk the buffer */
    if (out_len) *out_len = o.len;
    return o.buf;
}
