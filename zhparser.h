#ifndef ZHPARSER_H
#define ZHPARSER_H

/*
 * SCWS prior to 1.2.3 declared a function named `pstrdup` in scws.h, which
 * collides with PostgreSQL's pstrdup() macro. Rather than #define-shadowing
 * it (which is fragile if SCWS later inlines or changes the signature), we
 * isolate the rename to this header only.
 *
 * Build systems linking against SCWS >= 1.2.3 can pass
 * -DZHPARSER_SCWS_HAS_NO_PSTRDUP_CONFLICT to skip this entirely.
 */

#ifndef ZHPARSER_SCWS_HAS_NO_PSTRDUP_CONFLICT
#  ifdef pstrdup
#    define ZHPARSER_SAVED_PSTRDUP pstrdup
#    undef pstrdup
#  endif
#  define pstrdup scws_pstrdup
#endif

#include "scws.h"

#ifndef ZHPARSER_SCWS_HAS_NO_PSTRDUP_CONFLICT
#  undef pstrdup
#  ifdef ZHPARSER_SAVED_PSTRDUP
#    define pstrdup ZHPARSER_SAVED_PSTRDUP
#    undef ZHPARSER_SAVED_PSTRDUP
#  endif
#endif

#endif /* ZHPARSER_H */
