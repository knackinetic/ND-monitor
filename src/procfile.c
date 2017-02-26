#include "common.h"
#include "procfile.h"

#define PF_PREFIX "PROCFILE"

#define PFWORDS_INCREASE_STEP 200
#define PFLINES_INCREASE_STEP 10
#define PROCFILE_INCREMENT_BUFFER 512

int procfile_adaptive_initial_allocation = 0;

// if adaptive allocation is set, these store the
// max values we have seen so far
size_t procfile_max_lines = PFLINES_INCREASE_STEP;
size_t procfile_max_words = PFWORDS_INCREASE_STEP;
size_t procfile_max_allocation = PROCFILE_INCREMENT_BUFFER;


// ----------------------------------------------------------------------------

char *procfile_filename(procfile *ff) {
    if(ff->filename[0]) return ff->filename;

    char buffer[FILENAME_MAX + 1];
    snprintfz(buffer, FILENAME_MAX, "/proc/self/fd/%d", ff->fd);

    ssize_t l = readlink(buffer, ff->filename, FILENAME_MAX);
    if(unlikely(l == -1))
        snprintfz(ff->filename, FILENAME_MAX, "unknown filename for fd %d", ff->fd);
    else
        ff->filename[l] = '\0';

    // on non-linux systems, something like this will be needed
    // fcntl(ff->fd, F_GETPATH, ff->filename)

    return ff->filename;
}

// ----------------------------------------------------------------------------
// An array of words

static inline pfwords *pfwords_add(pfwords *fw, char *str) NEVERNULL;
static inline pfwords *pfwords_add(pfwords *fw, char *str) {
    // debug(D_PROCFILE, PF_PREFIX ":   adding word No %d: '%s'", fw->len, str);

    if(unlikely(fw->len == fw->size)) {
        // debug(D_PROCFILE, PF_PREFIX ":   expanding words");

        fw = reallocz(fw, sizeof(pfwords) + (fw->size + PFWORDS_INCREASE_STEP) * sizeof(char *));
        fw->size += PFWORDS_INCREASE_STEP;
    }

    fw->words[fw->len++] = str;

    return fw;
}

static inline pfwords *pfwords_new(void) NEVERNULL;
static inline pfwords *pfwords_new(void) {
    // debug(D_PROCFILE, PF_PREFIX ":   initializing words");

    size_t size = (procfile_adaptive_initial_allocation) ? procfile_max_words : PFWORDS_INCREASE_STEP;

    pfwords *new = mallocz(sizeof(pfwords) + size * sizeof(char *));
    new->len = 0;
    new->size = size;
    return new;
}

static inline void pfwords_reset(pfwords *fw) {
    // debug(D_PROCFILE, PF_PREFIX ":   reseting words");
    fw->len = 0;
}

static inline void pfwords_free(pfwords *fw) {
    // debug(D_PROCFILE, PF_PREFIX ":   freeing words");

    freez(fw);
}


// ----------------------------------------------------------------------------
// An array of lines

static inline pflines *pflines_add(pflines *fl, size_t first_word) NEVERNULL;
static inline pflines *pflines_add(pflines *fl, size_t first_word) {
    // debug(D_PROCFILE, PF_PREFIX ":   adding line %d at word %d", fl->len, first_word);

    if(unlikely(fl->len == fl->size)) {
        // debug(D_PROCFILE, PF_PREFIX ":   expanding lines");

        fl = reallocz(fl, sizeof(pflines) + (fl->size + PFLINES_INCREASE_STEP) * sizeof(ffline));
        fl->size += PFLINES_INCREASE_STEP;
    }

    fl->lines[fl->len].words = 0;
    fl->lines[fl->len++].first = first_word;

    return fl;
}

static inline pflines *pflines_new(void) NEVERNULL;
static inline pflines *pflines_new(void) {
    // debug(D_PROCFILE, PF_PREFIX ":   initializing lines");

    size_t size = (unlikely(procfile_adaptive_initial_allocation)) ? procfile_max_words : PFLINES_INCREASE_STEP;

    pflines *new = mallocz(sizeof(pflines) + size * sizeof(ffline));
    new->len = 0;
    new->size = size;
    return new;
}

static inline void pflines_reset(pflines *fl) {
    // debug(D_PROCFILE, PF_PREFIX ":   reseting lines");

    fl->len = 0;
}

static inline void pflines_free(pflines *fl) {
    // debug(D_PROCFILE, PF_PREFIX ":   freeing lines");

    freez(fl);
}


// ----------------------------------------------------------------------------
// The procfile

void procfile_close(procfile *ff) {
    if(unlikely(!ff)) return;

    debug(D_PROCFILE, PF_PREFIX ": Closing file '%s'", procfile_filename(ff));

    if(likely(ff->lines)) pflines_free(ff->lines);
    if(likely(ff->words)) pfwords_free(ff->words);

    if(likely(ff->fd != -1)) close(ff->fd);
    freez(ff);
}

static inline void procfile_parser(procfile *ff) {
    // debug(D_PROCFILE, PF_PREFIX ": Parsing file '%s'", ff->filename);

    char  *s = ff->data                 // our current position
        , *e = &ff->data[ff->len]       // the terminating null
        , *t = ff->data;                // the first character of a quoted or a parenthesized string

                                        // the look up array to find our type of character
    PF_CHAR_TYPE *separators = ff->separators;

    char quote = 0;                     // the quote character - only when in quoted string

    size_t
          l = 0                         // counts the number of lines we added
        , w = 0                         // counts the number of words we added
        , opened = 0;                   // counts the number of open parenthesis

    ff->lines = pflines_add(ff->lines, w);

    while(likely(s < e)) {
        // we are not at the end
        PF_CHAR_TYPE ct = separators[(unsigned char)(*s)];

        // this is faster than a switch()
        if(likely(ct == PF_CHAR_IS_WORD)) {
            s++;
        }
        else if(likely(ct == PF_CHAR_IS_SEPARATOR)) {
            if(unlikely(quote || opened)) {
                // we are inside a quote
                s++;
                continue;
            }

            if(unlikely(s == t)) {
                // skip all leading white spaces
                t = ++s;
                continue;
            }

            // end of word
            *s = '\0';

            ff->words = pfwords_add(ff->words, t);
            ff->lines->lines[l].words++;
            w++;

            t = ++s;
        }
        else if(likely(ct == PF_CHAR_IS_NEWLINE)) {
            // end of line
            *s = '\0';

            ff->words = pfwords_add(ff->words, t);
            ff->lines->lines[l].words++;
            w++;

            // debug(D_PROCFILE, PF_PREFIX ":   ended line %d with %d words", l, ff->lines->lines[l].words);

            ff->lines = pflines_add(ff->lines, w);
            l++;

            t = ++s;
        }
        else if(likely(ct == PF_CHAR_IS_QUOTE)) {
            if(unlikely(!quote && s == t)) {
                // quote opened at the beginning
                quote = *s;
                t = ++s;
            }
            else if(unlikely(quote && quote == *s)) {
                // quote closed
                quote = 0;

                *s = '\0';
                ff->words = pfwords_add(ff->words, t);
                ff->lines->lines[l].words++;
                w++;

                t = ++s;
            }
            else
                s++;
        }
        else if(likely(ct == PF_CHAR_IS_OPEN)) {
            if(s == t) {
                opened++;
                t = ++s;
            }
            else if(opened) {
                opened++;
                s++;
            }
            else
                s++;
        }
        else if(likely(ct == PF_CHAR_IS_CLOSE)) {
            if(opened) {
                opened--;

                if(!opened) {
                    *s = '\0';
                    ff->words = pfwords_add(ff->words, t);
                    ff->lines->lines[l].words++;
                    w++;

                    t = ++s;
                }
                else
                    s++;
            }
            else
                s++;
        }
        else
            fatal("Internal Error: procfile_readall() does not handle all the cases.");
    }

    if(likely(s > t && t < e)) {
        // the last word
        if(likely(ff->len < ff->size))
            *s = '\0';
        else {
            // we are going to loose the last byte
            ff->data[ff->size - 1] = '\0';
        }

        ff->words = pfwords_add(ff->words, t);
        ff->lines->lines[l].words++;
    }
}

procfile *procfile_readall(procfile *ff) {
    // debug(D_PROCFILE, PF_PREFIX ": Reading file '%s'.", ff->filename);

    ff->len = 0;    // zero the used size
    ssize_t r = 1;  // read at least once
    while(r > 0) {
        ssize_t s = ff->len;
        ssize_t x = ff->size - s;

        if(unlikely(!x)) {
            debug(D_PROCFILE, PF_PREFIX ": Expanding data buffer for file '%s'.", procfile_filename(ff));
            ff = reallocz(ff, sizeof(procfile) + ff->size + PROCFILE_INCREMENT_BUFFER);
            ff->size += PROCFILE_INCREMENT_BUFFER;
        }

        debug(D_PROCFILE, "Reading file '%s', from position %zd with length %zd", procfile_filename(ff), s, (ssize_t)(ff->size - s));
        r = read(ff->fd, &ff->data[s], ff->size - s);
        if(unlikely(r == -1)) {
            if(unlikely(!(ff->flags & PROCFILE_FLAG_NO_ERROR_ON_FILE_IO))) error(PF_PREFIX ": Cannot read from file '%s'", procfile_filename(ff));
            procfile_close(ff);
            return NULL;
        }

        ff->len += r;
    }

    // debug(D_PROCFILE, "Rewinding file '%s'", ff->filename);
    if(unlikely(lseek(ff->fd, 0, SEEK_SET) == -1)) {
        if(unlikely(!(ff->flags & PROCFILE_FLAG_NO_ERROR_ON_FILE_IO))) error(PF_PREFIX ": Cannot rewind on file '%s'.", procfile_filename(ff));
        procfile_close(ff);
        return NULL;
    }

    pflines_reset(ff->lines);
    pfwords_reset(ff->words);
    procfile_parser(ff);

    if(unlikely(procfile_adaptive_initial_allocation)) {
        if(unlikely(ff->len > procfile_max_allocation)) procfile_max_allocation = ff->len;
        if(unlikely(ff->lines->len > procfile_max_lines)) procfile_max_lines = ff->lines->len;
        if(unlikely(ff->words->len > procfile_max_words)) procfile_max_words = ff->words->len;
    }

    // debug(D_PROCFILE, "File '%s' updated.", ff->filename);
    return ff;
}

static inline void procfile_set_separators(procfile *ff, const char *separators) {
    static PF_CHAR_TYPE def[256];
    static char initilized = 0;

    if(unlikely(!initilized)) {
        // this is thread safe
        // if initialized is zero, multiple threads may be executing
        // this code at the same time, setting in def[] the exact same values
        int i = 256;
        while(i--) {
            if(unlikely(i == '\n' || i == '\r'))
                def[i] = PF_CHAR_IS_NEWLINE;

            else if(unlikely(isspace(i) || !isprint(i)))
                def[i] = PF_CHAR_IS_SEPARATOR;

            else
                def[i] = PF_CHAR_IS_WORD;
        }

        initilized = 1;
    }

    // copy the default
    PF_CHAR_TYPE *ffs = ff->separators, *ffd = def, *ffe = &def[256];
    while(ffd != ffe)
        *ffs++ = *ffd++;

    // set the separators
    if(unlikely(!separators))
        separators = " \t=|";

    ffs = ff->separators;
    const char *s = separators;
    while(*s)
        ffs[(int)*s++] = PF_CHAR_IS_SEPARATOR;
}

void procfile_set_quotes(procfile *ff, const char *quotes) {
    PF_CHAR_TYPE *ffs = ff->separators;

    // remove all quotes
    int i = 256;
    while(i--)
        if(unlikely(ffs[i] == PF_CHAR_IS_QUOTE))
            ffs[i] = PF_CHAR_IS_WORD;

    // if nothing given, return
    if(unlikely(!quotes || !*quotes))
        return;

    // set the quotes
    const char *s = quotes;
    while(*s)
        ffs[(int)*s++] = PF_CHAR_IS_QUOTE;
}

void procfile_set_open_close(procfile *ff, const char *open, const char *close) {
    PF_CHAR_TYPE *ffs = ff->separators;

    // remove all open/close
    int i = 256;
    while(i--)
        if(unlikely(ffs[i] == PF_CHAR_IS_OPEN || ffs[i] == PF_CHAR_IS_CLOSE))
            ffs[i] = PF_CHAR_IS_WORD;

    // if nothing given, return
    if(unlikely(!open || !*open || !close || !*close))
        return;

    // set the openings
    const char *s = open;
    while(*s)
        ffs[(int)*s++] = PF_CHAR_IS_OPEN;

    // set the closings
    s = close;
    while(*s)
        ffs[(int)*s++] = PF_CHAR_IS_CLOSE;
}

procfile *procfile_open(const char *filename, const char *separators, uint32_t flags) {
    debug(D_PROCFILE, PF_PREFIX ": Opening file '%s'", filename);

    int fd = open(filename, O_RDONLY, 0666);
    if(unlikely(fd == -1)) {
        if(unlikely(!(flags & PROCFILE_FLAG_NO_ERROR_ON_FILE_IO))) error(PF_PREFIX ": Cannot open file '%s'", filename);
        return NULL;
    }

    size_t size = (unlikely(procfile_adaptive_initial_allocation)) ? procfile_max_allocation : PROCFILE_INCREMENT_BUFFER;
    procfile *ff = mallocz(sizeof(procfile) + size);

    //strncpyz(ff->filename, filename, FILENAME_MAX);
    ff->filename[0] = '\0';

    ff->fd = fd;
    ff->size = size;
    ff->len = 0;
    ff->flags = flags;

    ff->lines = pflines_new();
    ff->words = pfwords_new();

    procfile_set_separators(ff, separators);

    debug(D_PROCFILE, "File '%s' opened.", filename);
    return ff;
}

procfile *procfile_reopen(procfile *ff, const char *filename, const char *separators, uint32_t flags) {
    if(unlikely(!ff)) return procfile_open(filename, separators, flags);

    if(likely(ff->fd != -1)) close(ff->fd);

    ff->fd = open(filename, O_RDONLY, 0666);
    if(unlikely(ff->fd == -1)) {
        procfile_close(ff);
        return NULL;
    }

    //strncpyz(ff->filename, filename, FILENAME_MAX);
    ff->filename[0] = '\0';

    ff->flags = flags;

    // do not do the separators again if NULL is given
    if(likely(separators)) procfile_set_separators(ff, separators);

    return ff;
}

// ----------------------------------------------------------------------------
// example parsing of procfile data

void procfile_print(procfile *ff) {
    size_t lines = procfile_lines(ff), l;
    char *s;

    debug(D_PROCFILE, "File '%s' with %zu lines and %zu words", procfile_filename(ff), ff->lines->len, ff->words->len);

    for(l = 0; likely(l < lines) ;l++) {
        size_t words = procfile_linewords(ff, l);

        debug(D_PROCFILE, " line %zu starts at word %zu and has %zu words", l, ff->lines->lines[l].first, ff->lines->lines[l].words);

        size_t w;
        for(w = 0; likely(w < words) ;w++) {
            s = procfile_lineword(ff, l, w);
            debug(D_PROCFILE, "     [%zu.%zu] '%s'", l, w, s);
        }
    }
}
