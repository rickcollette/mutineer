#pragma once

/* AR (Access Rights) flags - stored in user.flags as bitset A-Z */
#define AR_FLAG(c) (1u << ((c) - 'A'))

/* AC (Activity/Restriction) flags - stored in user.ac_flags
   These control what activities a user can perform.
   Matching ORIGINAL_BBS FlagType enum. */

/* Restriction flags (R prefix in ACS) */
#define AC_RLOGON     (1u << 0)   /* L - Restricted from logging on */
#define AC_RCHAT      (1u << 1)   /* C - Restricted from chat */
#define AC_RVALIDATE  (1u << 2)   /* V - Restricted from validation */
#define AC_RUSERLIST  (1u << 3)   /* U - Restricted from user list */
#define AC_RAMSG      (1u << 4)   /* A - Restricted from auto-message */
#define AC_RPOSTAN    (1u << 5)   /* * - Restricted from anonymous posting */
#define AC_RPOST      (1u << 6)   /* P - Restricted from posting */
#define AC_REMAIL     (1u << 7)   /* E - Restricted from email */
#define AC_RVOTING    (1u << 8)   /* K - Restricted from voting */
#define AC_RMSG       (1u << 9)   /* M - Restricted from messages */

/* Feature flags (F prefix in ACS) */
#define AC_FNODLRATIO   (1u << 10)  /* 1 - No download ratio enforced */
#define AC_FNOPOSTRATIO (1u << 11)  /* 2 - No post ratio enforced */
#define AC_FNOCREDITS   (1u << 12)  /* 3 - No credits required */
#define AC_FNODELETION  (1u << 13)  /* 4 - Cannot be deleted */

/* Status flags - stored in user.status_flags */
#define STATUS_LOCKED   (1u << 0)   /* Account is locked */
#define STATUS_DELETED  (1u << 1)   /* Account is deleted */
#define STATUS_EXPERT   (1u << 2)   /* Expert mode enabled */
#define STATUS_ANSI     (1u << 3)   /* ANSI graphics enabled */
#define STATUS_PAUSE    (1u << 4)   /* Pause at end of page */
#define STATUS_HOTKEYS  (1u << 5)   /* Hot keys enabled */
#define STATUS_CLEAR    (1u << 6)   /* Clear screen between menus */
#define STATUS_RESERVED (1u << 7)   /* Reserved (was Avatar - not supported) */
#define STATUS_NOCHAT   (1u << 8)   /* Not available for chat */
#define STATUS_NOPAGE   (1u << 9)   /* Cannot be paged */

/* Helper macros */
#define HAS_AR_FLAG(user, c)      ((user)->flags & AR_FLAG(c))
#define HAS_AC_FLAG(user, flag)   ((user)->ac_flags & (flag))
#define HAS_STATUS(user, flag)    ((user)->status_flags & (flag))

#define SET_AR_FLAG(user, c)      ((user)->flags |= AR_FLAG(c))
#define SET_AC_FLAG(user, flag)   ((user)->ac_flags |= (flag))
#define SET_STATUS(user, flag)    ((user)->status_flags |= (flag))

#define CLEAR_AR_FLAG(user, c)    ((user)->flags &= ~AR_FLAG(c))
#define CLEAR_AC_FLAG(user, flag) ((user)->ac_flags &= ~(flag))
#define CLEAR_STATUS(user, flag)  ((user)->status_flags &= ~(flag))

/* File record flags - stored in files.flags */
#define FILE_FLAG_NOTVAL    (1u << 0)   /* File not validated */
#define FILE_FLAG_OFFLINE   (1u << 1)   /* File is offline */
#define FILE_FLAG_UNHIDDEN  (1u << 2)   /* Unhide file from unvalidated users */
#define FILE_FLAG_RESUME    (1u << 3)   /* Allow resume download */
#define FILE_FLAG_NOTIME    (1u << 4)   /* No time deducted for download */
#define FILE_FLAG_FREE      (1u << 5)   /* Free download (no credits) */

/* File area flags - stored in file_areas.flags */
#define FA_FLAG_CDROM       (1u << 0)   /* CD-ROM area (read-only) */
#define FA_FLAG_FREEFILES   (1u << 1)   /* All files are free */
#define FA_FLAG_NOCOUNT     (1u << 2)   /* Don't count towards ratio */
#define FA_FLAG_NOTIME      (1u << 3)   /* No time deducted */
#define FA_FLAG_PRIVATE     (1u << 4)   /* Private uploads only */
#define FA_FLAG_SLOW        (1u << 5)   /* Slow media (CD-ROM) */

/* AC flag character to bitmask mapping (for ACS R? operator) */
static inline unsigned ac_flag_from_char(char c) {
  switch (c) {
    case 'L': return AC_RLOGON;
    case 'C': return AC_RCHAT;
    case 'V': return AC_RVALIDATE;
    case 'U': return AC_RUSERLIST;
    case 'A': return AC_RAMSG;
    case '*': return AC_RPOSTAN;
    case 'P': return AC_RPOST;
    case 'E': return AC_REMAIL;
    case 'K': return AC_RVOTING;
    case 'M': return AC_RMSG;
    case '1': return AC_FNODLRATIO;
    case '2': return AC_FNOPOSTRATIO;
    case '3': return AC_FNOCREDITS;
    case '4': return AC_FNODELETION;
    default:  return 0;
  }
}
