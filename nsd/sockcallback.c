/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
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
 * sockcallback.c --
 *
 *	Support for the socket callback thread.
 */

static const char *RCSID = "@(#) $Header$, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following defines a socket being monitored.
 */

typedef struct Callback {
    struct Callback     *nextPtr;
    int                  sock;
    int			 idx;
    int                  when;
    Ns_SockProc         *proc;
    void                *arg;
} Callback;

/*
 * Local functions defined in this file
 */

static Ns_ThreadProc SockCallbackThread;
static int Queue(int sock, Ns_SockProc *proc, void *arg, int when);
static void CallbackTrigger(void);

/*
 * Static variables defined in this file
 */

static Callback	    *firstQueuePtr, *lastQueuePtr;
static int	     shutdownPending;
static int	     running;
static Ns_Thread     sockThread;
static Ns_Mutex      lock;
static Ns_Cond	     cond;
static int	     trigPipe[2];
static Tcl_HashTable table;


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockCallback --
 *
 *	Register a callback to be run when a socket reaches a certain 
 *	state. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will wake up the callback thread. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockCallback(int sock, Ns_SockProc *proc, void *arg, int when)
{
    return Queue(sock, proc, arg, when);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockCancelCallback --
 *
 *	Remove a callback registered on a socket. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will wake up the callback thread. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_SockCancelCallback(int sock)
{
    (void) Queue(sock, NULL, NULL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartSockShutdown, NsWaitSockShutdown --
 *
 *	Initiate and then wait for socket callbacks shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May timeout waiting for shutdown.
 *
 *----------------------------------------------------------------------
 */

void
NsStartSockShutdown(void)
{
    Ns_MutexLock(&lock);
    if (running) {
	shutdownPending = 1;
	CallbackTrigger();
    }
    Ns_MutexUnlock(&lock);
}

void
NsWaitSockShutdown(Ns_Time *toPtr)
{
    int status;
    
    status = NS_OK;
    Ns_MutexLock(&lock);
    while (status == NS_OK && running) {
	status = Ns_CondTimedWait(&cond, &lock, toPtr);
    }
    Ns_MutexUnlock(&lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "socks: timeout waiting for callback shutdown");
    } else if (sockThread != NULL) {
	Ns_ThreadJoin(&sockThread, NULL);
	sockThread = NULL;
    	close(trigPipe[0]);
    	close(trigPipe[1]);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CallbackTrigger --
 *
 *	Wakeup the callback thread if it's in poll().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
CallbackTrigger(void)
{
    if (write(trigPipe[1], "", 1) != 1) {
	Ns_Fatal("trigger write() failed: %s", strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Queue --
 *
 *	Queue a callback for socket.
 *
 * Results:
 *	NS_OK or NS_ERROR on shutdown pending.
 *
 * Side effects:
 *	Socket thread may be created or signalled.
 *
 *----------------------------------------------------------------------
 */

static int
Queue(int sock, Ns_SockProc *proc, void *arg, int when)
{
    Callback   *cbPtr;
    int         status, trigger, create;

    cbPtr = ns_malloc(sizeof(Callback));
    cbPtr->sock = sock;
    cbPtr->proc = proc;
    cbPtr->arg = arg;
    cbPtr->when = when;
    trigger = create = 0;
    Ns_MutexLock(&lock);
    if (shutdownPending) {
	ns_free(cbPtr);
    	status = NS_ERROR;
    } else {
	if (!running) {
    	    Tcl_InitHashTable(&table, TCL_ONE_WORD_KEYS);
	    Ns_MutexSetName2(&lock, "ns", "socks");
	    create = 1;
	    running = 1;
	} else if (firstQueuePtr == NULL) {
	    trigger = 1;
	}
        if (firstQueuePtr == NULL) {
            firstQueuePtr = cbPtr;
        } else {
            lastQueuePtr->nextPtr = cbPtr;
        }
        cbPtr->nextPtr = NULL;
        lastQueuePtr = cbPtr;
    	status = NS_OK;
    }
    Ns_MutexUnlock(&lock);
    if (trigger) {
	CallbackTrigger();
    } else if (create) {
    	if (ns_sockpair(trigPipe) != 0) {
	    Ns_Fatal("ns_sockpair() failed: %s", strerror(errno));
    	}
    	Ns_ThreadCreate(SockCallbackThread, NULL, 0, &sockThread);
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * SockCallbackThread --
 *
 *	Run callbacks registered with Ns_SockCallback.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Depends on callbacks.
 *
 *----------------------------------------------------------------------
 */

static void
SockCallbackThread(void *ignored)
{
    char          c;
    int           when[3], events[3];
    int           n, i, whenany, new, stop;
    int		  max, nfds;
    Callback     *cbPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    struct pollfd *pfds;

    Ns_ThreadSetName("-socks-");
    Ns_WaitForStartup();
    Ns_Log(Notice, "socks: starting");

    events[0] = POLLIN;
    events[1] = POLLOUT;
    events[2] = POLLPRI;
    when[0] = NS_SOCK_READ;
    when[1] = NS_SOCK_WRITE;
    when[2] = NS_SOCK_EXCEPTION;
    whenany = (when[0] | when[1] | when[2] | NS_SOCK_EXIT);
    max = 100;
    pfds = ns_malloc(sizeof(struct pollfd) * max);
    pfds[0].fd = trigPipe[0];
    pfds[0].events = POLLIN;
    
    while (1) {

	/*
	 * Grab the list of any queue updates and the shutdown
	 * flag.
	 */

    	Ns_MutexLock(&lock);
	cbPtr = firstQueuePtr;
	firstQueuePtr = NULL;
        lastQueuePtr = NULL;
	stop = shutdownPending;
	Ns_MutexUnlock(&lock);
    
	/*
    	 * Move any queued callbacks to the active table.
	 */

    	while (cbPtr != NULL) {
	    hPtr = Tcl_CreateHashEntry(&table, (char *) cbPtr->sock, &new);
	    if (!new) {
		ns_free(Tcl_GetHashValue(hPtr));
	    }
	    Tcl_SetHashValue(hPtr, cbPtr);
	    cbPtr = cbPtr->nextPtr;

	}

	/*
	 * Verify and set the poll bits for all active callbacks.
	 */

	if (max <= table.numEntries) {
	    max  = table.numEntries + 100;
	    pfds = ns_realloc(pfds, max);
	}
	nfds = 1;
	hPtr = Tcl_FirstHashEntry(&table, &search);
	while (hPtr != NULL) {
	    cbPtr = Tcl_GetHashValue(hPtr);
	    if (!(cbPtr->when & whenany)) {
	    	Tcl_DeleteHashEntry(hPtr);
		ns_free(cbPtr);
	    } else {
		cbPtr->idx = nfds;
		pfds[nfds].fd = cbPtr->sock;
		pfds[nfds].events = pfds[nfds].revents = 0;
        	for (i = 0; i < 3; ++i) {
                    if (cbPtr->when & when[i]) {
			pfds[nfds].events |= events[i];
                    }
        	}
		++n;
	    }
	    hPtr = Tcl_NextHashEntry(&search);
        }

    	/*
	 * Select on the sockets and drain the trigger pipe if
	 * necessary.
	 */

	if (stop) {
	    break;
	}
	pfds[0].revents = 0;
	do {
	    n = poll(pfds, nfds, -1);
	} while (n < 0 && errno == EINTR);
	if (n < 0) {
	    Ns_Fatal("poll() failed: %s", strerror(errno));
	}
	if ((pfds[0].revents & POLLIN) && read(trigPipe[0], &c, 1) != 1) {
	    Ns_Fatal("trigger read() failed: %s", strerror(errno));
	}

    	/*
	 * Execute any ready callbacks.
	 */
	 
    	hPtr = Tcl_FirstHashEntry(&table, &search);
	while (n > 0 && hPtr != NULL) {
	    cbPtr = Tcl_GetHashValue(hPtr);
            for (i = 0; i < 3; ++i) {
                if ((cbPtr->when & when[i])
		    && (pfds[cbPtr->idx].revents & events[i])) {
                    if (!((*cbPtr->proc)(cbPtr->sock, cbPtr->arg, when[i]))) {
			cbPtr->when = 0;
		    }
                }
            }
	    hPtr = Tcl_NextHashEntry(&search);
        }
    }

    /*
     * Invoke any exit callabacks, cleanup the callback
     * system, and signal shutdown complete.
     */

    Ns_Log(Notice, "socks: shutdown pending");
    hPtr = Tcl_FirstHashEntry(&table, &search);
    while (hPtr != NULL) {
	cbPtr = Tcl_GetHashValue(hPtr);
	if (cbPtr->when & NS_SOCK_EXIT) {
	    (void) ((*cbPtr->proc)(cbPtr->sock, cbPtr->arg, NS_SOCK_EXIT));
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    hPtr = Tcl_FirstHashEntry(&table, &search);
    while (hPtr != NULL) {
	ns_free(Tcl_GetHashValue(hPtr));
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&table);

    Ns_Log(Notice, "socks: shutdown complete");
    Ns_MutexLock(&lock);
    running = 0;
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);
}


void
NsGetSockCallbacks(Tcl_DString *dsPtr)
{
    Callback     *cbPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char buf[100];

    Ns_MutexLock(&lock);
    if (running) {
	hPtr = Tcl_FirstHashEntry(&table, &search);
	while (hPtr != NULL) {
	    cbPtr = Tcl_GetHashValue(hPtr);
	    Tcl_DStringStartSublist(dsPtr);
	    sprintf(buf, "%d", (int) cbPtr->sock);
	    Tcl_DStringAppendElement(dsPtr, buf);
	    Tcl_DStringStartSublist(dsPtr);
	    if (cbPtr->when & NS_SOCK_READ) {
		Tcl_DStringAppendElement(dsPtr, "read");
	    }
	    if (cbPtr->when & NS_SOCK_WRITE) {
		Tcl_DStringAppendElement(dsPtr, "write");
	    }
	    if (cbPtr->when & NS_SOCK_EXCEPTION) {
		Tcl_DStringAppendElement(dsPtr, "exception");
	    }
	    if (cbPtr->when & NS_SOCK_EXIT) {
		Tcl_DStringAppendElement(dsPtr, "exit");
	    }
	    Tcl_DStringEndSublist(dsPtr);
	    Ns_GetProcInfo(dsPtr, (void *) cbPtr->proc, cbPtr->arg);
	    Tcl_DStringEndSublist(dsPtr);
	    hPtr = Tcl_NextHashEntry(&search);
	}
    }
    Ns_MutexUnlock(&lock);
}
