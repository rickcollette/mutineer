#pragma once
#include <stddef.h>
#include "bbs_session.h"

/* MCI (Mutineer Command Interpreter) expansion
   
   Color codes:
     ^0-^9     - User color scheme colors (mapped to ANSI)
     ~L#       - Set foreground color (0-15)
     ~B#       - Set background color (0-7)
     ~K#       - Blink attribute (0=off, 1=on)
     ~RS       - Reset all attributes
   
   User MCI codes (~XX):
     ~UN       - User handle
     ~RN       - Real name
     ~U#       - User ID number
     ~AG       - User age
     ~BD       - Birth date
     ~SX       - Sex (M/F/U)
     ~CT       - City/state
     ~ST       - Street address
     ~ZP       - Zip code
     ~PH       - Phone number
     ~FO       - First time on date
     ~LO       - Last on date
     ~TT       - Total time on (minutes)
     ~TL       - Time left (minutes)
     ~TB       - Time bank balance
     ~SL       - Security level
     ~DL       - Downloads count
     ~UL       - Uploads count
     ~DK       - Download KB
     ~UK       - Upload KB
     ~MP       - Messages posted
     ~ES       - Emails sent
     ~FB       - Feedback count
     ~CR       - Credits
     ~FP       - File points
     ~LG       - Total logons
     ~NN       - Node number
     ~AR       - AR flags list
   
   System MCI codes (~XX):
     ~BN       - BBS name
     ~SN       - Sysop name
     ~BP       - BBS phone
     ~TC       - Total calls
     ~NU       - Number of users
     ~NF       - Number of files
     ~NM       - Number of messages
     ~NO       - Number online
     ~VR       - Version string
     ~DA       - Date (YYYY-MM-DD)
     ~TM       - Time (HH:MM)
     ~AN       - Current area name
   
   Legacy percent codes (%XX):
     %NL       - Newline (skip)
     %PE       - Pause prompt
     %UN       - User handle
     %TI/%TL   - Time left
     %NN       - Node number
     %DA       - Date
     %TM       - Time
     %PO       - User posts
     %MT       - Total messages
     %FT       - Total files
     %SL       - Security level
     %CR       - Credits
     %FP       - File points
     %MA       - Messages in area
     %FA       - Files in area
     %NO       - Nodes online
     %AR       - AR flags
     %?ACS{then|else} - Conditional
*/

size_t mci_expand(const Session* s, const char* in, char* out, size_t cap);

/* Color scheme support */
#define MCI_NUM_COLOR_SCHEMES 8
const char* mci_scheme_name(int scheme_id);   /* returns NULL if out of range */
void mci_scheme_preview(const char* out, int scheme_id, char* buf, size_t cap);
