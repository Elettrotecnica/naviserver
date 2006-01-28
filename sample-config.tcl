#
# The contents of this file are subject to the Mozilla Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://www.mozilla.org/.
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is AOLserver Code and related documentation
# distributed by AOL.
# 
# The Initial Developer of the Original Code is America Online,
# Inc. Portions created by AOL are Copyright (C) 1999 America Online,
# Inc. All Rights Reserved.
#
# Alternatively, the contents of this file may be used under the terms
# of the GNU General Public License (the "GPL"), in which case the
# provisions of GPL are applicable instead of those above.  If you wish
# to allow use of your version of this file only under the terms of the
# GPL and not to allow others to use your version of this file under the
# License, indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by the GPL.
# If you do not delete the provisions above, a recipient may use your
# version of this file under either the License or the GPL.
# 
#
# $Header$
#

#
# sample-config.tcl --  Example config script.
#
#  This script is an naviserver configuration script with
#  several example sections.  To use:
#
#  % cp sample-config.tcl myconfig.tcl
#  % vi myconfig.tcl		(edit as needed)
#  % bin/nsd -f -t myconfig.tcl	(test in foreground)
#  % bin/nsd -t myconfig.tcl	(run in background)
#  % gdb bin/nsd
#  (gdb) run -f -d -t myconfig.tcl (run in debugger)
#

#
# Set some Tcl variables that are commonly used throughout this file.
#

set servername	"server1"
set serverdesc	"Server Name"

# Absolute path to installation directory
set homedir	/usr/local/ns

# The hostname, address and port for nssock should be set to actual values.
set hostname	[ns_info hostname]
set address	[ns_info address]
set port	8080

# Root directory for each virtual server
set serverdir	${homedir}

# Alternatively in case when multiple server share the same
# installation, server can be put into seperate directories
#set serverdir	${homedir}/servers/${servername}

# Relative directory under serverdir for html/adp files
set pageroot	pages

# Absolute path to pages directory
set pagedir	$serverdir/$pageroot

#
# Modules to load
#

ns_section	"ns/server/${servername}/modules"
ns_param	nssock			${homedir}/bin/nssock.so
ns_param	nslog 			${homedir}/bin/nslog.so
ns_param	nscgi 			${homedir}/bin/nscgi.so
ns_param	nsperm 			${homedir}/bin/nsperm.so
ns_param 	nscp 			${homedir}/bin/nscp.so

# Tcl modules are loaded here as well, they should be put
# under tcl/ in separate directory each
#ns_param	nstk			Tcl

#
# Global server parameters
#

ns_section	"ns/parameters"
ns_param	home			$homedir
ns_param	debug			false

# Where all shared Tcl modules are located
ns_param        tcllibrary              ${homedir}/tcl

# Main server log file
ns_param        serverlog               ${homedir}/logs/nsd.log

# Pid file of the server process
ns_param        pidfile                 ${homedir}/logs/nsd.pid

#
# I18N Parameters
#

# Automatic adjustment of response content-type header to include charset
# This defaults to True.
ns_param	hackcontenttype		true

# Default output charset.  When none specified, no character encoding of 
# output is performed.
ns_param	outputcharset		iso8859-1

# Default Charset for Url Encode/Decode. When none specified, no character 
# set encoding is performed.
ns_param	urlcharset		iso8859-1

# This parameter supports output encoding arbitration.
ns_param  preferredcharsets		{ utf-8 iso8859-1 }

#
# MIME types.
#
#  Note: naviserver already has an exhaustive list of MIME types, but in
#  case something is missing you can add it here.
#

ns_section	"ns/mimetypes"

# MIME type for unknown extension.
ns_param	default			"*/*"

# MIME type for missing extension.
ns_param	noextension		"*/*"

#
# I18N Mime-types
#
#  Define content-type header values to be mapped from these file-types.
# 
#  Note that you can map file-types of adp files to control
#  the output encoding through mime-type specificaion.
#  Remember to add an adp mapping for that extension.
#
ns_param	.adp			"text/html; charset=iso-8859-1"
ns_param	.u_adp          	"text/html; charset=UTF-8"
ns_param	.gb_adp         	"text/html; charset=GB2312"
ns_param	.sjis_html      	"text/html; charset=shift_jis"
ns_param	.sjis_adp       	"text/html; charset=shift_jis"
ns_param	.gb_html        	"text/html; charset=GB2312"

#
#   I18N File-type to Encoding mappings
#
ns_section	"ns/encodings"
ns_param   	.utf_html       	"utf-8"
ns_param   	.sjis_html      	"shiftjis"
ns_param   	.gb_html        	"gb2312"
ns_param   	.big5_html      	"big5"
ns_param   	.euc-cn_html    	"euc-cn"
#
# Note: you will need to include file-type to encoding mappings
# for ANY source files that are to be used, to allow the
# server to handle them properly.  E.g., the following
# asserts that the GB-producing .adp files are themselves
# encoded in GB2312 (this is not simply assumed).
#
ns_param   	.gb_adp         	"gb2312"

#
# Thread library (nsthread) parameters
#
ns_section 	"ns/threads"

# Per-thread stack size.
ns_param   	stacksize		[expr 128*1024]

#
# Server-level configuration
#
#  There is only one server in naviserver, but this is helpful when multiple
#  servers share the same configuration file.  This file assumes that only
#  one server is in use so it is set at the top in the "server" Tcl variable.
#  Other host-specific values are set up above as Tcl variables, too.
#

ns_section	"ns/servers"
ns_param	$servername     	$serverdesc

#
# Server parameters
#
#  Server-level I18N Parameters can be specified here, to override
#  the global ones for this server.
#  These are: hackcontenttype outputcharset urlcharset
#  See the global parameter I18N section for a description of these.
#

ns_section 	"ns/server/${servername}"

# Parse *.tcl files in pageroot.
ns_param   	enabletclpages  	false

#
# Scaling and Tuning Options
#

# Normally there's one conn per thread
ns_param   	connsperthread  	0
# Flush all data before returning
ns_param   	flushcontent    	false
# Max connections to put on queue
ns_param   	maxconnections  	100
# Tune this to scale your server
ns_param   	maxthreads      	10
# Tune this to scale your server
ns_param   	minthreads      	0
# Idle threads die at this rate
ns_param   	threadtimeout   	120

#
# ADP (AOLserver Dynamic Page) configuration
#
ns_section 	"ns/server/${servername}/adp"

# Extensions to parse as ADP's.
ns_param   	map             	"/*.adp"

# Set "Expires: now" on all ADP's.
ns_param   	enableexpire    	false

# Allow Tclpro debugging with "?debug".
ns_param   	enabledebug     	false

# I18N Note: will need to define I18N specifying mappings of ADP's here as well.
ns_param   	map             	"/*.u_adp"
ns_param   	map             	"/*.gb_adp"
ns_param   	map             	"/*.sjis_adp"

# ADP start page to use for empty ADP requests
#ns_param   	startpage      		$pagedir/index.adp

# ADP error page.
#ns_param   	errorpage      		$pagedir/errorpage.adp


#
# Server specific Tcl setup
#

ns_section      "ns/server/${servername}/tcl"

# Number of buckets in Tcl hash table for nsv vars
ns_param        nsvbuckets              16

# Path to private Tcl modules
ns_param        library                 ${homedir}/modules/tcl

#
# Fast Path --
#
#  Fast path configuration is used to configure options used for serving 
#  static content, and also provides options to automatically display 
#  directory listings.
#

ns_section	"ns/server/${servername}/fastpath"

# Defines absolute path to server's home directory
ns_param    	serverdir             ${serverdir}

# Defines absolute or relative to serverdir directory where all 
# html/adp pages are located
ns_param    	pagedir               ${pageroot}

# Enable cache for normal URLs. Optional, default is false.
ns_param	cache			false

# Size of fast path cache. Optional, default is 5120000.
ns_param	cachemaxsize		5120000

# Largest file size allowed in cache. Optional, default is cachemaxsize / 10.
ns_param	cachemaxentry		512000

# Use mmap() for cache. Optional, default is false.
ns_param	mmap			false

# Directory index/default page to look for.
ns_param        directoryfile           "index.adp index.tcl index.html index.htm"

# Directory listing style. Optional, Can be "fancy" or "simple". 
ns_param	directorylisting	fancy

# Name of Tcl proc to use to display directory listings. Optional, default is to use
# _ns_dirlist. You can either specify directoryproc, or directoryadp - not both.
ns_param        directoryproc           _ns_dirlist

# Name of ADP page to use to display directory listings. Optional. You can either
# specify directoryadp or directoryproc - not both.
#ns_param	directoryadp		""

#
# Socket driver module (HTTP)  -- nssock
#

ns_section 	"ns/server/${servername}/module/nssock"

# TCP port server will listen on
ns_param   	port           		$port

# IP address for listener to bind on
ns_param   	address        		$address

# Hostname to use in redirects
ns_param   	hostname       		$hostname

# Max upload size
ns_param  	maxinput	  	1024000

# Max line size
ns_param  	maxline	  		4096

# Read-ahead buffer size
ns_param  	bufsize        		16384

# Max upload size when to use spooler
ns_param  	readahead      		16384

# Max upload size when to use statistics
ns_param  	uploadsize     		2048

# Number of spooler threads
ns_param  	spoolerthreads 		1

# Number of writer threads
ns_param  	writerthreads  		0

# Min return file size when to use writer
ns_param  	writersize     		1048576

# Timed-out waiting for complete request.
ns_param   	readtimeoutlogging    	false

# Unable to match request to a virtual server.
ns_param   	serverrejectlogging   	false

# Malformed request, or would exceed request limits.
ns_param   	sockerrorlogging      	false

# Error while attempting to shutdown a socket during connection close.
ns_param   	sockshuterrorlogging  	false

#
# Access log -- nslog
#

ns_section 	"ns/server/${servername}/module/nslog"
# Name to the log file
ns_param   	file            	$homedir/logs/access.log

# If true then use common log format
ns_param   	formattedtime   	true

# If true then use NCSA combined format
ns_param   	logcombined     	true

# Put in the log request elapsed time
ns_param	logreqtime		false

# Max # of lines in the buffer, 0 ni limit
ns_param   	maxbuffer       	0

# Max # of files to keep when rolling
ns_param   	maxbackup       	100

# Time to roll log
ns_param   	rollhour        	0

# If true then do the log rolling
ns_param   	rolllog         	true

# If true then roll the log on SIGHUP
ns_param   	rollonsignal    	false

# If true then don't show query string in the log
ns_param   	suppressquery   	false

# If true ten check for X-Forwarded-For header
ns_param   	checkforproxy   	false

# List of additional headers to put in the log
#ns_param   	extendedheaders 	"Referer X-Forwarded-For"

#
# CGI interface -- nscgi
#
#  WARNING: These directories must not live under pageroot.
#

ns_section 	"ns/server/${servername}/module/nscgi"
# CGI script file dir (GET).
ns_param   	map 			"GET  /cgi-bin /usr/local/cgi"

# CGI script file dir (POST).
ns_param   	map 			"POST /cgi-bin /usr/local/cgi"

#
# Example: Control port configuration.
#
#  To enable:
#  
#  1. Define an address and port to listen on. For security
#     reasons listening on any port other then 127.0.0.1 is 
#     not recommended.
#
#  2. Decided whether or not you wish to enable features such
#     as password echoing at login time, and command logging. 
#
#  3. Add a list of authorized users and passwords. The entires
#     take the following format:
#   
#     <user>:<encryptedPassword>:
#
#     You can use the ns_crypt Tcl command to generate an encrypted
#     password. The ns_crypt command uses the same algorithm as the 
#     Unix crypt(3) command. You could also use passwords from the
#     /etc/passwd file.
#
#     The first two characters of the password are the salt - they can be 
#     anything since the salt is used to simply introduce disorder into
#     the encoding algorithm.
#
#     ns_crypt <key> <salt>
#     ns_crypt x t2
#    
#     The configuration example below adds the user "nsadmin" with a 
#     password of "x".
#
#  4. Make sure the nscp.so module is loaded in the modules section.
#

ns_section 	"ns/server/${servername}/module/nscp"
ns_param 	address 		127.0.0.1
ns_param 	port 			9999
ns_param 	echopassword 		true
ns_param 	cpcmdlogging 		false

ns_section 	"ns/server/${servername}/module/nscp/users"
ns_param 	user 			"nsadmin:t2GqvvaiIUbF2:"

#
# Example: Host headers based virtual servers.
#
# To enable:
#
# 1. Load comm driver(s) globally.
# 2. Configure drivers as in a virtual server.
# 3. Add a "servers" section to map virtual servers to Host headers.
# 4. Ensure "defaultserver" in comm driver refers to a defined
#    virtual server.
#
#ns_section 	"ns/modules"
#ns_param   	nssock          	${homedir}/bin/nssock.so

ns_section 	"ns/module/nssock"
ns_param   	port            	$port
ns_param   	hostname        	$hostname
ns_param   	address         	$address
ns_param   	defaultserver   	$servername

ns_section 	"ns/module/nssock/servers"
ns_param   	$servername         	$hostname:$port

#
# Example: Dynamic Host headers based virtual servers.
#
#  To enable:
#
#  1. Enable by setting enabled to true.
#  2. For each hosted name create directory under ${serverdir}
#  3. Each virtual host directory should have ${pageroot} subdirectory
#  
#  /usr/local/ns/
#        servers/${servername}
#                        host.com/
#                               pages
#                        domain.net/
#                               pages
#
#
ns_section	"ns/server/${servername}/vhost" 

# Enable or disable virtual hosting
ns_param        enabled                 false

# Prefix between serverdir and host name
ns_param        hostprefix              ""

# Remove :port in the Host: header when building pageroot path so Host: www.host.com:80
# will result in pageroot ${serverdir}/www.host.com
ns_param        stripport               true

# Remove www. prefix from Host: header when building pageroot path so Host: www.host.com
# will result in pageroot ${serverdir}/host.com
ns_param        stripwww                true

# Hash the leading characters of string into a path, skipping periods and slashes. 
# If string contains less characters than levels requested, '_' characters are used as padding.
# For example, given the string 'foo' and the levels 2, 3:
#   foo, 2 -> /f/o
#   foo, 3 -> /f/o/o
ns_param        hosthashlevel           0

#
# Example:  Multiple connection thread pools.
#
#  To enable:
# 
#  1. Define one or more thread pools.
#  2. Configure pools as with the default server pool.
#  3. Map method/URL combinations to the pools
# 
#  All unmapped method/URL's will go to the default server pool.
# 

ns_section 	"ns/server/server1/pools"
ns_param 	slow 			"Slow requests here."
ns_param 	fast 			"Fast requests here."

ns_section 	"ns/server/server1/pool/slow"
ns_param 	map 			"POST /slowupload.adp"
ns_param 	maxthreads      	20
ns_param 	minthreads      	0

ns_section 	"ns/server/server1/pool/fast"
ns_param 	map 			"GET /faststuff.adp"
ns_param 	maxthreads 		10

#
# Example:  Web based stats interface.
#
#  To enable:
#
#  1. Configure whether or not stats are enabled. (Optional: default = false)
#  2. Configure URL for statistics. (Optional: default = /_stats)
#
#    http://<host>:<port>/_stats
# 
#  3. Configure user. (Optional: default = naviserver)
#  4. Configure password. (Optional: default = stats)
#
#  For added security it is recommended that configure your own
#  URL, user, and password instead of using the default values.
#

ns_section 	"ns/server/stats"
ns_param 	enabled 		1
ns_param 	url 			/_stats
ns_param 	user 			nsadmin
ns_param 	password 		nsadmin
 
