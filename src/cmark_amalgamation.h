#ifndef CMARK_H
#define CMARK_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif
#ifndef CMARK_CONFIG_H
#define CMARK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAVE_STDBOOL_H

#ifdef HAVE_STDBOOL_H
  #include <stdbool.h>
#elif !defined(__cplusplus)
  typedef char bool;
#endif

#define HAVE___BUILTIN_EXPECT

#define HAVE___ATTRIBUTE__

#ifdef HAVE___ATTRIBUTE__
  #define CMARK_ATTRIBUTE(list) __attribute__ (list)
#else
  #define CMARK_ATTRIBUTE(list)
#endif

#ifndef CMARK_INLINE
  #if defined(_MSC_VER) && !defined(__cplusplus)
    #define CMARK_INLINE __inline
  #else
    #define CMARK_INLINE inline
  #endif
#endif

/* snprintf and vsnprintf fallbacks for MSVC before 2015,
   due to Valentin Milea http://stackoverflow.com/questions/2915672/
*/

#if defined(_MSC_VER) && _MSC_VER < 1900

#include <stdio.h>
#include <stdarg.h>

#define snprintf c99_snprintf
#define vsnprintf c99_vsnprintf

CMARK_INLINE int c99_vsnprintf(char *outBuf, size_t size, const char *format, va_list ap)
{
    int count = -1;

    if (size != 0)
        count = _vsnprintf_s(outBuf, size, _TRUNCATE, format, ap);
    if (count == -1)
        count = _vscprintf(format, ap);

    return count;
}

CMARK_INLINE int c99_snprintf(char *outBuf, size_t size, const char *format, ...)
{
    int count;
    va_list ap;

    va_start(ap, format);
    count = c99_vsnprintf(outBuf, size, format, ap);
    va_end(ap);

    return count;
}

#endif

#ifdef __cplusplus
}
#endif

#endif
/** # NAME
 *
 * **cmark** - CommonMark parsing, manipulating, and rendering
 */
#ifndef CMARK_EXPORT_H
#define CMARK_EXPORT_H

#ifdef CMARK_STATIC_DEFINE
#  define CMARK_EXPORT
#  define CMARK_NO_EXPORT
#else
#  ifndef CMARK_EXPORT
#    ifdef libcmark_EXPORTS
        /* We are building this library */
#      define CMARK_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define CMARK_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef CMARK_NO_EXPORT
#    define CMARK_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef CMARK_DEPRECATED
#  define CMARK_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef CMARK_DEPRECATED_EXPORT
#  define CMARK_DEPRECATED_EXPORT CMARK_EXPORT CMARK_DEPRECATED
#endif

#ifndef CMARK_DEPRECATED_NO_EXPORT
#  define CMARK_DEPRECATED_NO_EXPORT CMARK_NO_EXPORT CMARK_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef CMARK_NO_DEPRECATED
#    define CMARK_NO_DEPRECATED
#  endif
#endif

#endif
/** # DESCRIPTION
 *
 * ## Simple Interface
 */

/** Convert 'text' (assumed to be a UTF-8 encoded string with length
 * 'len') from CommonMark Markdown to HTML, returning a null-terminated,
 * UTF-8-encoded string. It is the caller's responsibility
 * to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_markdown_to_html(const char *text, size_t len, int options);

/** ## Node Structure
 */

typedef enum {
  /* Error status */
  CMARK_NODE_NONE,

  /* Block */
  CMARK_NODE_DOCUMENT,
  CMARK_NODE_BLOCK_QUOTE,
  CMARK_NODE_LIST,
  CMARK_NODE_ITEM,
  CMARK_NODE_CODE_BLOCK,
  CMARK_NODE_HTML_BLOCK,
  CMARK_NODE_CUSTOM_BLOCK,
  CMARK_NODE_PARAGRAPH,
  CMARK_NODE_HEADING,
  CMARK_NODE_THEMATIC_BREAK,

  CMARK_NODE_FIRST_BLOCK = CMARK_NODE_DOCUMENT,
  CMARK_NODE_LAST_BLOCK = CMARK_NODE_THEMATIC_BREAK,

  /* Inline */
  CMARK_NODE_TEXT,
  CMARK_NODE_SOFTBREAK,
  CMARK_NODE_LINEBREAK,
  CMARK_NODE_CODE,
  CMARK_NODE_HTML_INLINE,
  CMARK_NODE_CUSTOM_INLINE,
  CMARK_NODE_EMPH,
  CMARK_NODE_STRONG,
  CMARK_NODE_LINK,
  CMARK_NODE_IMAGE,

  CMARK_NODE_FIRST_INLINE = CMARK_NODE_TEXT,
  CMARK_NODE_LAST_INLINE = CMARK_NODE_IMAGE,
} cmark_node_type;

/* For backwards compatibility: */
#define CMARK_NODE_HEADER CMARK_NODE_HEADING
#define CMARK_NODE_HRULE CMARK_NODE_THEMATIC_BREAK
#define CMARK_NODE_HTML CMARK_NODE_HTML_BLOCK
#define CMARK_NODE_INLINE_HTML CMARK_NODE_HTML_INLINE

typedef enum {
  CMARK_NO_LIST,
  CMARK_BULLET_LIST,
  CMARK_ORDERED_LIST
} cmark_list_type;

typedef enum {
  CMARK_NO_DELIM,
  CMARK_PERIOD_DELIM,
  CMARK_PAREN_DELIM
} cmark_delim_type;

typedef struct cmark_node cmark_node;
typedef struct cmark_parser cmark_parser;
typedef struct cmark_iter cmark_iter;

/**
 * ## Custom memory allocator support
 */

/** Defines the memory allocation functions to be used by CMark
 * when parsing and allocating a document tree
 */
typedef struct cmark_mem {
  void *(*calloc)(size_t, size_t);
  void *(*realloc)(void *, size_t);
  void (*free)(void *);
} cmark_mem;

/**
 * ## Creating and Destroying Nodes
 */

/** Creates a new node of type 'type'.  Note that the node may have
 * other required properties, which it is the caller's responsibility
 * to assign.
 */
CMARK_EXPORT cmark_node *cmark_node_new(cmark_node_type type);

/** Same as `cmark_node_new`, but explicitly listing the memory
 * allocator used to allocate the node.  Note:  be sure to use the same
 * allocator for every node in a tree, or bad things can happen.
 */
CMARK_EXPORT cmark_node *cmark_node_new_with_mem(cmark_node_type type,
                                                 cmark_mem *mem);

/** Frees the memory allocated for a node and any children.
 */
CMARK_EXPORT void cmark_node_free(cmark_node *node);

/**
 * ## Tree Traversal
 */

/** Returns the next node in the sequence after 'node', or NULL if
 * there is none.
 */
CMARK_EXPORT cmark_node *cmark_node_next(cmark_node *node);

/** Returns the previous node in the sequence after 'node', or NULL if
 * there is none.
 */
CMARK_EXPORT cmark_node *cmark_node_previous(cmark_node *node);

/** Returns the parent of 'node', or NULL if there is none.
 */
CMARK_EXPORT cmark_node *cmark_node_parent(cmark_node *node);

/** Returns the first child of 'node', or NULL if 'node' has no children.
 */
CMARK_EXPORT cmark_node *cmark_node_first_child(cmark_node *node);

/** Returns the last child of 'node', or NULL if 'node' has no children.
 */
CMARK_EXPORT cmark_node *cmark_node_last_child(cmark_node *node);

/**
 * ## Iterator
 *
 * An iterator will walk through a tree of nodes, starting from a root
 * node, returning one node at a time, together with information about
 * whether the node is being entered or exited.  The iterator will
 * first descend to a child node, if there is one.  When there is no
 * child, the iterator will go to the next sibling.  When there is no
 * next sibling, the iterator will return to the parent (but with
 * a 'cmark_event_type' of `CMARK_EVENT_EXIT`).  The iterator will
 * return `CMARK_EVENT_DONE` when it reaches the root node again.
 * One natural application is an HTML renderer, where an `ENTER` event
 * outputs an open tag and an `EXIT` event outputs a close tag.
 * An iterator might also be used to transform an AST in some systematic
 * way, for example, turning all level-3 headings into regular paragraphs.
 *
 *     void
 *     usage_example(cmark_node *root) {
 *         cmark_event_type ev_type;
 *         cmark_iter *iter = cmark_iter_new(root);
 *
 *         while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
 *             cmark_node *cur = cmark_iter_get_node(iter);
 *             // Do something with `cur` and `ev_type`
 *         }
 *
 *         cmark_iter_free(iter);
 *     }
 *
 * Iterators will never return `EXIT` events for leaf nodes, which are nodes
 * of type:
 *
 * * CMARK_NODE_HTML_BLOCK
 * * CMARK_NODE_THEMATIC_BREAK
 * * CMARK_NODE_CODE_BLOCK
 * * CMARK_NODE_TEXT
 * * CMARK_NODE_SOFTBREAK
 * * CMARK_NODE_LINEBREAK
 * * CMARK_NODE_CODE
 * * CMARK_NODE_HTML_INLINE
 *
 * Nodes must only be modified after an `EXIT` event, or an `ENTER` event for
 * leaf nodes.
 */

typedef enum {
  CMARK_EVENT_NONE,
  CMARK_EVENT_DONE,
  CMARK_EVENT_ENTER,
  CMARK_EVENT_EXIT
} cmark_event_type;

/** Creates a new iterator starting at 'root'.  The current node and event
 * type are undefined until 'cmark_iter_next' is called for the first time.
 * The memory allocated for the iterator should be released using
 * 'cmark_iter_free' when it is no longer needed.
 */
CMARK_EXPORT
cmark_iter *cmark_iter_new(cmark_node *root);

/** Frees the memory allocated for an iterator.
 */
CMARK_EXPORT
void cmark_iter_free(cmark_iter *iter);

/** Advances to the next node and returns the event type (`CMARK_EVENT_ENTER`,
 * `CMARK_EVENT_EXIT` or `CMARK_EVENT_DONE`).
 */
CMARK_EXPORT
cmark_event_type cmark_iter_next(cmark_iter *iter);

/** Returns the current node.
 */
CMARK_EXPORT
cmark_node *cmark_iter_get_node(cmark_iter *iter);

/** Returns the current event type.
 */
CMARK_EXPORT
cmark_event_type cmark_iter_get_event_type(cmark_iter *iter);

/** Returns the root node.
 */
CMARK_EXPORT
cmark_node *cmark_iter_get_root(cmark_iter *iter);

/** Resets the iterator so that the current node is 'current' and
 * the event type is 'event_type'.  The new current node must be a
 * descendant of the root node or the root node itself.
 */
CMARK_EXPORT
void cmark_iter_reset(cmark_iter *iter, cmark_node *current,
                      cmark_event_type event_type);

/**
 * ## Accessors
 */

/** Returns the user data of 'node'.
 */
CMARK_EXPORT void *cmark_node_get_user_data(cmark_node *node);

/** Sets arbitrary user data for 'node'.  Returns 1 on success,
 * 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_user_data(cmark_node *node, void *user_data);

/** Returns the type of 'node', or `CMARK_NODE_NONE` on error.
 */
CMARK_EXPORT cmark_node_type cmark_node_get_type(cmark_node *node);

/** Like 'cmark_node_get_type', but returns a string representation
    of the type, or `"<unknown>"`.
 */
CMARK_EXPORT
const char *cmark_node_get_type_string(cmark_node *node);

/** Returns the string contents of 'node', or an empty
    string if none is set.
 */
CMARK_EXPORT const char *cmark_node_get_literal(cmark_node *node);

/** Sets the string contents of 'node'.  Returns 1 on success,
 * 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_literal(cmark_node *node, const char *content);

/** Returns the heading level of 'node', or 0 if 'node' is not a heading.
 */
CMARK_EXPORT int cmark_node_get_heading_level(cmark_node *node);

/* For backwards compatibility */
#define cmark_node_get_header_level cmark_node_get_heading_level
#define cmark_node_set_header_level cmark_node_set_heading_level

/** Sets the heading level of 'node', returning 1 on success and 0 on error.
 */
CMARK_EXPORT int cmark_node_set_heading_level(cmark_node *node, int level);

/** Returns the list type of 'node', or `CMARK_NO_LIST` if 'node'
 * is not a list.
 */
CMARK_EXPORT cmark_list_type cmark_node_get_list_type(cmark_node *node);

/** Sets the list type of 'node', returning 1 on success and 0 on error.
 */
CMARK_EXPORT int cmark_node_set_list_type(cmark_node *node,
                                          cmark_list_type type);

/** Returns the list delimiter type of 'node', or `CMARK_NO_DELIM` if 'node'
 * is not a list.
 */
CMARK_EXPORT cmark_delim_type cmark_node_get_list_delim(cmark_node *node);

/** Sets the list delimiter type of 'node', returning 1 on success and 0
 * on error.
 */
CMARK_EXPORT int cmark_node_set_list_delim(cmark_node *node,
                                           cmark_delim_type delim);

/** Returns starting number of 'node', if it is an ordered list, otherwise 0.
 */
CMARK_EXPORT int cmark_node_get_list_start(cmark_node *node);

/** Sets starting number of 'node', if it is an ordered list. Returns 1
 * on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_list_start(cmark_node *node, int start);

/** Returns 1 if 'node' is a tight list, 0 otherwise.
 */
CMARK_EXPORT int cmark_node_get_list_tight(cmark_node *node);

/** Sets the "tightness" of a list.  Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_list_tight(cmark_node *node, int tight);

/** Returns the info string from a fenced code block.
 */
CMARK_EXPORT const char *cmark_node_get_fence_info(cmark_node *node);

/** Sets the info string in a fenced code block, returning 1 on
 * success and 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_fence_info(cmark_node *node, const char *info);

/** Returns the URL of a link or image 'node', or an empty string
    if no URL is set.
 */
CMARK_EXPORT const char *cmark_node_get_url(cmark_node *node);

/** Sets the URL of a link or image 'node'. Returns 1 on success,
 * 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_url(cmark_node *node, const char *url);

/** Returns the title of a link or image 'node', or an empty
    string if no title is set.
 */
CMARK_EXPORT const char *cmark_node_get_title(cmark_node *node);

/** Sets the title of a link or image 'node'. Returns 1 on success,
 * 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_title(cmark_node *node, const char *title);

/** Returns the literal "on enter" text for a custom 'node', or
    an empty string if no on_enter is set.
 */
CMARK_EXPORT const char *cmark_node_get_on_enter(cmark_node *node);

/** Sets the literal text to render "on enter" for a custom 'node'.
    Any children of the node will be rendered after this text.
    Returns 1 on success 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_on_enter(cmark_node *node,
                                         const char *on_enter);

/** Returns the literal "on exit" text for a custom 'node', or
    an empty string if no on_exit is set.
 */
CMARK_EXPORT const char *cmark_node_get_on_exit(cmark_node *node);

/** Sets the literal text to render "on exit" for a custom 'node'.
    Any children of the node will be rendered before this text.
    Returns 1 on success 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_on_exit(cmark_node *node, const char *on_exit);

/** Returns the line on which 'node' begins.
 */
CMARK_EXPORT int cmark_node_get_start_line(cmark_node *node);

/** Returns the column at which 'node' begins.
 */
CMARK_EXPORT int cmark_node_get_start_column(cmark_node *node);

/** Returns the line on which 'node' ends.
 */
CMARK_EXPORT int cmark_node_get_end_line(cmark_node *node);

/** Returns the column at which 'node' ends.
 */
CMARK_EXPORT int cmark_node_get_end_column(cmark_node *node);

/**
 * ## Tree Manipulation
 */

/** Unlinks a 'node', removing it from the tree, but not freeing its
 * memory.  (Use 'cmark_node_free' for that.)
 */
CMARK_EXPORT void cmark_node_unlink(cmark_node *node);

/** Inserts 'sibling' before 'node'.  Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_insert_before(cmark_node *node,
                                          cmark_node *sibling);

/** Inserts 'sibling' after 'node'. Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_insert_after(cmark_node *node, cmark_node *sibling);

/** Replaces 'oldnode' with 'newnode' and unlinks 'oldnode' (but does
 * not free its memory).
 * Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_replace(cmark_node *oldnode, cmark_node *newnode);

/** Adds 'child' to the beginning of the children of 'node'.
 * Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_prepend_child(cmark_node *node, cmark_node *child);

/** Adds 'child' to the end of the children of 'node'.
 * Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_append_child(cmark_node *node, cmark_node *child);

/** Consolidates adjacent text nodes.
 */
CMARK_EXPORT void cmark_consolidate_text_nodes(cmark_node *root);

/**
 * ## Parsing
 *
 * Simple interface:
 *
 *     cmark_node *document = cmark_parse_document("Hello *world*", 13,
 *                                                 CMARK_OPT_DEFAULT);
 *
 * Streaming interface:
 *
 *     cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
 *     FILE *fp = fopen("myfile.md", "rb");
 *     while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
 *     	   cmark_parser_feed(parser, buffer, bytes);
 *     	   if (bytes < sizeof(buffer)) {
 *     	       break;
 *     	   }
 *     }
 *     document = cmark_parser_finish(parser);
 *     cmark_parser_free(parser);
 */

/** Creates a new parser object.
 */
CMARK_EXPORT
cmark_parser *cmark_parser_new(int options);

/** Creates a new parser object with the given memory allocator
 */
CMARK_EXPORT
cmark_parser *cmark_parser_new_with_mem(int options, cmark_mem *mem);

/** Frees memory allocated for a parser object.
 */
CMARK_EXPORT
void cmark_parser_free(cmark_parser *parser);

/** Feeds a string of length 'len' to 'parser'.
 */
CMARK_EXPORT
void cmark_parser_feed(cmark_parser *parser, const char *buffer, size_t len);

/** Finish parsing and return a pointer to a tree of nodes.
 */
CMARK_EXPORT
cmark_node *cmark_parser_finish(cmark_parser *parser);

/** Parse a CommonMark document in 'buffer' of length 'len'.
 * Returns a pointer to a tree of nodes.  The memory allocated for
 * the node tree should be released using 'cmark_node_free'
 * when it is no longer needed.
 */
CMARK_EXPORT
cmark_node *cmark_parse_document(const char *buffer, size_t len, int options);

/** Parse a CommonMark document in file 'f', returning a pointer to
 * a tree of nodes.  The memory allocated for the node tree should be
 * released using 'cmark_node_free' when it is no longer needed.
 */
CMARK_EXPORT
cmark_node *cmark_parse_file(FILE *f, int options);

/**
 * ## Rendering
 */

/** Render a 'node' tree as XML.  It is the caller's responsibility
 * to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_xml(cmark_node *root, int options);

/** Render a 'node' tree as an HTML fragment.  It is up to the user
 * to add an appropriate header and footer. It is the caller's
 * responsibility to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_html(cmark_node *root, int options);

/** Render a 'node' tree as a groff man page, without the header.
 * It is the caller's responsibility to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_man(cmark_node *root, int options, int width);

/** Render a 'node' tree as a commonmark document.
 * It is the caller's responsibility to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_commonmark(cmark_node *root, int options, int width);

/** Render a 'node' tree as a LaTeX document.
 * It is the caller's responsibility to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_latex(cmark_node *root, int options, int width);

/**
 * ## Options
 */

/** Default options.
 */
#define CMARK_OPT_DEFAULT 0

/**
 * ### Options affecting rendering
 */

/** Include a `data-sourcepos` attribute on all block elements.
 */
#define CMARK_OPT_SOURCEPOS (1 << 1)

/** Render `softbreak` elements as hard line breaks.
 */
#define CMARK_OPT_HARDBREAKS (1 << 2)

/** Suppress raw HTML and unsafe links (`javascript:`, `vbscript:`,
 * `file:`, and `data:`, except for `image/png`, `image/gif`,
 * `image/jpeg`, or `image/webp` mime types).  Raw HTML is replaced
 * by a placeholder HTML comment. Unsafe links are replaced by
 * empty strings.
 */
#define CMARK_OPT_SAFE (1 << 3)

/** Render `softbreak` elements as spaces.
 */
#define CMARK_OPT_NOBREAKS (1 << 4)

/**
 * ### Options affecting parsing
 */

/** Normalize tree by consolidating adjacent text nodes.
 */
#define CMARK_OPT_NORMALIZE (1 << 8)

/** Validate UTF-8 in the input before parsing, replacing illegal
 * sequences with the replacement character U+FFFD.
 */
#define CMARK_OPT_VALIDATE_UTF8 (1 << 9)

/** Convert straight quotes to curly, --- to em dashes, -- to en dashes.
 */
#define CMARK_OPT_SMART (1 << 10)

/**
 * ## Version information
 */

/** The library version as integer for runtime checks. Also available as
 * macro CMARK_VERSION for compile time checks.
 *
 * * Bits 16-23 contain the major version.
 * * Bits 8-15 contain the minor version.
 * * Bits 0-7 contain the patchlevel.
 *
 * In hexadecimal format, the number 0x010203 represents version 1.2.3.
 */
CMARK_EXPORT
int cmark_version(void);

/** The library version string for runtime checks. Also available as
 * macro CMARK_VERSION_STRING for compile time checks.
 */
CMARK_EXPORT
const char *cmark_version_string(void);

/** # AUTHORS
 *
 * John MacFarlane, Vicent Marti,  Kārlis Gaņģis, Nick Wellnhofer.
 */

#ifndef CMARK_NO_SHORT_NAMES
#define NODE_DOCUMENT CMARK_NODE_DOCUMENT
#define NODE_BLOCK_QUOTE CMARK_NODE_BLOCK_QUOTE
#define NODE_LIST CMARK_NODE_LIST
#define NODE_ITEM CMARK_NODE_ITEM
#define NODE_CODE_BLOCK CMARK_NODE_CODE_BLOCK
#define NODE_HTML_BLOCK CMARK_NODE_HTML_BLOCK
#define NODE_CUSTOM_BLOCK CMARK_NODE_CUSTOM_BLOCK
#define NODE_PARAGRAPH CMARK_NODE_PARAGRAPH
#define NODE_HEADING CMARK_NODE_HEADING
#define NODE_HEADER CMARK_NODE_HEADER
#define NODE_THEMATIC_BREAK CMARK_NODE_THEMATIC_BREAK
#define NODE_HRULE CMARK_NODE_HRULE
#define NODE_TEXT CMARK_NODE_TEXT
#define NODE_SOFTBREAK CMARK_NODE_SOFTBREAK
#define NODE_LINEBREAK CMARK_NODE_LINEBREAK
#define NODE_CODE CMARK_NODE_CODE
#define NODE_HTML_INLINE CMARK_NODE_HTML_INLINE
#define NODE_CUSTOM_INLINE CMARK_NODE_CUSTOM_INLINE
#define NODE_EMPH CMARK_NODE_EMPH
#define NODE_STRONG CMARK_NODE_STRONG
#define NODE_LINK CMARK_NODE_LINK
#define NODE_IMAGE CMARK_NODE_IMAGE
#define BULLET_LIST CMARK_BULLET_LIST
#define ORDERED_LIST CMARK_ORDERED_LIST
#define PERIOD_DELIM CMARK_PERIOD_DELIM
#define PAREN_DELIM CMARK_PAREN_DELIM
#endif

#ifdef __cplusplus
}
#endif

#endif

#ifndef CMARK_BUFFER_H
#define CMARK_BUFFER_H

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t bufsize_t;

typedef struct {
  cmark_mem *mem;
  unsigned char *ptr;
  bufsize_t asize, size;
} cmark_strbuf;

extern unsigned char cmark_strbuf__initbuf[];

#define CMARK_BUF_INIT(mem)                                                    \
  { mem, cmark_strbuf__initbuf, 0, 0 }

/**
 * Initialize a cmark_strbuf structure.
 *
 * For the cases where CMARK_BUF_INIT cannot be used to do static
 * initialization.
 */
void cmark_strbuf_init(cmark_mem *mem, cmark_strbuf *buf,
                       bufsize_t initial_size);

/**
 * Grow the buffer to hold at least `target_size` bytes.
 */
void cmark_strbuf_grow(cmark_strbuf *buf, bufsize_t target_size);

void cmark_strbuf_free(cmark_strbuf *buf);
void cmark_strbuf_swap(cmark_strbuf *buf_a, cmark_strbuf *buf_b);

bufsize_t cmark_strbuf_len(const cmark_strbuf *buf);

int cmark_strbuf_cmp(const cmark_strbuf *a, const cmark_strbuf *b);

unsigned char *cmark_strbuf_detach(cmark_strbuf *buf);
void cmark_strbuf_copy_cstr(char *data, bufsize_t datasize,
                            const cmark_strbuf *buf);

static CMARK_INLINE const char *cmark_strbuf_cstr(const cmark_strbuf *buf) {
  return (char *)buf->ptr;
}

#define cmark_strbuf_at(buf, n) ((buf)->ptr[n])

void cmark_strbuf_set(cmark_strbuf *buf, const unsigned char *data,
                      bufsize_t len);
void cmark_strbuf_sets(cmark_strbuf *buf, const char *string);
void cmark_strbuf_putc(cmark_strbuf *buf, int c);
void cmark_strbuf_put(cmark_strbuf *buf, const unsigned char *data,
                      bufsize_t len);
void cmark_strbuf_puts(cmark_strbuf *buf, const char *string);
void cmark_strbuf_clear(cmark_strbuf *buf);

bufsize_t cmark_strbuf_strchr(const cmark_strbuf *buf, int c, bufsize_t pos);
bufsize_t cmark_strbuf_strrchr(const cmark_strbuf *buf, int c, bufsize_t pos);
void cmark_strbuf_drop(cmark_strbuf *buf, bufsize_t n);
void cmark_strbuf_truncate(cmark_strbuf *buf, bufsize_t len);
void cmark_strbuf_rtrim(cmark_strbuf *buf);
void cmark_strbuf_trim(cmark_strbuf *buf);
void cmark_strbuf_normalize_whitespace(cmark_strbuf *s);
void cmark_strbuf_unescape(cmark_strbuf *s);

#ifdef __cplusplus
}
#endif

#endif

#ifndef CMARK_CMARK_CTYPE_H
#define CMARK_CMARK_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

/** Locale-independent versions of functions from ctype.h.
 * We want cmark to behave the same no matter what the system locale.
 */

int cmark_isspace(char c);

int cmark_ispunct(char c);

int cmark_isalnum(char c);

int cmark_isdigit(char c);

int cmark_isalpha(char c);

#ifdef __cplusplus
}
#endif

#endif

#ifndef CMARK_CHUNK_H
#define CMARK_CHUNK_H

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define CMARK_CHUNK_EMPTY                                                      \
  { NULL, 0, 0 }

typedef struct {
  unsigned char *data;
  bufsize_t len;
  bufsize_t alloc; // also implies a NULL-terminated string
} cmark_chunk;

static CMARK_INLINE void cmark_chunk_free(cmark_mem *mem, cmark_chunk *c) {
  if (c->alloc)
    mem->free(c->data);

  c->data = NULL;
  c->alloc = 0;
  c->len = 0;
}

static CMARK_INLINE void cmark_chunk_ltrim(cmark_chunk *c) {
  assert(!c->alloc);

  while (c->len && cmark_isspace(c->data[0])) {
    c->data++;
    c->len--;
  }
}

static CMARK_INLINE void cmark_chunk_rtrim(cmark_chunk *c) {
  assert(!c->alloc);

  while (c->len > 0) {
    if (!cmark_isspace(c->data[c->len - 1]))
      break;

    c->len--;
  }
}

static CMARK_INLINE void cmark_chunk_trim(cmark_chunk *c) {
  cmark_chunk_ltrim(c);
  cmark_chunk_rtrim(c);
}

static CMARK_INLINE bufsize_t cmark_chunk_strchr(cmark_chunk *ch, int c,
                                                 bufsize_t offset) {
  const unsigned char *p =
      (unsigned char *)memchr(ch->data + offset, c, ch->len - offset);
  return p ? (bufsize_t)(p - ch->data) : ch->len;
}

static CMARK_INLINE const char *cmark_chunk_to_cstr(cmark_mem *mem,
                                                    cmark_chunk *c) {
  unsigned char *str;

  if (c->alloc) {
    return (char *)c->data;
  }
  str = (unsigned char *)mem->calloc(c->len + 1, 1);
  if (c->len > 0) {
    memcpy(str, c->data, c->len);
  }
  str[c->len] = 0;
  c->data = str;
  c->alloc = 1;

  return (char *)str;
}

static CMARK_INLINE void cmark_chunk_set_cstr(cmark_mem *mem, cmark_chunk *c,
                                              const char *str) {
  unsigned char *old = c->alloc ? c->data : NULL;
  if (str == NULL) {
    c->len = 0;
    c->data = NULL;
    c->alloc = 0;
  } else {
    c->len = (bufsize_t)strlen(str);
    c->data = (unsigned char *)mem->calloc(c->len + 1, 1);
    c->alloc = 1;
    memcpy(c->data, str, c->len + 1);
  }
  if (old != NULL) {
    mem->free(old);
  }
}

static CMARK_INLINE cmark_chunk cmark_chunk_literal(const char *data) {
  bufsize_t len = data ? (bufsize_t)strlen(data) : 0;
  cmark_chunk c = {(unsigned char *)data, len, 0};
  return c;
}

static CMARK_INLINE cmark_chunk cmark_chunk_dup(const cmark_chunk *ch,
                                                bufsize_t pos, bufsize_t len) {
  cmark_chunk c = {ch->data + pos, len, 0};
  return c;
}

static CMARK_INLINE cmark_chunk cmark_chunk_buf_detach(cmark_strbuf *buf) {
  cmark_chunk c;

  c.len = buf->size;
  c.data = cmark_strbuf_detach(buf);
  c.alloc = 1;

  return c;
}

#endif

#ifndef CMARK_NODE_H
#define CMARK_NODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>


typedef struct {
  cmark_list_type list_type;
  int marker_offset;
  int padding;
  int start;
  cmark_delim_type delimiter;
  unsigned char bullet_char;
  bool tight;
} cmark_list;

typedef struct {
  cmark_chunk info;
  cmark_chunk literal;
  uint8_t fence_length;
  uint8_t fence_offset;
  unsigned char fence_char;
  int8_t fenced;
} cmark_code;

typedef struct {
  int level;
  bool setext;
} cmark_heading;

typedef struct {
  cmark_chunk url;
  cmark_chunk title;
} cmark_link;

typedef struct {
  cmark_chunk on_enter;
  cmark_chunk on_exit;
} cmark_custom;

enum cmark_node__internal_flags {
  CMARK_NODE__OPEN = (1 << 0),
  CMARK_NODE__LAST_LINE_BLANK = (1 << 1),
};

struct cmark_node {
  cmark_strbuf content;

  struct cmark_node *next;
  struct cmark_node *prev;
  struct cmark_node *parent;
  struct cmark_node *first_child;
  struct cmark_node *last_child;

  void *user_data;

  int start_line;
  int start_column;
  int end_line;
  int end_column;
  uint16_t type;
  uint16_t flags;

  union {
    cmark_chunk literal;
    cmark_list list;
    cmark_code code;
    cmark_heading heading;
    cmark_link link;
    cmark_custom custom;
    int html_block_type;
  } as;
};

static CMARK_INLINE cmark_mem *cmark_node_mem(cmark_node *node) {
  return node->content.mem;
}
CMARK_EXPORT int cmark_node_check(cmark_node *node, FILE *out);

#ifdef __cplusplus
}
#endif

#endif
#ifndef CMARK_HOUDINI_H
#define CMARK_HOUDINI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#ifdef HAVE___BUILTIN_EXPECT
#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

#ifdef HOUDINI_USE_LOCALE
#define _isxdigit(c) isxdigit(c)
#define _isdigit(c) isdigit(c)
#else
/*
 * Helper _isdigit methods -- do not trust the current locale
 * */
#define _isxdigit(c) (strchr("0123456789ABCDEFabcdef", (c)) != NULL)
#define _isdigit(c) ((c) >= '0' && (c) <= '9')
#endif

#define HOUDINI_ESCAPED_SIZE(x) (((x)*12) / 10)
#define HOUDINI_UNESCAPED_SIZE(x) (x)

extern bufsize_t houdini_unescape_ent(cmark_strbuf *ob, const uint8_t *src,
                                      bufsize_t size);
extern int houdini_escape_html(cmark_strbuf *ob, const uint8_t *src,
                               bufsize_t size);
extern int houdini_escape_html0(cmark_strbuf *ob, const uint8_t *src,
                                bufsize_t size, int secure);
extern int houdini_unescape_html(cmark_strbuf *ob, const uint8_t *src,
                                 bufsize_t size);
extern void houdini_unescape_html_f(cmark_strbuf *ob, const uint8_t *src,
                                    bufsize_t size);
extern int houdini_escape_href(cmark_strbuf *ob, const uint8_t *src,
                               bufsize_t size);

#ifdef __cplusplus
}
#endif

#endif
#ifndef CMARK_H
#define CMARK_H

#include <stdio.h>
#include <cmark_export.h>
#include <cmark_version.h>

#ifdef __cplusplus
extern "C" {
#endif

/** # NAME
 *
 * **cmark** - CommonMark parsing, manipulating, and rendering
 */

/** # DESCRIPTION
 *
 * ## Simple Interface
 */

/** Convert 'text' (assumed to be a UTF-8 encoded string with length
 * 'len') from CommonMark Markdown to HTML, returning a null-terminated,
 * UTF-8-encoded string. It is the caller's responsibility
 * to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_markdown_to_html(const char *text, size_t len, int options);

/** ## Node Structure
 */

typedef enum {
  /* Error status */
  CMARK_NODE_NONE,

  /* Block */
  CMARK_NODE_DOCUMENT,
  CMARK_NODE_BLOCK_QUOTE,
  CMARK_NODE_LIST,
  CMARK_NODE_ITEM,
  CMARK_NODE_CODE_BLOCK,
  CMARK_NODE_HTML_BLOCK,
  CMARK_NODE_CUSTOM_BLOCK,
  CMARK_NODE_PARAGRAPH,
  CMARK_NODE_HEADING,
  CMARK_NODE_THEMATIC_BREAK,

  CMARK_NODE_FIRST_BLOCK = CMARK_NODE_DOCUMENT,
  CMARK_NODE_LAST_BLOCK = CMARK_NODE_THEMATIC_BREAK,

  /* Inline */
  CMARK_NODE_TEXT,
  CMARK_NODE_SOFTBREAK,
  CMARK_NODE_LINEBREAK,
  CMARK_NODE_CODE,
  CMARK_NODE_HTML_INLINE,
  CMARK_NODE_CUSTOM_INLINE,
  CMARK_NODE_EMPH,
  CMARK_NODE_STRONG,
  CMARK_NODE_LINK,
  CMARK_NODE_IMAGE,

  CMARK_NODE_FIRST_INLINE = CMARK_NODE_TEXT,
  CMARK_NODE_LAST_INLINE = CMARK_NODE_IMAGE,
} cmark_node_type;

/* For backwards compatibility: */
#define CMARK_NODE_HEADER CMARK_NODE_HEADING
#define CMARK_NODE_HRULE CMARK_NODE_THEMATIC_BREAK
#define CMARK_NODE_HTML CMARK_NODE_HTML_BLOCK
#define CMARK_NODE_INLINE_HTML CMARK_NODE_HTML_INLINE

typedef enum {
  CMARK_NO_LIST,
  CMARK_BULLET_LIST,
  CMARK_ORDERED_LIST
} cmark_list_type;

typedef enum {
  CMARK_NO_DELIM,
  CMARK_PERIOD_DELIM,
  CMARK_PAREN_DELIM
} cmark_delim_type;

typedef struct cmark_node cmark_node;
typedef struct cmark_parser cmark_parser;
typedef struct cmark_iter cmark_iter;

/**
 * ## Custom memory allocator support
 */

/** Defines the memory allocation functions to be used by CMark
 * when parsing and allocating a document tree
 */
typedef struct cmark_mem {
  void *(*calloc)(size_t, size_t);
  void *(*realloc)(void *, size_t);
  void (*free)(void *);
} cmark_mem;

/**
 * ## Creating and Destroying Nodes
 */

/** Creates a new node of type 'type'.  Note that the node may have
 * other required properties, which it is the caller's responsibility
 * to assign.
 */
CMARK_EXPORT cmark_node *cmark_node_new(cmark_node_type type);

/** Same as `cmark_node_new`, but explicitly listing the memory
 * allocator used to allocate the node.  Note:  be sure to use the same
 * allocator for every node in a tree, or bad things can happen.
 */
CMARK_EXPORT cmark_node *cmark_node_new_with_mem(cmark_node_type type,
                                                 cmark_mem *mem);

/** Frees the memory allocated for a node and any children.
 */
CMARK_EXPORT void cmark_node_free(cmark_node *node);

/**
 * ## Tree Traversal
 */

/** Returns the next node in the sequence after 'node', or NULL if
 * there is none.
 */
CMARK_EXPORT cmark_node *cmark_node_next(cmark_node *node);

/** Returns the previous node in the sequence after 'node', or NULL if
 * there is none.
 */
CMARK_EXPORT cmark_node *cmark_node_previous(cmark_node *node);

/** Returns the parent of 'node', or NULL if there is none.
 */
CMARK_EXPORT cmark_node *cmark_node_parent(cmark_node *node);

/** Returns the first child of 'node', or NULL if 'node' has no children.
 */
CMARK_EXPORT cmark_node *cmark_node_first_child(cmark_node *node);

/** Returns the last child of 'node', or NULL if 'node' has no children.
 */
CMARK_EXPORT cmark_node *cmark_node_last_child(cmark_node *node);

/**
 * ## Iterator
 *
 * An iterator will walk through a tree of nodes, starting from a root
 * node, returning one node at a time, together with information about
 * whether the node is being entered or exited.  The iterator will
 * first descend to a child node, if there is one.  When there is no
 * child, the iterator will go to the next sibling.  When there is no
 * next sibling, the iterator will return to the parent (but with
 * a 'cmark_event_type' of `CMARK_EVENT_EXIT`).  The iterator will
 * return `CMARK_EVENT_DONE` when it reaches the root node again.
 * One natural application is an HTML renderer, where an `ENTER` event
 * outputs an open tag and an `EXIT` event outputs a close tag.
 * An iterator might also be used to transform an AST in some systematic
 * way, for example, turning all level-3 headings into regular paragraphs.
 *
 *     void
 *     usage_example(cmark_node *root) {
 *         cmark_event_type ev_type;
 *         cmark_iter *iter = cmark_iter_new(root);
 *
 *         while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
 *             cmark_node *cur = cmark_iter_get_node(iter);
 *             // Do something with `cur` and `ev_type`
 *         }
 *
 *         cmark_iter_free(iter);
 *     }
 *
 * Iterators will never return `EXIT` events for leaf nodes, which are nodes
 * of type:
 *
 * * CMARK_NODE_HTML_BLOCK
 * * CMARK_NODE_THEMATIC_BREAK
 * * CMARK_NODE_CODE_BLOCK
 * * CMARK_NODE_TEXT
 * * CMARK_NODE_SOFTBREAK
 * * CMARK_NODE_LINEBREAK
 * * CMARK_NODE_CODE
 * * CMARK_NODE_HTML_INLINE
 *
 * Nodes must only be modified after an `EXIT` event, or an `ENTER` event for
 * leaf nodes.
 */

typedef enum {
  CMARK_EVENT_NONE,
  CMARK_EVENT_DONE,
  CMARK_EVENT_ENTER,
  CMARK_EVENT_EXIT
} cmark_event_type;

/** Creates a new iterator starting at 'root'.  The current node and event
 * type are undefined until 'cmark_iter_next' is called for the first time.
 * The memory allocated for the iterator should be released using
 * 'cmark_iter_free' when it is no longer needed.
 */
CMARK_EXPORT
cmark_iter *cmark_iter_new(cmark_node *root);

/** Frees the memory allocated for an iterator.
 */
CMARK_EXPORT
void cmark_iter_free(cmark_iter *iter);

/** Advances to the next node and returns the event type (`CMARK_EVENT_ENTER`,
 * `CMARK_EVENT_EXIT` or `CMARK_EVENT_DONE`).
 */
CMARK_EXPORT
cmark_event_type cmark_iter_next(cmark_iter *iter);

/** Returns the current node.
 */
CMARK_EXPORT
cmark_node *cmark_iter_get_node(cmark_iter *iter);

/** Returns the current event type.
 */
CMARK_EXPORT
cmark_event_type cmark_iter_get_event_type(cmark_iter *iter);

/** Returns the root node.
 */
CMARK_EXPORT
cmark_node *cmark_iter_get_root(cmark_iter *iter);

/** Resets the iterator so that the current node is 'current' and
 * the event type is 'event_type'.  The new current node must be a
 * descendant of the root node or the root node itself.
 */
CMARK_EXPORT
void cmark_iter_reset(cmark_iter *iter, cmark_node *current,
                      cmark_event_type event_type);

/**
 * ## Accessors
 */

/** Returns the user data of 'node'.
 */
CMARK_EXPORT void *cmark_node_get_user_data(cmark_node *node);

/** Sets arbitrary user data for 'node'.  Returns 1 on success,
 * 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_user_data(cmark_node *node, void *user_data);

/** Returns the type of 'node', or `CMARK_NODE_NONE` on error.
 */
CMARK_EXPORT cmark_node_type cmark_node_get_type(cmark_node *node);

/** Like 'cmark_node_get_type', but returns a string representation
    of the type, or `"<unknown>"`.
 */
CMARK_EXPORT
const char *cmark_node_get_type_string(cmark_node *node);

/** Returns the string contents of 'node', or an empty
    string if none is set.
 */
CMARK_EXPORT const char *cmark_node_get_literal(cmark_node *node);

/** Sets the string contents of 'node'.  Returns 1 on success,
 * 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_literal(cmark_node *node, const char *content);

/** Returns the heading level of 'node', or 0 if 'node' is not a heading.
 */
CMARK_EXPORT int cmark_node_get_heading_level(cmark_node *node);

/* For backwards compatibility */
#define cmark_node_get_header_level cmark_node_get_heading_level
#define cmark_node_set_header_level cmark_node_set_heading_level

/** Sets the heading level of 'node', returning 1 on success and 0 on error.
 */
CMARK_EXPORT int cmark_node_set_heading_level(cmark_node *node, int level);

/** Returns the list type of 'node', or `CMARK_NO_LIST` if 'node'
 * is not a list.
 */
CMARK_EXPORT cmark_list_type cmark_node_get_list_type(cmark_node *node);

/** Sets the list type of 'node', returning 1 on success and 0 on error.
 */
CMARK_EXPORT int cmark_node_set_list_type(cmark_node *node,
                                          cmark_list_type type);

/** Returns the list delimiter type of 'node', or `CMARK_NO_DELIM` if 'node'
 * is not a list.
 */
CMARK_EXPORT cmark_delim_type cmark_node_get_list_delim(cmark_node *node);

/** Sets the list delimiter type of 'node', returning 1 on success and 0
 * on error.
 */
CMARK_EXPORT int cmark_node_set_list_delim(cmark_node *node,
                                           cmark_delim_type delim);

/** Returns starting number of 'node', if it is an ordered list, otherwise 0.
 */
CMARK_EXPORT int cmark_node_get_list_start(cmark_node *node);

/** Sets starting number of 'node', if it is an ordered list. Returns 1
 * on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_list_start(cmark_node *node, int start);

/** Returns 1 if 'node' is a tight list, 0 otherwise.
 */
CMARK_EXPORT int cmark_node_get_list_tight(cmark_node *node);

/** Sets the "tightness" of a list.  Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_list_tight(cmark_node *node, int tight);

/** Returns the info string from a fenced code block.
 */
CMARK_EXPORT const char *cmark_node_get_fence_info(cmark_node *node);

/** Sets the info string in a fenced code block, returning 1 on
 * success and 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_fence_info(cmark_node *node, const char *info);

/** Returns the URL of a link or image 'node', or an empty string
    if no URL is set.
 */
CMARK_EXPORT const char *cmark_node_get_url(cmark_node *node);

/** Sets the URL of a link or image 'node'. Returns 1 on success,
 * 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_url(cmark_node *node, const char *url);

/** Returns the title of a link or image 'node', or an empty
    string if no title is set.
 */
CMARK_EXPORT const char *cmark_node_get_title(cmark_node *node);

/** Sets the title of a link or image 'node'. Returns 1 on success,
 * 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_title(cmark_node *node, const char *title);

/** Returns the literal "on enter" text for a custom 'node', or
    an empty string if no on_enter is set.
 */
CMARK_EXPORT const char *cmark_node_get_on_enter(cmark_node *node);

/** Sets the literal text to render "on enter" for a custom 'node'.
    Any children of the node will be rendered after this text.
    Returns 1 on success 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_on_enter(cmark_node *node,
                                         const char *on_enter);

/** Returns the literal "on exit" text for a custom 'node', or
    an empty string if no on_exit is set.
 */
CMARK_EXPORT const char *cmark_node_get_on_exit(cmark_node *node);

/** Sets the literal text to render "on exit" for a custom 'node'.
    Any children of the node will be rendered before this text.
    Returns 1 on success 0 on failure.
 */
CMARK_EXPORT int cmark_node_set_on_exit(cmark_node *node, const char *on_exit);

/** Returns the line on which 'node' begins.
 */
CMARK_EXPORT int cmark_node_get_start_line(cmark_node *node);

/** Returns the column at which 'node' begins.
 */
CMARK_EXPORT int cmark_node_get_start_column(cmark_node *node);

/** Returns the line on which 'node' ends.
 */
CMARK_EXPORT int cmark_node_get_end_line(cmark_node *node);

/** Returns the column at which 'node' ends.
 */
CMARK_EXPORT int cmark_node_get_end_column(cmark_node *node);

/**
 * ## Tree Manipulation
 */

/** Unlinks a 'node', removing it from the tree, but not freeing its
 * memory.  (Use 'cmark_node_free' for that.)
 */
CMARK_EXPORT void cmark_node_unlink(cmark_node *node);

/** Inserts 'sibling' before 'node'.  Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_insert_before(cmark_node *node,
                                          cmark_node *sibling);

/** Inserts 'sibling' after 'node'. Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_insert_after(cmark_node *node, cmark_node *sibling);

/** Replaces 'oldnode' with 'newnode' and unlinks 'oldnode' (but does
 * not free its memory).
 * Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_replace(cmark_node *oldnode, cmark_node *newnode);

/** Adds 'child' to the beginning of the children of 'node'.
 * Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_prepend_child(cmark_node *node, cmark_node *child);

/** Adds 'child' to the end of the children of 'node'.
 * Returns 1 on success, 0 on failure.
 */
CMARK_EXPORT int cmark_node_append_child(cmark_node *node, cmark_node *child);

/** Consolidates adjacent text nodes.
 */
CMARK_EXPORT void cmark_consolidate_text_nodes(cmark_node *root);

/**
 * ## Parsing
 *
 * Simple interface:
 *
 *     cmark_node *document = cmark_parse_document("Hello *world*", 13,
 *                                                 CMARK_OPT_DEFAULT);
 *
 * Streaming interface:
 *
 *     cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
 *     FILE *fp = fopen("myfile.md", "rb");
 *     while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
 *     	   cmark_parser_feed(parser, buffer, bytes);
 *     	   if (bytes < sizeof(buffer)) {
 *     	       break;
 *     	   }
 *     }
 *     document = cmark_parser_finish(parser);
 *     cmark_parser_free(parser);
 */

/** Creates a new parser object.
 */
CMARK_EXPORT
cmark_parser *cmark_parser_new(int options);

/** Creates a new parser object with the given memory allocator
 */
CMARK_EXPORT
cmark_parser *cmark_parser_new_with_mem(int options, cmark_mem *mem);

/** Frees memory allocated for a parser object.
 */
CMARK_EXPORT
void cmark_parser_free(cmark_parser *parser);

/** Feeds a string of length 'len' to 'parser'.
 */
CMARK_EXPORT
void cmark_parser_feed(cmark_parser *parser, const char *buffer, size_t len);

/** Finish parsing and return a pointer to a tree of nodes.
 */
CMARK_EXPORT
cmark_node *cmark_parser_finish(cmark_parser *parser);

/** Parse a CommonMark document in 'buffer' of length 'len'.
 * Returns a pointer to a tree of nodes.  The memory allocated for
 * the node tree should be released using 'cmark_node_free'
 * when it is no longer needed.
 */
CMARK_EXPORT
cmark_node *cmark_parse_document(const char *buffer, size_t len, int options);

/** Parse a CommonMark document in file 'f', returning a pointer to
 * a tree of nodes.  The memory allocated for the node tree should be
 * released using 'cmark_node_free' when it is no longer needed.
 */
CMARK_EXPORT
cmark_node *cmark_parse_file(FILE *f, int options);

/**
 * ## Rendering
 */

/** Render a 'node' tree as XML.  It is the caller's responsibility
 * to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_xml(cmark_node *root, int options);

/** Render a 'node' tree as an HTML fragment.  It is up to the user
 * to add an appropriate header and footer. It is the caller's
 * responsibility to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_html(cmark_node *root, int options);

/** Render a 'node' tree as a groff man page, without the header.
 * It is the caller's responsibility to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_man(cmark_node *root, int options, int width);

/** Render a 'node' tree as a commonmark document.
 * It is the caller's responsibility to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_commonmark(cmark_node *root, int options, int width);

/** Render a 'node' tree as a LaTeX document.
 * It is the caller's responsibility to free the returned buffer.
 */
CMARK_EXPORT
char *cmark_render_latex(cmark_node *root, int options, int width);

/**
 * ## Options
 */

/** Default options.
 */
#define CMARK_OPT_DEFAULT 0

/**
 * ### Options affecting rendering
 */

/** Include a `data-sourcepos` attribute on all block elements.
 */
#define CMARK_OPT_SOURCEPOS (1 << 1)

/** Render `softbreak` elements as hard line breaks.
 */
#define CMARK_OPT_HARDBREAKS (1 << 2)

/** Suppress raw HTML and unsafe links (`javascript:`, `vbscript:`,
 * `file:`, and `data:`, except for `image/png`, `image/gif`,
 * `image/jpeg`, or `image/webp` mime types).  Raw HTML is replaced
 * by a placeholder HTML comment. Unsafe links are replaced by
 * empty strings.
 */
#define CMARK_OPT_SAFE (1 << 3)

/** Render `softbreak` elements as spaces.
 */
#define CMARK_OPT_NOBREAKS (1 << 4)

/**
 * ### Options affecting parsing
 */

/** Normalize tree by consolidating adjacent text nodes.
 */
#define CMARK_OPT_NORMALIZE (1 << 8)

/** Validate UTF-8 in the input before parsing, replacing illegal
 * sequences with the replacement character U+FFFD.
 */
#define CMARK_OPT_VALIDATE_UTF8 (1 << 9)

/** Convert straight quotes to curly, --- to em dashes, -- to en dashes.
 */
#define CMARK_OPT_SMART (1 << 10)

/**
 * ## Version information
 */

/** The library version as integer for runtime checks. Also available as
 * macro CMARK_VERSION for compile time checks.
 *
 * * Bits 16-23 contain the major version.
 * * Bits 8-15 contain the minor version.
 * * Bits 0-7 contain the patchlevel.
 *
 * In hexadecimal format, the number 0x010203 represents version 1.2.3.
 */
CMARK_EXPORT
int cmark_version(void);

/** The library version string for runtime checks. Also available as
 * macro CMARK_VERSION_STRING for compile time checks.
 */
CMARK_EXPORT
const char *cmark_version_string(void);

/** # AUTHORS
 *
 * John MacFarlane, Vicent Marti,  Kārlis Gaņģis, Nick Wellnhofer.
 */

#ifndef CMARK_NO_SHORT_NAMES
#define NODE_DOCUMENT CMARK_NODE_DOCUMENT
#define NODE_BLOCK_QUOTE CMARK_NODE_BLOCK_QUOTE
#define NODE_LIST CMARK_NODE_LIST
#define NODE_ITEM CMARK_NODE_ITEM
#define NODE_CODE_BLOCK CMARK_NODE_CODE_BLOCK
#define NODE_HTML_BLOCK CMARK_NODE_HTML_BLOCK
#define NODE_CUSTOM_BLOCK CMARK_NODE_CUSTOM_BLOCK
#define NODE_PARAGRAPH CMARK_NODE_PARAGRAPH
#define NODE_HEADING CMARK_NODE_HEADING
#define NODE_HEADER CMARK_NODE_HEADER
#define NODE_THEMATIC_BREAK CMARK_NODE_THEMATIC_BREAK
#define NODE_HRULE CMARK_NODE_HRULE
#define NODE_TEXT CMARK_NODE_TEXT
#define NODE_SOFTBREAK CMARK_NODE_SOFTBREAK
#define NODE_LINEBREAK CMARK_NODE_LINEBREAK
#define NODE_CODE CMARK_NODE_CODE
#define NODE_HTML_INLINE CMARK_NODE_HTML_INLINE
#define NODE_CUSTOM_INLINE CMARK_NODE_CUSTOM_INLINE
#define NODE_EMPH CMARK_NODE_EMPH
#define NODE_STRONG CMARK_NODE_STRONG
#define NODE_LINK CMARK_NODE_LINK
#define NODE_IMAGE CMARK_NODE_IMAGE
#define BULLET_LIST CMARK_BULLET_LIST
#define ORDERED_LIST CMARK_ORDERED_LIST
#define PERIOD_DELIM CMARK_PERIOD_DELIM
#define PAREN_DELIM CMARK_PAREN_DELIM
#endif

#ifdef __cplusplus
}
#endif

#endif

#ifndef CMARK_EXPORT_H
#define CMARK_EXPORT_H

#ifdef CMARK_STATIC_DEFINE
#  define CMARK_EXPORT
#  define CMARK_NO_EXPORT
#else
#  ifndef CMARK_EXPORT
#    ifdef libcmark_EXPORTS
        /* We are building this library */
#      define CMARK_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define CMARK_EXPORT __attribute__((visibility("default")))
#    endif
#  endif

#  ifndef CMARK_NO_EXPORT
#    define CMARK_NO_EXPORT __attribute__((visibility("hidden")))
#  endif
#endif

#ifndef CMARK_DEPRECATED
#  define CMARK_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef CMARK_DEPRECATED_EXPORT
#  define CMARK_DEPRECATED_EXPORT CMARK_EXPORT CMARK_DEPRECATED
#endif

#ifndef CMARK_DEPRECATED_NO_EXPORT
#  define CMARK_DEPRECATED_NO_EXPORT CMARK_NO_EXPORT CMARK_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef CMARK_NO_DEPRECATED
#    define CMARK_NO_DEPRECATED
#  endif
#endif

#endif
#ifndef CMARK_VERSION_H
#define CMARK_VERSION_H

#define CMARK_VERSION ((0 << 16) | (27 << 8)  | 1)
#define CMARK_VERSION_STRING "0.27.1"

#endif


#ifndef CMARK_AST_H
#define CMARK_AST_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LINK_LABEL_LENGTH 1000

struct cmark_parser {
  struct cmark_mem *mem;
  struct cmark_reference_map *refmap;
  struct cmark_node *root;
  struct cmark_node *current;
  int line_number;
  bufsize_t offset;
  bufsize_t column;
  bufsize_t first_nonspace;
  bufsize_t first_nonspace_column;
  int indent;
  bool blank;
  bool partially_consumed_tab;
  cmark_strbuf curline;
  bufsize_t last_line_length;
  cmark_strbuf linebuf;
  int options;
  bool last_buffer_ended_with_cr;
};

#ifdef __cplusplus
}
#endif

#endif


#ifndef CMARK_REFERENCES_H
#define CMARK_REFERENCES_H


#ifdef __cplusplus
extern "C" {
#endif

#define REFMAP_SIZE 16

struct cmark_reference {
  struct cmark_reference *next;
  unsigned char *label;
  cmark_chunk url;
  cmark_chunk title;
  unsigned int hash;
};

typedef struct cmark_reference cmark_reference;

struct cmark_reference_map {
  cmark_mem *mem;
  cmark_reference *table[REFMAP_SIZE];
};

typedef struct cmark_reference_map cmark_reference_map;

cmark_reference_map *cmark_reference_map_new(cmark_mem *mem);
void cmark_reference_map_free(cmark_reference_map *map);
cmark_reference *cmark_reference_lookup(cmark_reference_map *map,
                                        cmark_chunk *label);
extern void cmark_reference_create(cmark_reference_map *map, cmark_chunk *label,
                                   cmark_chunk *url, cmark_chunk *title);

#ifdef __cplusplus
}
#endif

#endif

#ifndef CMARK_UTF8_H
#define CMARK_UTF8_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cmark_utf8proc_case_fold(cmark_strbuf *dest, const uint8_t *str,
			      bufsize_t len);
void cmark_utf8proc_encode_char(int32_t uc, cmark_strbuf *buf);
int cmark_utf8proc_iterate(const uint8_t *str, bufsize_t str_len, int32_t *dst);
void cmark_utf8proc_check(cmark_strbuf *dest, const uint8_t *line,
			  bufsize_t size);
int cmark_utf8proc_is_space(int32_t uc);
int cmark_utf8proc_is_punctuation(int32_t uc);

#ifdef __cplusplus
}
#endif

#endif


#ifndef CMARK_INLINES_H
#define CMARK_INLINES_H

#ifdef __cplusplus
extern "C" {
#endif

cmark_chunk cmark_clean_url(cmark_mem *mem, cmark_chunk *url);
cmark_chunk cmark_clean_title(cmark_mem *mem, cmark_chunk *title);

void cmark_parse_inlines(cmark_mem *mem, cmark_node *parent,
                         cmark_reference_map *refmap, int options);

bufsize_t cmark_parse_reference_inline(cmark_mem *mem, cmark_strbuf *input,
                                       cmark_reference_map *refmap);

#ifdef __cplusplus
}
#endif

#endif


#ifdef __cplusplus
extern "C" {
#endif

bufsize_t _scan_at(bufsize_t (*scanner)(const unsigned char *), cmark_chunk *c,
                   bufsize_t offset);
bufsize_t _scan_scheme(const unsigned char *p);
bufsize_t _scan_autolink_uri(const unsigned char *p);
bufsize_t _scan_autolink_email(const unsigned char *p);
bufsize_t _scan_html_tag(const unsigned char *p);
bufsize_t _scan_html_block_start(const unsigned char *p);
bufsize_t _scan_html_block_start_7(const unsigned char *p);
bufsize_t _scan_html_block_end_1(const unsigned char *p);
bufsize_t _scan_html_block_end_2(const unsigned char *p);
bufsize_t _scan_html_block_end_3(const unsigned char *p);
bufsize_t _scan_html_block_end_4(const unsigned char *p);
bufsize_t _scan_html_block_end_5(const unsigned char *p);
bufsize_t _scan_link_title(const unsigned char *p);
bufsize_t _scan_spacechars(const unsigned char *p);
bufsize_t _scan_atx_heading_start(const unsigned char *p);
bufsize_t _scan_setext_heading_line(const unsigned char *p);
bufsize_t _scan_thematic_break(const unsigned char *p);
bufsize_t _scan_open_code_fence(const unsigned char *p);
bufsize_t _scan_close_code_fence(const unsigned char *p);
bufsize_t _scan_entity(const unsigned char *p);
bufsize_t _scan_dangerous_url(const unsigned char *p);

#define scan_scheme(c, n) _scan_at(&_scan_scheme, c, n)
#define scan_autolink_uri(c, n) _scan_at(&_scan_autolink_uri, c, n)
#define scan_autolink_email(c, n) _scan_at(&_scan_autolink_email, c, n)
#define scan_html_tag(c, n) _scan_at(&_scan_html_tag, c, n)
#define scan_html_block_start(c, n) _scan_at(&_scan_html_block_start, c, n)
#define scan_html_block_start_7(c, n) _scan_at(&_scan_html_block_start_7, c, n)
#define scan_html_block_end_1(c, n) _scan_at(&_scan_html_block_end_1, c, n)
#define scan_html_block_end_2(c, n) _scan_at(&_scan_html_block_end_2, c, n)
#define scan_html_block_end_3(c, n) _scan_at(&_scan_html_block_end_3, c, n)
#define scan_html_block_end_4(c, n) _scan_at(&_scan_html_block_end_4, c, n)
#define scan_html_block_end_5(c, n) _scan_at(&_scan_html_block_end_5, c, n)
#define scan_link_title(c, n) _scan_at(&_scan_link_title, c, n)
#define scan_spacechars(c, n) _scan_at(&_scan_spacechars, c, n)
#define scan_atx_heading_start(c, n) _scan_at(&_scan_atx_heading_start, c, n)
#define scan_setext_heading_line(c, n)                                         \
  _scan_at(&_scan_setext_heading_line, c, n)
#define scan_thematic_break(c, n) _scan_at(&_scan_thematic_break, c, n)
#define scan_open_code_fence(c, n) _scan_at(&_scan_open_code_fence, c, n)
#define scan_close_code_fence(c, n) _scan_at(&_scan_close_code_fence, c, n)
#define scan_entity(c, n) _scan_at(&_scan_entity, c, n)
#define scan_dangerous_url(c, n) _scan_at(&_scan_dangerous_url, c, n)

#ifdef __cplusplus
}
#endif


#ifndef CMARK_ITERATOR_H
#define CMARK_ITERATOR_H

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
  cmark_event_type ev_type;
  cmark_node *node;
} cmark_iter_state;

struct cmark_iter {
  cmark_mem *mem;
  cmark_node *root;
  cmark_iter_state cur;
  cmark_iter_state next;
};

#ifdef __cplusplus
}
#endif

#endif
