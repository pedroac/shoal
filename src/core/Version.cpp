#include "core/Version.h"

#ifndef SHOAL_VERSION_STRING
#define SHOAL_VERSION_STRING "unknown"
#endif

namespace shoal::core {

QString version()
{
    return QStringLiteral(SHOAL_VERSION_STRING);
}

}  // namespace shoal::core
