/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://mozilla.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 *
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */


/*
 * fastpath.c --
 *
 * Get page possibly from a file cache.
 */

#include "nsd.h"

NS_RCSID("@(#) $Header$");


/*
 * The following structures define the offsets parsed
 * from the "Range:" request header
 */

#define MAX_RANGES  (NS_CONN_MAXBUFS/3)

typedef struct {
    Tcl_WideInt start;   /* Start position */
    Tcl_WideInt end;     /* End position (inclusive) */
    Tcl_WideInt size;    /* Absolute size in bytes */
} RangeOffset;

typedef struct {
    int           status;  /* Return status: 206 or 416 */
    int           count;   /* Number of valid ranges parsed */
    Tcl_WideInt size;    /* File size */
    unsigned long mtime;   /* File modification time */
    RangeOffset   offsets[MAX_RANGES];
} Range;

/*
 * The following structure defines the contents of a file
 * stored in the file cache.
 */

typedef struct {
    time_t mtime;
    int    size;
    int    refcnt;
    char   bytes[1];  /* Grown to actual file size. */
} File;


/*
 * Local functions defined in this file
 */

static Ns_Callback FreeEntry;

static void DecrEntry      (File *filePtr);
static int  UrlIs          (CONST char *server, CONST char *url, int dir);
static int  FastStat       (CONST char *file, Tcl_StatBuf *stPtr);
static int  FastGetRestart (Ns_Conn *conn, CONST char *page);
static int  ParseRange     (Ns_Conn *conn, Range *rangesPtr);
static int  FastReturn     (NsServer *servPtr, Ns_Conn *conn, int status,
                            CONST char *type, CONST char *file,
                            Tcl_StatBuf *stPtr);
static int  ReturnRange    (Ns_Conn *conn, Range *rangesPtr, Tcl_Channel chan,
                            CONST char *data, Tcl_WideInt len, CONST char *type);


/*
 * Local variables defined in this file.
 */

static Ns_Cache *cache;    /* Global cache of pages for all virtual servers.     */
static int       maxentry; /* Maximum size of an individual entry in the cache.  */
static int       usemmap;  /* Use the mmap() system call to read data from disk. */



/*
 *----------------------------------------------------------------------
 * NsConfigFastpath --
 *
 *      Initialize the global fastpath cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
NsConfigFastpath()
{
    char  *path;

#ifdef _WIN32
#  define CACHE_KEYS TCL_STRING_KEYS
#else
#  define CACHE_KEYS FILE_KEYS
#endif

    path = Ns_ConfigGetPath(NULL, NULL, "fastpath", NULL);
    usemmap = Ns_ConfigBool(path, "mmap", NS_FALSE);
    if (Ns_ConfigBool(path, "cache", NS_FALSE)) {
        cache = Ns_CacheCreateSz("nsfp", CACHE_KEYS,
                    Ns_ConfigIntRange(path, "cachemaxsize", 1024*10000, 1024, INT_MAX),
                    FreeEntry);
        maxentry =  Ns_ConfigIntRange(path, "cachemaxentry", 8192, 8, INT_MAX);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnFile --
 *
 *      Send the contents of a file out the conn.
 *
 * Results:
 *      NS_OK/NS_ERROR
 *
 * Side effects:
 *      See FastReturn.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnFile(Ns_Conn *conn, int status, CONST char *type, CONST char *file)
{
    Tcl_StatBuf  st;
    char        *server;
    NsServer    *servPtr;

    if (FastStat(file, &st) == 0) {
        return Ns_ConnReturnNotFound(conn);
    }

    server  = Ns_ConnServer(conn);
    servPtr = NsGetServer(server);

    return FastReturn(servPtr, conn, status, type, file, &st);
}


/*
 *----------------------------------------------------------------------
 * Ns_PageRoot --
 *
 *      Return path name of the server pages directory.
 *      Depreciated: Use Ns_PagePath() which is virtual host aware.
 *
 * Results:
 *      Server pageroot or NULL on invalid server.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

CONST char *
Ns_PageRoot(CONST char *server)
{
    NsServer *servPtr = NsGetServer(server);

    if (servPtr != NULL) {
        return servPtr->fastpath.pageroot;
    }

    return NULL;
}


/*
 *----------------------------------------------------------------------
 * Ns_UrlIsFile, Ns_UrlIsDir --
 *
 *      Check if a file/directory that corresponds to a URL exists.
 *
 * Results:
 *      Return NS_TRUE if the file exists and NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_UrlIsFile(CONST char *server, CONST char *url)
{
    return UrlIs(server, url, 0);
}

int
Ns_UrlIsDir(CONST char *server, CONST char *url)
{
    return UrlIs(server, url, 1);
}

static int
UrlIs(CONST char *server, CONST char *url, int dir)
{
    Ns_DString   ds;
    int          status, is = NS_FALSE;
    Tcl_Obj     *path;
    Tcl_StatBuf  st;

    Ns_DStringInit(&ds);
    if (Ns_UrlToFile(&ds, server, url) == NS_OK) {
        path = Tcl_NewStringObj(ds.string, -1);
        Tcl_IncrRefCount(path);
        status = Tcl_FSStat(path, &st);
        Tcl_DecrRefCount(path);
        if (status == 0
            && ((dir && S_ISDIR(st.st_mode))
                || (dir == NS_FALSE && S_ISREG(st.st_mode)))) {
            is = NS_TRUE;
        }
    }
    Ns_DStringFree(&ds);

    return is;
}


/*
 *----------------------------------------------------------------------
 * Ns_FastPathProc, NsFastPathProc --
 *
 *      Return the contents of a URL.
 *
 * Results:
 *      Return NS_OK for success or NS_ERROR for failure.
 *
 * Side effects:
 *      Contents of file may be cached in file cache.
 *
 *----------------------------------------------------------------------
 */

int
Ns_FastPathProc(void *arg, Ns_Conn *conn)
{
    char     *server = arg;
    NsServer *servPtr;

    if ((servPtr = NsGetServer(server)) == NULL) {
        return NS_ERROR;
    }
    return NsFastPathProc(servPtr, conn);
}

int
NsFastPathProc(void *arg, Ns_Conn *conn)
{
    Ns_DString   ds;
    NsServer    *servPtr = arg;
    char        *url = conn->request->url;
    int          status, result, i;
    Tcl_Obj     *path;
    Tcl_StatBuf  st;

    Ns_DStringInit(&ds);
    if (NsUrlToFile(&ds, servPtr, url) != NS_OK || !FastStat(ds.string, &st)) {
        goto notfound;
    }
    if (S_ISREG(st.st_mode)) {

        /*
         * Return ordinary files as with Ns_ConnReturnFile.
         */

        result = FastReturn(servPtr, conn, 200, NULL, ds.string, &st);

    } else if (S_ISDIR(st.st_mode)) {

        /*
         * For directories, search for a matching directory file and
         * restart the connection if found.
         */

        for (i = 0; i < servPtr->fastpath.dirc; ++i) {
            Ns_DStringSetLength(&ds, 0);
            if (NsUrlToFile(&ds, servPtr, url) != NS_OK) {
                goto notfound;
            }
            Ns_DStringVarAppend(&ds, "/", servPtr->fastpath.dirv[i], NULL);
            path = Tcl_NewStringObj(ds.string, -1);
            Tcl_IncrRefCount(path);
            status = Tcl_FSStat(path, &st);
            Tcl_DecrRefCount(path);
            if (status == 0 && S_ISREG(st.st_mode)) {
                if (url[strlen(url) - 1] != '/') {
                    Ns_DStringSetLength(&ds, 0);
                    Ns_DStringVarAppend(&ds, url, "/", NULL);
                    result = Ns_ConnReturnRedirect(conn, ds.string);
                } else {
                    result = FastGetRestart(conn, servPtr->fastpath.dirv[i]);
                }
                goto done;
            }
        }

        /*
         * If no index file was found, invoke a directory listing
         * ADP or Tcl proc if configured.
         */

        if (servPtr->fastpath.diradp != NULL) {
            result = Ns_AdpRequest(conn, servPtr->fastpath.diradp);
        } else if (servPtr->fastpath.dirproc != NULL) {
            result = Ns_TclRequest(conn, servPtr->fastpath.dirproc);
        } else {
            goto notfound;
        }
    } else {
    notfound:
        result = Ns_ConnReturnNotFound(conn);
    }

 done:
    Ns_DStringFree(&ds);

    return result;
}


/*
 *----------------------------------------------------------------------
 * FastGetRestart --
 *
 *      Construct the full URL and redirect internally.
 *
 * Results:
 *      See Ns_ConnRedirect().
 *
 * Side effects:
 *      See Ns_ConnRedirect().
 *
 *----------------------------------------------------------------------
 */

static int
FastGetRestart(Ns_Conn *conn, CONST char *page)
{
    int        status;
    Ns_DString ds;

    Ns_DStringInit(&ds);
    Ns_MakePath(&ds, conn->request->url, page, NULL);
    status = Ns_ConnRedirect(conn, ds.string);
    Ns_DStringFree(&ds);

    return status;
}


/*
 *----------------------------------------------------------------------
 * FreeEntry --
 *
 *      Logically remove a cached file from file cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeEntry(void *arg)
{
    File *filePtr = (File *) arg;

    DecrEntry(filePtr);
}


/*
 *----------------------------------------------------------------------
 * DecrEntry --
 *
 *      Decrement reference count of cached file.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
DecrEntry(File *filePtr)
{
    if (--filePtr->refcnt == 0) {
        ns_free(filePtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * FastStat --
 *
 *      Stat a file, logging an error on unexpected results.
 *
 * Results:
 *      1 if stat ok, 0 otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
FastStat(CONST char *file, Tcl_StatBuf *stPtr)
{
    Tcl_Obj *path;
    int      status;

    path = Tcl_NewStringObj(file, -1);
    Tcl_IncrRefCount(path);
    status = Tcl_FSStat(path, stPtr);
    Tcl_DecrRefCount(path);

    if (status != 0) {
        if (Tcl_GetErrno() != ENOENT && Tcl_GetErrno() != EACCES) {
            Ns_Log(Error, "fastpath: stat(%s) failed: %s",
                   file, strerror(Tcl_GetErrno()));
        }
        return 0;
    }

    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * FastReturn --
 *
 *      Return file contents, possibly from cache.
 *
 * Results:
 *      Standard Ns_Request result.
 *
 * Side effects:
 *      May map, cache, open, and/or send file out connection.
 *
 *----------------------------------------------------------------------
 */

static int
FastReturn(NsServer *servPtr, Ns_Conn *conn, int status, CONST char *type,
           CONST char *file, Tcl_StatBuf *stPtr)
{
    int         new, nread, result = NS_ERROR;
    Range       range;
    char       *key;
    Ns_Entry   *entry;
    File       *filePtr;
    FileMap     fmap;
    Tcl_Channel chan = NULL;

#ifndef _WIN32
    FileKey     ukey;
#endif

    /*
     *  Initialize the structure for possible Range: requests
     */

    range.status = status;
    range.size   = stPtr->st_size;
    range.mtime  = stPtr->st_mtime;

    /*
     * Determine the mime type if not given.
     */

    if (type == NULL) {
        type = Ns_GetMimeType(file);
    }

    /*
     * Set the last modified header if not set yet.
     * If not modified since last request, return now.
     */

    Ns_ConnSetLastModifiedHeader(conn, &stPtr->st_mtime);
    if (Ns_ConnModifiedSince(conn, stPtr->st_mtime) == NS_FALSE) {
        return Ns_ConnReturnNotModified(conn);
    }

    /*
     * For no output (i.e., HEAD request), just send required
     * headers.
     */

    if (conn->flags & NS_CONN_SKIPBODY) {
        Ns_ConnSetRequiredHeaders(conn, type, stPtr->st_size);
        return Ns_ConnFlushHeaders(conn, status);
    }

    /*
     * Check if this is a Range: request and parse the
     * requested ranges..
     */

    if (ParseRange(conn, &range) == NS_ERROR) {
        Ns_ConnPrintfHeaders(conn, "Content-Range", "bytes */%llu", range.size);
        return Ns_ConnReturnStatus(conn, range.status);
    }

    /*
     * Depending on the size of the content and state of the fastpath cache,
     * either return the data directly, or cache it first and return the
     * cached copy.
     */

    if (cache == NULL || stPtr->st_size > maxentry) {

        /*
         * Caching is disabled or the entry is too large for the cache
         * so send the content directly.
         */

        if (usemmap
            && NsMemMap(file, stPtr->st_size, NS_MMAP_READ, &fmap) == NS_OK) {
            result = ReturnRange(conn, &range, NULL, fmap.addr, fmap.size, type);
            NsMemUmap(&fmap);
        } else {
            chan = Tcl_OpenFileChannel(NULL, file, "r", 0644);
            if (chan == NULL) {
                Ns_Log(Warning, "fastpath: failed to open '%s': '%s'",
                       file, strerror(Tcl_GetErrno()));
                goto notfound;
            }
            Tcl_SetChannelOption(NULL, chan, "-translation", "binary");
            result = ReturnRange(conn, &range, chan, 0, stPtr->st_size, type);
            Tcl_Close(NULL, chan);
        }

    } else {

        /*
         * Search for an existing cache entry for this file, validating
         * the contents against the current file mtime and size.
         */

#ifdef _WIN32
        key = file;
#else
        ukey.dev = stPtr->st_dev;
        ukey.ino = stPtr->st_ino;
        key = (char *) &ukey;
#endif
        filePtr = NULL;
        Ns_CacheLock(cache);
        entry = Ns_CacheWaitCreateEntry(cache, key, &new, NULL);

        /*
         * Validate entry.
         */

        if (!new
            && (filePtr = Ns_CacheGetValue(entry)) != NULL
            && (filePtr->mtime != stPtr->st_mtime
                || filePtr->size != stPtr->st_size)) {
            Ns_CacheUnsetValue(entry);
            new = 1;
        }

        if (new) {

            /*
             * Read and cache new or invalidated entries in one big chunk.
             */

            Ns_CacheUnlock(cache);
            chan = Tcl_OpenFileChannel(NULL, file, "r", 0644);
            if (chan == NULL) {
                filePtr = NULL;
                Ns_Log(Warning, "fastpath: failed to open '%s': '%s'",
                       file, strerror(Tcl_GetErrno()));
            } else {
                Tcl_SetChannelOption(NULL, chan, "-translation", "binary");
            }
            if (chan) {
                filePtr = ns_malloc(sizeof(File) + stPtr->st_size);
                filePtr->refcnt = 1;
                filePtr->size   = stPtr->st_size;
                filePtr->mtime  = stPtr->st_mtime;
                nread = Tcl_Read(chan, filePtr->bytes, filePtr->size);
                Tcl_Close(NULL, chan);
                if (nread != filePtr->size) {
                    Ns_Log(Warning, "fastpath: failed to read '%s': '%s'",
                           file, strerror(Tcl_GetErrno()));
                    ns_free(filePtr);
                    filePtr = NULL;
                }
            }
            Ns_CacheLock(cache);
            entry = Ns_CacheCreateEntry(cache, key, &new);
            if (filePtr != NULL) {
                Ns_CacheSetValueSz(entry, filePtr, (size_t) (filePtr->size + sizeof(File)));
            } else {
                Ns_CacheDeleteEntry(entry);
            }
            Ns_CacheBroadcast(cache);
        }
        if (filePtr != NULL) {
            ++filePtr->refcnt;
            Ns_CacheUnlock(cache);
            result = ReturnRange(conn, &range, NULL, filePtr->bytes,
                                 filePtr->size, type);
            Ns_CacheLock(cache);
            DecrEntry(filePtr);
        }
        Ns_CacheUnlock(cache);
        if (filePtr == NULL) {
            goto notfound;
        }
    }

    return result;

 notfound:

    return Ns_ConnReturnNotFound(conn);
}


/*
 *----------------------------------------------------------------------
 *
 * ParseRange --
 *
 *      Checks for presence of "Range:" header, parses it and fills-in
 *      the parsed range offsets.
 *
 * Results:
 *      NS_ERROR: byte-range is syntactically correct but unsatisfiable
 *      NS_OK: parsed ok; rnPtr->count has the number of ranges parsed
 *
 * Side effects:
 *      All byte-range-sets beyond MAX_RANGES will be ignored
 *
 *----------------------------------------------------------------------
 */

static int
ParseRange(Ns_Conn *conn, Range *rangesPtr)
{
    int          index = 0;
    char        *rangestr, *httptime;
    RangeOffset *thisPtr = NULL, *prevPtr = NULL;

    /*
     * Initially, assume no ranges found and assume
     * the content is to be returned as a whole.
     */

    rangesPtr->count  = 0;
    rangesPtr->status = 200;

    /*
     * Check for valid "Range:" header
     */

    rangestr = Ns_SetIGet(conn->headers, "Range");
    if (rangestr == NULL) {
        return NS_OK;
    }

    /*
     * Check for "If-Range:"; and return the whole file, if changed.
     */

    httptime = Ns_SetIGet(conn->headers, "If-Range");
    if (httptime && rangesPtr->mtime > Ns_ParseHttpTime(httptime)) {
        return NS_OK;
    }

    /*
     * Parse the header value and fill-in ranges.
     * See RFC 2616 "14.35.1 Byte Ranges" for the syntax.
     */

    rangestr = strstr(rangestr, "bytes=");
    if (rangestr == NULL) {
        return NS_OK;
    }
    rangestr += 6; /* Skip "bytes=" */

    while (*rangestr && index < MAX_RANGES-1) {

        thisPtr = &rangesPtr->offsets[index];
        thisPtr->start = 0;
        thisPtr->end   = 0;
        thisPtr->size  = 0;

        if (isdigit(UCHAR(*rangestr))) {

            /*
             * Parse: first-byte-pos "-" last-byte-pos
             */

            thisPtr->start = atoll(rangestr);
            while (isdigit(UCHAR(*rangestr))) {
                rangestr++;
            }
            if (*rangestr != '-') {
                return NS_OK; /* Invalid syntax? */
            }
            rangestr++; /* Skip '-' */
            if (!isdigit(UCHAR(*rangestr))) {
                thisPtr->end = rangesPtr->size - 1;
            } else {
                thisPtr->end = atoll(rangestr);
                while (isdigit(UCHAR(*rangestr))) {
                    rangestr++;
                }
                if (thisPtr->end >= rangesPtr->size) {
                    thisPtr->end = rangesPtr->size - 1;
                }
            }

        } else if (*rangestr == '-') {

            /*
             * Parse: "-" suffix-length
             */

            rangestr++; /* Skip '-' */
            if (!isdigit(UCHAR(*rangestr))) {
                return NS_OK; /* Invalid syntax? */
            }
            thisPtr->end = atoll(rangestr);
            while (isdigit(UCHAR(*rangestr))) {
                rangestr++;
            }
            if (thisPtr->end >= rangesPtr->size) {
                thisPtr->end  = rangesPtr->size;
            }

            /*
             * Size from the end; convert into count
             */

            thisPtr->start = rangesPtr->size - thisPtr->end;
            thisPtr->end = thisPtr->start + thisPtr->end - 1;

        } else {

            /*
             * Invalid syntax?
             */

            return NS_OK;
        }

        /*
         * Check end of range_spec
         */

        switch (*rangestr) {
        case ',':
            rangestr++;
            break;
        case '\0':
            break;
        default:
            return NS_OK; /* Invalid syntax? */
        }

        /*
         * We are now done with the syntax of the range so go check
         * the semantics of the values...
         */

        /*
         * RFC 2616: 416 "Requested Range Not Satisfiable"
         *
         * "if first-byte-pos of all of the byte-range-spec values were
         *  greater than the current length of the selected resource"
         *
         * This is not clear: "all of the..." means *each-and-every*
         * first-byte-pos MUST be greater than the resource length.
         *
         * We opt to implement "any of the..." rather ...
         */

        if (thisPtr->start >= rangesPtr->size) {
            rangesPtr->status = 416;
            return NS_ERROR;
        }

        /*
         * RFC 2616: 14.35.1 Byte Ranges
         *
         *  "If the last-byte-pos value is present, it MUST be greater
         *   than or equal to the first-byte-pos in that byte-range-spec,
         *   or the byte-range-spec is syntactically invalid."
         *
         */

        if (thisPtr->end < thisPtr->start) {
            return NS_OK;
        }

        /*
         * Check this range overlapping with the former.
         * The standard does not cleary specify how to
         * check those. Therefore, here is what we do:
         *
         *  a. for non-overlapping ranges: keep both
         *  b. for overlapping ranges: collapse into one
         */

        if (prevPtr == NULL
            || (thisPtr->start > (prevPtr->end + 1))
            || (prevPtr->start && thisPtr->end < (prevPtr->start - 1))) {
            /* a. */
            prevPtr = thisPtr;
            index++; /* One more valid range */
        } else {
            /* b. */
            prevPtr->start = MIN(prevPtr->start, thisPtr->start);
            prevPtr->end   = MAX(prevPtr->end,   thisPtr->end);
        }

        prevPtr->size = prevPtr->end - prevPtr->start + 1;
    }

    if (index) {
        rangesPtr->count  = index;
        rangesPtr->status = 206; /* "Partial Content" */
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnRange --
 *
 *	Sets required headers, dumps them, and then writes the data.
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	May set numerous headers, will close connection.
 *  MAX_RANGES depends on NS_CONN_MAXBUFS which is used by Ns_ConnSend
 *
 *----------------------------------------------------------------------
 */

static int
ReturnRange(Ns_Conn *conn, Range *rangesPtr, Tcl_Channel chan,
            CONST char *data, Tcl_WideInt len, CONST char *type)
{
    struct iovec bufs[MAX_RANGES*3], *iovPtr = bufs;
    int          status, i, result = NS_ERROR;
    char         boundary[32];
    time_t       now = time(0);
    Ns_DString   ds;
    RangeOffset *roPtr;

    switch (rangesPtr->count) {
    case 0:

        /*
         * No ranges: send all data, close connection and return
         */

        status = rangesPtr->status;

        if (chan) {
            return Ns_ConnReturnOpenChannel(conn, status, type, chan, len);
        } else {
            return Ns_ConnReturnData(conn, status, data, len, type);
        }

    case 1:

        /*
         * Single byte-range-set: global Content-Range: header
         * will be included in the reply
         */

        roPtr = &rangesPtr->offsets[0];
        Ns_ConnPrintfHeaders(conn, "Content-range", "bytes %llu-%llu/%llu",
                             roPtr->start, roPtr->end, len);

        Ns_ConnSetRequiredHeaders(conn, type, roPtr->size);
        Ns_ConnQueueHeaders(conn, rangesPtr->status);

        if (chan) {
            Tcl_Seek(chan, roPtr->start, SEEK_SET);
            result = Ns_ConnSendChannel(conn, chan, roPtr->size);
        } else {
            iovPtr->iov_base = (char *) data + roPtr->start;
            iovPtr->iov_len  = roPtr->size;
            result = Ns_ConnSend(conn, iovPtr, 1);
        }
        break;

    default:

         /*
          * Multiple ranges, return as multipart/byterange
          */

        Ns_DStringInit(&ds);
        sprintf(boundary,"%lu", now);
        Ns_ConnPrintfHeaders(conn, "Content-type",
                             "multipart/byteranges; boundary=%s", boundary);
        /*
         * Use 3 iovec structures for each range to contain
         * starting boundary headers, data and closing boundary.
         */

        /*
         * First pass, produce headers and calculate content length
         */

        rangesPtr->size = 0;

        for (i = 0; i < rangesPtr->count; i++) {
            roPtr = &rangesPtr->offsets[i];

            /*
             * Point to first iov struct for the given index
             */

            iovPtr = &bufs[i*3];

            /*
             * First io vector in the triple will hold the headers
             */

            iovPtr->iov_base = &ds.string[ds.length];
            Ns_DStringPrintf(&ds,"--%s\r\n",boundary);
            Ns_DStringPrintf(&ds,"Content-type: %s\r\n",type);
            Ns_DStringPrintf(&ds,"Content-range: bytes %llu-%llu/%llu\r\n\r\n",
                             roPtr->start, roPtr->end, len);
            iovPtr->iov_len = strlen(iovPtr->iov_base);
            rangesPtr->size += iovPtr->iov_len;

            /*
             * Second io vector will contain actual range buffer offset
             * and size. It will be ignored in chan mode.
             */

            iovPtr++;
            iovPtr->iov_base = (char *) data + roPtr->start;
            iovPtr->iov_len  = roPtr->size;
            rangesPtr->size += iovPtr->iov_len;

            /*
             * Third io vector will hold closing boundary
             */

            iovPtr++;
            iovPtr->iov_base = &ds.string[ds.length];

            /*
             * Last boundary should have trailing --
             */

            if (i == rangesPtr->count - 1) {
                Ns_DStringPrintf(&ds,"\r\n--%s--",boundary);
            }

            Ns_DStringAppend(&ds,"\r\n");
            iovPtr->iov_len = strlen(iovPtr->iov_base);
            rangesPtr->size += iovPtr->iov_len;
        }

        /*
         * Second pass, send http headers and data
         */

        Ns_ConnSetRequiredHeaders(conn, type, rangesPtr->size);
        Ns_ConnQueueHeaders(conn, rangesPtr->status);

        if (chan == NULL) {

            /*
             * In mmap mode, send all iov buffers at once
             */

            result = Ns_ConnSend(conn, bufs, rangesPtr->count * 3);

        } else {

            /*
             * In chan mode, send headers and contents in separate calls
             */

            for (i = 0; i < rangesPtr->count; i++) {
                roPtr = &rangesPtr->offsets[i];

                /*
                 * Point iovPtr to headers iov buffer
                 */

                iovPtr = &bufs[i*3];
                result = Ns_ConnSend(conn, iovPtr, 1);
                if (result == NS_ERROR) {
                    break;
                }

                /*
                 * Send file content directly from open chan
                 */

                Tcl_Seek(chan, roPtr->start, SEEK_SET);
                result = Ns_ConnSendChannel(conn, chan, roPtr->size);
                if (result == NS_ERROR) {
                    break;
                }

                /*
                 * Point iovPtr to the third (boundary) iov buffer.
                 * The second iov buffer is not used in chan mode.
                 */

                iovPtr += 2;
                result = Ns_ConnSend(conn, iovPtr, 1);
                if (result == NS_ERROR) {
                    break;
                }
            }

        }

        Ns_DStringFree(&ds);
        break;
    }

    if (result == NS_OK) {
        result = Ns_ConnClose(conn);
    }

    return result;
}
