#include "revealinfinder.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>	 // SHOpenFolderAndSelectItems, SHParseDisplayName
#include <objbase.h> // CoInitializeEx — Shell API needs COM apartment
#include <string>
#endif

namespace RevealInFinder
{

	namespace
	{

		// Last-resort fallback. Called when the file no longer exists, or
		// when a platform-specific reveal failed. Plain "show this folder" —
		// Qt picks Finder or Explorer, so no per-platform branch here (the
		// reveal-and-highlight tiers below are the genuinely platform-bound part).
		void openParentFolder(const QString &parentDir, const Logger &log)
		{
			if (parentDir.isEmpty() || !QFileInfo(parentDir).exists())
			{
				log(QtCriticalMsg, QStringLiteral("Cannot reveal: file and parent folder are both unreachable"));
				return;
			}
			if (!QDesktopServices::openUrl(QUrl::fromLocalFile(parentDir)))
				log(QtWarningMsg, QStringLiteral("Couldn't open the parent folder: %1").arg(parentDir));
		}

#ifdef Q_OS_MAC

		// Three-tier reveal:
		//   1. `open -R <path>`: fast and reliable for local paths.
		//   2. AppleScript `tell Finder to reveal`: survives network mounts
		//      where `open -R` sometimes loses the file.
		//   3. Parent-folder fallback if both fail.
		void revealOnMac(const QString &path, const QString &parentDir, const Logger &log)
		{
			if (QProcess::startDetached(QStringLiteral("open"), {"-R", path}))
				return;

			QString escapedPath = path;
			escapedPath.replace(QLatin1String("\\"), QLatin1String("\\\\"))
				.replace(QLatin1String("\""), QLatin1String("\\\""));
			const QString revealScript =
				QStringLiteral("tell application \"Finder\" to reveal POSIX file \"%1\"").arg(escapedPath);
			const QStringList args = {QStringLiteral("-e"), revealScript, QStringLiteral("-e"),
									  QStringLiteral("tell application \"Finder\" to activate")};
			if (QProcess::startDetached(QStringLiteral("osascript"), args))
				return;

			log(QtWarningMsg, QStringLiteral("open(1) and osascript both failed; opening parent"));
			openParentFolder(parentDir, log);
		}

#elif defined(Q_OS_WIN)

		// Three-tier reveal:
		//   1. Shell API (SHParseDisplayName + SHOpenFolderAndSelectItems).
		//   2. `explorer.exe /select,"<path>"` fallback.
		//   3. Parent-folder fallback.
		//
		// Explorer needs `/select,"<path>"` with the comma; `setNativeArguments`
		// preserves the exact quoting Explorer expects.
		void revealOnWindows(const QString &path, const QString &parentDir, const Logger &log)
		{
			const QString nativePath = QDir::toNativeSeparators(path);

			// `GetFullPathNameW` handles relative and long paths in one
			// call. 4096 wchars is well above `MAX_PATH`.
			std::wstring fullPath(4096, L'\0');
			const DWORD fullPathLen =
				GetFullPathNameW(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
								 static_cast<DWORD>(fullPath.size()), fullPath.data(), nullptr);
			if (fullPathLen == 0 || fullPathLen >= fullPath.size())
			{
				log(QtWarningMsg, QStringLiteral("GetFullPathNameW failed; opening parent folder"));
				openParentFolder(parentDir, log);
				return;
			}
			fullPath.resize(fullPathLen);
			const QString canonicalPath = QString::fromWCharArray(fullPath.data());

			// CoInitializeEx defensively: Qt may have already initialised
			// COM with a different model (RPC_E_CHANGED_MODE). Skip the
			// matching CoUninitialize in that case.
			const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			const bool needCoUninit = (coHr == S_OK || coHr == S_FALSE);

			bool shellApiSucceeded = false;
			PIDLIST_ABSOLUTE pidl = nullptr;
			SFGAOF sfgao = 0;
			const HRESULT parseHr = SHParseDisplayName(fullPath.data(), nullptr, &pidl, 0, &sfgao);
			if (SUCCEEDED(parseHr) && pidl)
			{
				const HRESULT openHr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
				CoTaskMemFree(pidl);
				shellApiSucceeded = SUCCEEDED(openHr);
				if (!shellApiSucceeded)
				{
					log(QtWarningMsg, QStringLiteral("SHOpenFolderAndSelectItems failed: 0x%1")
										  .arg(static_cast<quint32>(openHr), 8, 16, QChar('0')));
				}
			}
			else
			{
				log(QtWarningMsg, QStringLiteral("SHParseDisplayName failed: 0x%1")
									  .arg(static_cast<quint32>(parseHr), 8, 16, QChar('0')));
			}

			if (needCoUninit)
				CoUninitialize();

			if (shellApiSucceeded)
				return;

			// Tier 2: explorer.exe /select,"path".
			QProcess explorer;
			explorer.setProgram(QStringLiteral("explorer.exe"));
			explorer.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(canonicalPath));
			if (explorer.startDetached())
				return;

			log(QtWarningMsg, QStringLiteral("Shell API + explorer.exe /select both failed; opening parent"));
			openParentFolder(parentDir, log);
		}

#endif

	} // namespace

	void reveal(const QString &path, const Logger &log)
	{
		if (path.isEmpty())
		{
			log(QtCriticalMsg, QStringLiteral("No file path to reveal"));
			return;
		}

		const QFileInfo fi(path);
		const QString parentDir = fi.absolutePath();

		// File gone, so open parent unhighlighted.
		if (!fi.exists())
		{
			log(QtWarningMsg, QStringLiteral("File not found, opening parent folder: %1").arg(parentDir));
			openParentFolder(parentDir, log);
			return;
		}

#ifdef Q_OS_MAC
		revealOnMac(path, parentDir, log);
#elif defined(Q_OS_WIN)
		revealOnWindows(path, parentDir, log);
#else
		openParentFolder(parentDir, log);
#endif
	}

} // namespace RevealInFinder