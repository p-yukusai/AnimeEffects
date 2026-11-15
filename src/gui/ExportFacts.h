#ifndef GUI_EXPORTFACTS_H
#define GUI_EXPORTFACTS_H

#include <QStringList>

#include "ctrl/ExportTarget.h"

namespace gui {

// What the export will produce and what the destination requires, in words, as the
// lines the form states above its controls.
//
// This lives outside MainWindow because it is the one piece of the export UI whose
// correctness is a TEXT question ("does the form state the destination's own limit,
// and does it say why there is no audio?") and a question a test can ask without a
// project, a GL context or a window. Every bug this session had in the export dialog
// was a disagreement between these lines and the controls underneath them, and there
// was no way to assert either one headlessly.
QStringList exportFacts(const ctrl::ExportPlan& aPlan, const ctrl::ExportTarget& aTarget,
                        const ctrl::ExportRequest& aRequest);

} // namespace gui

#endif // GUI_EXPORTFACTS_H
