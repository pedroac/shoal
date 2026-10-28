#include "assets/SpratLibrary.h"

#include <sprat/sprat.h>

namespace shoal::assets {

QString spratLibraryVersion()
{
    return QString::fromUtf8(sprat_version());
}

}  // namespace shoal::assets
