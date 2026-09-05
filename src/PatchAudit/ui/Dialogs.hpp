#pragma once
#include "../ps/ModuleScan.hpp"
#include "../ps/Types.hpp"

#include <functional>

namespace ui_pa {

/** Lists the plugins this patch needs and Rack doesn't have, with links into the
    VCV Library, and a way to import anyway. */
void showMissingModulesMenu(const ps::ScanResult& scan, const ps::InstallResult& r,
                            std::function<void()> onProceed);

/** Offered when a patch ships more than one .vcv. */
void showFileChoiceMenu(const ps::InstallResult& r, std::function<void(int64_t)> onPick);

} // namespace ui_pa
