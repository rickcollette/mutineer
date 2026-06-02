#pragma once
#include <stdbool.h>

/* Forward declaration - full definition in bbs_session.h */
typedef struct Session Session;

/* ACS (Access Control String) evaluator with precedence (! > & > |).
   
   Operators (matching ORIGINAL_BBS):
     S#   - Security Level >= #
     D#   - Download Security Level >= #
     F?   - Has AR flag ? (A-Z)
     R?   - Has AC (restriction) flag ? (A-Z)
     A#   - Age >= #
     B#   - Baud >= # * 100 (always true for telnet)
     C?   - In conference ? (A-Z)
     G?   - Gender is ? (M/F)
     H#   - Current hour = #
     N#   - Node number = #
     P#   - Credit >= #
     T#   - Time remaining >= #
     U#   - User number = #
     V    - User is validated (not expired)
     W#   - Day of week = # (0=Sun, 6=Sat)
     X#   - Days until expiration <= #
     Z    - Meets post ratio
   
   Legacy support:
     SLnn, Lnn - security level >= nn
     +X        - AR flag X set
     ARABC     - all listed AR flags set
     C>=N      - credits >= N
     R>=N      - ratio >= N
     T>=N      - time >= N
   
   Boolean operators:
     !expr     - negation
     expr & expr - AND
     expr | expr - OR
     (expr)    - grouping
*/
bool acs_allows(const Session* s, const char* acs);

/* Simplified ACS check for standalone tools (no session context).
   Only supports S#, D#, F?, and basic level checks. */
bool acs_check(const char* acs, int level, unsigned ar_flags, unsigned ac_flags);
