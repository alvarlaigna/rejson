#include "json_object.h"

/* === Parser === */
/* A custom context for the JSON lexer. */
typedef struct {
    jsonsl_error_t err;  // lexer error
    size_t errpos;       // error position
    Node **nodes;        // stack of created nodes
    int nlen;            // size of node stack
} JsonObjectContext;

#define _pushNode(ctx, n) ctx->nodes[ctx->nlen++] = n
#define _popNode(ctx) ctx->nodes[--ctx->nlen]

/* Decalre it. */
static int _IsAllowedWhitespace(unsigned c);

void pushCallback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *state,
                  const jsonsl_char_t *at) {
    JsonObjectContext *joctx = (JsonObjectContext *)jsn->data;

    // only objects (dictionaries) and lists (arrays) create a container on push
    switch (state->type) {
        case JSONSL_T_OBJECT:
            _pushNode(joctx, NewDictNode(1));
            break;
        case JSONSL_T_LIST:
            _pushNode(joctx, NewArrayNode(1));
            break;
    }
}

void popCallback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *state,
                 const jsonsl_char_t *at) {
    JsonObjectContext *joctx = (JsonObjectContext *)jsn->data;

    // This is a good time to create literals and hashkeys on the stack
    switch (state->type) {
        case JSONSL_T_STRING:
            _pushNode(joctx, NewStringNode(jsn->base + state->pos_begin + 1,
                                           state->pos_cur - state->pos_begin - 1));
            break;
        case JSONSL_T_SPECIAL:
            if (state->special_flags & JSONSL_SPECIALf_NUMERIC) {
                if (state->special_flags & JSONSL_SPECIALf_FLOAT) {
                    _pushNode(joctx, NewDoubleNode(atof(jsn->base + state->pos_begin)));
                } else {
                    _pushNode(joctx, NewIntNode(atoi(jsn->base + state->pos_begin)));
                }
            } else if (state->special_flags & JSONSL_SPECIALf_BOOLEAN) {
                _pushNode(joctx, NewBoolNode(state->special_flags & JSONSL_SPECIALf_TRUE));
            } else if (state->special_flags & JSONSL_SPECIALf_NULL) {
                _pushNode(joctx, NULL);
            }
            break;
        case JSONSL_T_HKEY:
            _pushNode(joctx, NewKeyValNode(jsn->base + state->pos_begin + 1,
                                           state->pos_cur - state->pos_begin - 1, NULL));
            break;
    }

    // Basically anything that pops from the JSON lexer needs to be set in its parent, except the
    // root element
    if (joctx->nlen > 1 && state->type != JSONSL_T_HKEY) {
        NodeType p = joctx->nodes[joctx->nlen - 2]->type;
        switch (p) {
            case N_DICT:
                Node_DictSetKeyVal(joctx->nodes[joctx->nlen - 1], _popNode(joctx));
                break;
            case N_ARRAY:
                Node_ArrayAppend(joctx->nodes[joctx->nlen - 1], _popNode(joctx));
                break;
            case N_KEYVAL:
                joctx->nodes[joctx->nlen - 2]->value.kvval.val = _popNode(joctx);
                Node_DictSetKeyVal(joctx->nodes[joctx->nlen - 1], _popNode(joctx));
                break;
            default:
                break;
        }
    }
}

int errorCallback(jsonsl_t jsn, jsonsl_error_t err, struct jsonsl_state_st *state, char *errat) {
    JsonObjectContext *joctx = (JsonObjectContext *)jsn->data;

    joctx->err = err;
    joctx->errpos = state->pos_cur;
    jsonsl_stop(jsn);
    return 0;
}

int CreateNodeFromJSON(const char *buf, size_t len, Node **node, char **err) {
    int levels = JSONSL_MAX_LEVELS;  // TODO: heur levels from len since we're not really streaming?

    size_t _off = 0, _len = len;
    char *_buf = (char *)buf;
    int is_literal = 0;

    // munch any leading whitespaces
    while (_IsAllowedWhitespace(_buf[_off]) && _off < _len) _off++;

    /* Embed literals in a list (also avoids JSONSL_ERROR_STRING_OUTSIDE_CONTAINER).
     * Copying is necc. evil to avoid messing w/ non-standard string implementations (e.g. sds), but
     * forgivable because most literals are supposed to be short-ish.
    */
    if ((is_literal = ('{' != _buf[_off]) && ('[' != _buf[_off]) && _off < _len)) {
        _len = _len - _off + 2;
        _buf = malloc(_len * sizeof(char));
        _buf[0] = '[';
        _buf[_len - 1] = ']';
        memcpy(&_buf[1], &buf[_off], len - _off);
    }

    /* The lexer. */
    jsonsl_t jsn = jsonsl_new(levels);
    jsn->error_callback = errorCallback;
    jsn->action_callback_POP = popCallback;
    jsn->action_callback_PUSH = pushCallback;
    jsonsl_enable_all_callbacks(jsn);

    /* Set up our custom context. */
    JsonObjectContext *joctx = calloc(1, sizeof(JsonObjectContext));
    joctx->nodes = calloc(levels, sizeof(Node *));
    jsn->data = joctx;

    /* Feed the lexer. */
    jsonsl_feed(jsn, _buf, _len);

    /* Finalize. */
    int rc = JSONOBJECT_OK;
    // success alone isn't an assurance, verify there's something in there too
    if (JSONSL_ERROR_SUCCESS == joctx->err && jsn->stack[jsn->level].nelem) {
        // extract the literal and discard the wrapper array
        if (is_literal) {
            Node_ArrayItem(joctx->nodes[0], 0, node);
            Node_ArraySet(joctx->nodes[0], 0, NULL);
            Node_Free(_popNode(joctx));
            free(_buf);
        } else {
            *node = _popNode(joctx);
        }
    } else {
        // report the error if the optional arg is passed
        rc = JSONOBJECT_ERROR;
        if (err) {
            sds serr = sdsempty();
            // TODO: trim buf to relevant position and show it, careful about returning \n in err!
            if (JSONSL_ERROR_SUCCESS != joctx->err) {
                // if we have a lexer error lets return it
                serr = sdscatprintf(serr, "ERR JSON lexer %s error at position %zd",
                                    jsonsl_strerror(joctx->err), joctx->errpos + 1);
            } else if (!jsn->stack[jsn->level].nelem) {
                // parsing went ok so far but it didn't yield anything substantial at the end
                serr = sdscatprintf(serr, "ERR JSON lexer found no elements in level %d position %zd",
                                    jsn->level, jsn->pos);
            }
            *err = strdup(serr);
            sdsfree(serr);
        }
    }

    free(joctx->nodes);
    free(joctx);
    jsonsl_destroy(jsn);

    return rc;
}

/* === JSON serializer === */

typedef struct {
    sds buf;         // serialization buffer
    int depth;       // current tree depth
    int indent;      // indentation string length
    sds indentstr;   // indentaion string
    sds newlinestr;  // newline string
    sds spacestr;    // space string
    sds delimstr;    // delimiter string
} _JSONBuilderContext;

#define _JSONSerialize_Indent(b) \
    if (b->indent)               \
        for (int i = 0; i < b->depth; i++) b->buf = sdscatsds(b->buf, b->indentstr);

void _JSONSerialize_StringValue(Node *n, void *ctx) {
    _JSONBuilderContext *b = (_JSONBuilderContext *)ctx;
    size_t len = n->value.strval.len;
    const char *c = n->value.strval.data;

    sds s = sdsnewlen("\"", 1);
    while (len--) {
        if ((unsigned char)*c > 31 && *c != '\"' && *c != '\\' && *c != '/') {
            s = sdscatlen(s, c, 1);
        } else {
            s = sdscatlen(s, "\\", 1);  // escape it
            switch (*c) {
                case '\"':  // quotation mark
                    s = sdscatlen(s, "\"", 1);
                    break;
                case '\\':  // solidus
                    s = sdscatlen(s, "\\", 1);
                    break;
                case '/':  // reverse solidus
                    s = sdscatlen(s, "/", 1);
                    break;
                case '\b':  // backspace
                    s = sdscatlen(s, "b", 1);
                    break;
                case '\f':  // formfeed
                    s = sdscatlen(s, "f", 1);
                    break;
                case '\n':  // newline
                    s = sdscatlen(s, "n", 1);
                    break;
                case '\r':  // carriage return
                    s = sdscatlen(s, "r", 1);
                    break;
                case '\t':  // tab
                    s = sdscatlen(s, "t", 1);
                    break;
                default:  // anything between 0 and 31
                    s = sdscatprintf(s, "u%04x", (unsigned char)*c);
                    break;
            }  // switch (*chr)
        }
        c++;
    }
    s = sdscatlen(s, "\"", 1);
    b->buf = sdscatsds(b->buf, s);
    sdsfree(s);
}

void _JSONSerialize_BeginValue(Node *n, void *ctx) {
    _JSONBuilderContext *b = (_JSONBuilderContext *)ctx;

    if (!n) {  // NULL nodes are literal nulls
        b->buf = sdscatlen(b->buf, "null", 4);
    } else {
        switch (n->type) {
            case N_BOOLEAN:
                if (n->value.boolval) {
                    b->buf = sdscatlen(b->buf, "true", 4);
                } else {
                    b->buf = sdscatlen(b->buf, "false", 5);
                }
                break;
            case N_INTEGER:
                b->buf = sdscatfmt(b->buf, "%I", n->value.intval);
                break;
            case N_NUMBER:
                if (fabs(floor(n->value.numval) - n->value.numval) <= DBL_EPSILON &&
                    fabs(n->value.numval) < 1.0e60)
                    b->buf = sdscatprintf(b->buf, "%.0f", n->value.numval);
                else if (fabs(n->value.numval) < 1.0e-6 || fabs(n->value.numval) > 1.0e9)
                    b->buf = sdscatprintf(b->buf, "%e", n->value.numval);
                else
                    b->buf = sdscatprintf(b->buf, "%g", n->value.numval);
                break;
            case N_STRING:
                _JSONSerialize_StringValue(n, b);
                break;
            case N_KEYVAL:
                b->buf = sdscatfmt(b->buf, "\"%s\":%s", n->value.kvval.key, b->spacestr);
                break;
            case N_DICT:
                b->buf = sdscatlen(b->buf, "{", 1);
                b->depth++;
                if (n->value.dictval.len) {
                    b->buf = sdscatsds(b->buf, b->newlinestr);
                    _JSONSerialize_Indent(b);
                }
                break;
            case N_ARRAY:
                b->buf = sdscatlen(b->buf, "[", 1);
                b->depth++;
                if (n->value.arrval.len) {
                    b->buf = sdscatsds(b->buf, b->newlinestr);
                    _JSONSerialize_Indent(b);
                }
                break;
            case N_NULL:  // keeps the compiler from complaining
                break;
        }  // switch(n->type)
    }
}

void _JSONSerialize_EndValue(Node *n, void *ctx) {
    _JSONBuilderContext *b = (_JSONBuilderContext *)ctx;
    if (n) {
        switch (n->type) {
            case N_DICT:
                if (n->value.dictval.len) {
                    b->buf = sdscatsds(b->buf, b->newlinestr);
                }
                b->depth--;
                _JSONSerialize_Indent(b);
                b->buf = sdscatlen(b->buf, "}", 1);
                break;
            case N_ARRAY:
                if (n->value.arrval.len) {
                    b->buf = sdscatsds(b->buf, b->newlinestr);
                }
                b->depth--;
                _JSONSerialize_Indent(b);
                b->buf = sdscatlen(b->buf, "]", 1);
                break;
            default:  // keeps the compiler from complaining
                break;
        }
    }
}

void _JSONSerialize_ContainerDelimiter(void *ctx) {
    _JSONBuilderContext *b = (_JSONBuilderContext *)ctx;
    b->buf = sdscat(b->buf, b->delimstr);
    _JSONSerialize_Indent(b);
}

void SerializeNodeToJSON(const Node *node, const JSONSerializeOpt *opt, sds *json) {
    int levels = JSONSL_MAX_LEVELS;

    // set up the builder
    _JSONBuilderContext *b = calloc(1, sizeof(_JSONBuilderContext));
    b->indentstr = opt->indentstr ? sdsnew(opt->indentstr) : sdsempty();
    b->newlinestr = opt->newlinestr ? sdsnew(opt->newlinestr) : sdsempty();
    b->spacestr = opt->spacestr ? sdsnew(opt->spacestr) : sdsempty();
    b->indent = sdslen(b->indentstr);
    b->delimstr = sdsnewlen(",", 1);
    b->delimstr = sdscat(b->delimstr, b->newlinestr);

    NodeSerializerOpt nso = {.fBegin = _JSONSerialize_BeginValue,
                             .xBegin = 0xffff,
                             .fEnd = _JSONSerialize_EndValue,
                             .xEnd = (N_DICT | N_ARRAY),
                             .fDelim = _JSONSerialize_ContainerDelimiter,
                             .xDelim = (N_DICT | N_ARRAY)};

    // the real work
    // b->buf = *json; <- this causes memory crap???
    b->buf = sdsdup(*json);
    Node_Serializer(node, &nso, b);
    sdsfree(*json);
    *json = b->buf;

    sdsfree(b->indentstr);
    sdsfree(b->newlinestr);
    sdsfree(b->spacestr);
    sdsfree(b->delimstr);
    free(b);
}

// clang-format off
// from jsonsl.c
/**
 * This table contains entries for the allowed whitespace as per RFC 4627
 */
static int _AllowedWhitespace[0x100] = {
    /* 0x00 */ 0,0,0,0,0,0,0,0,0,                                               /* 0x08 */
    /* 0x09 */ 1 /* <TAB> */,                                                   /* 0x09 */
    /* 0x0a */ 1 /* <LF> */,                                                    /* 0x0a */
    /* 0x0b */ 0,0,                                                             /* 0x0c */
    /* 0x0d */ 1 /* <CR> */,                                                    /* 0x0d */
    /* 0x0e */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                             /* 0x1f */
    /* 0x20 */ 1 /* <SP> */,                                                    /* 0x20 */
    /* 0x21 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x40 */
    /* 0x41 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x60 */
    /* 0x61 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x80 */
    /* 0x81 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xa0 */
    /* 0xa1 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xc0 */
    /* 0xc1 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xe0 */
    /* 0xe1 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0      /* 0xfe */
};

static int _IsAllowedWhitespace(unsigned c) { return c == ' ' || _AllowedWhitespace[c & 0xff]; }
// clang-format on