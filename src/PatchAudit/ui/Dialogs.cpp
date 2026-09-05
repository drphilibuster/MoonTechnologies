#include "Dialogs.hpp"

#include <rack.hpp>

using namespace rack;

namespace ui_pa {

void showMissingModulesMenu(const ps::ScanResult& scan, const ps::InstallResult& r,
                            std::function<void()> onProceed) {
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel(r.title));

	// Two counts, because they answer two different questions and conflating them
	// is what made this dialog untrustworthy: it used to print a module count
	// above a list of plugins, so "7 of 21 modules are unavailable" sat over four
	// rows whose own numbers summed to something else again.
	menu->addChild(createMenuLabel(string::f("%d of %d modules unavailable",
		scan.missingModules, scan.totalModules)));
	std::string from = string::f("from %d plugin%s", (int) scan.missing.size(),
		scan.missing.size() == 1 ? "" : "s");
	if (scan.deadEndModules > 0)
		from += string::f("  --  %d with no fix", scan.deadEndModules);
	menu->addChild(createMenuLabel(from));

	if (!scan.libraryKnown) {
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("The VCV Library hasn't answered yet, so"));
		menu->addChild(createMenuLabel("these verdicts are provisional."));
		menu->addChild(createMenuItem("Retry", "", []() { ps::vcvlib::retry(); }));
	}

	menu->addChild(new ui::MenuSeparator);

	for (size_t i = 0; i < scan.missing.size(); i++) {
		const ps::MissingEntry& m = scan.missing[i];
		const ps::resolve::Verdict& v = m.verdict;

		// A row is clickable when it has somewhere worth going -- not, as before,
		// whenever the slug happened to be in the manifests. That got it backwards
		// both ways: delisted plugins stayed clickable, and the rows with nothing
		// to offer were greyed with no explanation.
		std::string right = v.actionLabel;
		if (m.instances > (int) m.modelSlugs.size())
			right += string::f(" (%d used)", m.instances);
		std::string url = v.actionUrl;
		menu->addChild(createMenuItem(v.displayName, right,
			[url]() { if (!url.empty()) system::openBrowser(url); }, url.empty()));

		// Name the modules underneath, so the row's count is checkable and a
		// retired module is identified rather than merely counted.
		std::string detail = v.detail;
		while (!detail.empty()) {
			size_t nl = detail.find('\n');
			menu->addChild(createMenuLabel("    " + detail.substr(0, nl)));
			if (nl == std::string::npos)
				break;
			detail = detail.substr(nl + 1);
		}
	}

	menu->addChild(new ui::MenuSeparator);
	std::string allUrl = scan.combinedLibraryUrl();
	if (!allUrl.empty())
		menu->addChild(createMenuItem("Open all in VCV Library", "",
			[allUrl]() { system::openBrowser(allUrl); }));
	menu->addChild(new ui::MenuSeparator);

	menu->addChild(createMenuItem("Import anyway", "missing modules are skipped", onProceed));
	menu->addChild(createMenuItem("Cancel", "", []() {}));
}

void showFileChoiceMenu(const ps::InstallResult& r, std::function<void(int64_t)> onPick) {
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel("This patch ships several files"));
	menu->addChild(new ui::MenuSeparator);
	for (size_t i = 0; i < r.alternateFiles.size(); i++) {
		const ps::PatchFile& f = r.alternateFiles[i];
		int64_t id = f.id;
		std::string size = f.filesize > 0
			? string::f("%.0f KB", f.filesize / 1024.0) : "";
		menu->addChild(createMenuItem(f.filename, size, [onPick, id]() { onPick(id); }));
	}
	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuItem("Cancel", "", []() {}));
}

} // namespace ui_pa
