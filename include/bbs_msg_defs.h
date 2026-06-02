#pragma once

/* Anonymous posting policy (ANonTyp from ORIGINAL_BBS) */
#define ANON_NO       0   /* No anonymous posting */
#define ANON_YES      1   /* Anonymous posting allowed */
#define ANON_FORCED   2   /* All posts are anonymous */
#define ANON_DEARABBY 3   /* Dear Abby style (to: anonymous) */
#define ANON_ANYNAME  4   /* User can enter any name */

/* Message area flags (matching ORIGINAL_BBS MAFlags) */
#define MA_FLAG_REALNAME    (1u << 0)   /* Use real name instead of handle */
#define MA_FLAG_UNHIDDEN    (1u << 1)   /* Area is always visible */
#define MA_FLAG_FILTER      (1u << 2)   /* Filter profanity */
#define MA_FLAG_PRIVATE     (1u << 3)   /* Private messages only */
#define MA_FLAG_FORCEREAD   (1u << 4)   /* Force reading before posting */
#define MA_FLAG_QUOTE       (1u << 5)   /* Auto-quote when replying */
#define MA_FLAG_ECHOMAIL    (1u << 6)   /* FidoNet echomail area */
#define MA_FLAG_NETMAIL     (1u << 7)   /* FidoNet netmail area */

/* Message attributes (matching ORIGINAL_BBS MAttr) */
#define MSG_ATTR_PRIVATE     (1u << 0)   /* Private message */
#define MSG_ATTR_SENT        (1u << 1)   /* Message has been sent */
#define MSG_ATTR_UNVALIDATED (1u << 2)   /* Not yet validated */
#define MSG_ATTR_PERMANENT   (1u << 3)   /* Cannot be deleted */
#define MSG_ATTR_DELETED     (1u << 4)   /* Marked for deletion */
#define MSG_ATTR_ANONYMOUS   (1u << 5)   /* Posted anonymously */
#define MSG_ATTR_FORWARDED   (1u << 6)   /* Forwarded message */
#define MSG_ATTR_CARBON_COPY (1u << 7)   /* Carbon copy */

/* FidoNet message attributes (matching ORIGINAL_BBS NetAttr) */
#define NET_ATTR_PRIVATE     (1u << 0)
#define NET_ATTR_CRASH       (1u << 1)
#define NET_ATTR_RECEIVED    (1u << 2)
#define NET_ATTR_SENT        (1u << 3)
#define NET_ATTR_FILEATTACH  (1u << 4)
#define NET_ATTR_INTRANSIT   (1u << 5)
#define NET_ATTR_ORPHAN      (1u << 6)
#define NET_ATTR_KILLSENT    (1u << 7)
#define NET_ATTR_LOCAL       (1u << 8)
#define NET_ATTR_HOLD        (1u << 9)
#define NET_ATTR_FILEREQ     (1u << 11)
#define NET_ATTR_RETRECREQ   (1u << 12)
#define NET_ATTR_ISRETRECPT  (1u << 13)
#define NET_ATTR_AUDITREQ    (1u << 14)
#define NET_ATTR_FILEUPDREQ  (1u << 15)
