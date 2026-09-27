#include "revealinfinder.h"

#include <QFileInfo>

#ifdef Q_OS_MAC
#import <AppKit/AppKit.h>
#else
#include <QDesktopServices>
#include <QUrl>
#endif

#ifdef Q_OS_WIN
#include "diagnostics.h"
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <string>
#endif

namespace RevealInFinder
{

#ifndef Q_OS_MAC
	namespace
	{

		// Request a parent-folder window. Later failures aren't reported.
		void openParentFolder(const QString &parentDir, const Logger &log)
		{
			if (parentDir.isEmpty() || !QFileInfo(parentDir).exists())
			{
				log(QtCriticalMsg, QStringLiteral("Failed to open the parent folder; its location is unavailable."));
				return;
			}
			if (!QDesktopServices::openUrl(QUrl::fromLocalFile(parentDir)))
				log(QtWarningMsg, QStringLiteral("Failed to open: %1.").arg(parentDir));
		}

#ifdef Q_OS_WIN

		// Try the Shell API, then Explorer. If Explorer can't start, open the parent.
		void revealOnWindows(const QString &path, const QString &parentDir, const Logger &log)
		{
			const QString nativePath = QDir::toNativeSeparators(path);

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

			// Only undo COM initialisation if this call succeeds.
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
					qCWarning(lcApp).noquote() << QStringLiteral("SHOpenFolderAndSelectItems failed: 0x%1")
													  .arg(static_cast<quint32>(openHr), 8, 16, QChar('0'));
				}
			}
			else
			{
				qCWarning(lcApp).noquote() << QStringLiteral("SHParseDisplayName failed: 0x%1")
												  .arg(static_cast<quint32>(parseHr), 8, 16, QChar('0'));
			}

			if (needCoUninit)
				CoUninitialize();

			if (shellApiSucceeded)
				return;

			QProcess explorer;
			explorer.setProgram(QStringLiteral("explorer.exe"));
			// Preserve Explorer's required comma and quotes.
			explorer.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(canonicalPath));
			// Starting Explorer does not confirm that it selected the file.
			if (explorer.startDetached())
				return;

			log(QtWarningMsg, QStringLiteral("Shell API + explorer.exe /select both failed; opening parent"));
			openParentFolder(parentDir, log);
		}

#endif

	} // namespace
#endif

	void reveal(const QString &path, const Logger &log)
	{
		if (path.isEmpty())
		{
			log(QtCriticalMsg, QStringLiteral("No file path to reveal"));
			return;
		}

		const QFileInfo fi(path);
#ifdef Q_OS_MAC
		@autoreleasepool
		{
			if (![[NSWorkspace sharedWorkspace] selectFile:fi.absoluteFilePath().toNSString()
								  inFileViewerRootedAtPath:@""])
				log(QtWarningMsg, QStringLiteral("Failed to reveal: %1.").arg(path));
		}
#else
		const QString parentDir = fi.absolutePath();

		// If the file is missing, try its parent folder.
		if (!fi.exists())
		{
			log(QtWarningMsg, QStringLiteral("File not found. Trying its parent folder: %1.").arg(parentDir));
			openParentFolder(parentDir, log);
			return;
		}

#ifdef Q_OS_WIN
		revealOnWindows(path, parentDir, log);
#else
		openParentFolder(parentDir, log);
#endif
#endif
	}

} // namespace RevealInFinder
