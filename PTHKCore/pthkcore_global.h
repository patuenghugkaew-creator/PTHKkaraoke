#ifndef PTHKCORE_GLOBAL_H
#define PTHKCORE_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(PTHKCORE_LIBRARY)
#  define PTHKCORE_EXPORT Q_DECL_EXPORT
#else
#  define PTHKCORE_EXPORT Q_DECL_IMPORT
#endif

#endif // PTHKCORE_GLOBAL_H
