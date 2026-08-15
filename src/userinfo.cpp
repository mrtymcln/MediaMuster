#include "userinfo.h"

#include <QDir>

#if defined(Q_OS_UNIX)
#include <pwd.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
// Lean-and-mean keeps windows.h from dragging in rpcndr.h and friends,
// whose macros (`small`, `interface`, ...) stomp on ordinary identifiers.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#endif

QString UserInfo::displayName()
{
#if defined(Q_OS_UNIX)
	if (const struct passwd *pw = ::getpwuid(::getuid()))
	{
		// GECOS can carry comma-separated sub-fields; the full name is first.
		const QString full = QString::fromLocal8Bit(pw->pw_gecos)
								 .section(QLatin1Char(','), 0, 0)
								 .trimmed();
		if (!full.isEmpty())
			return full;
	}
#elif defined(Q_OS_WIN)
	wchar_t buf[256];
	ULONG len = 256;
	if (::GetUserNameExW(NameDisplay, buf, &len) && len > 0)
		return QString::fromWCharArray(buf, int(len));
#endif
	QString user = qEnvironmentVariable("USER");
	if (user.isEmpty())
		user = qEnvironmentVariable("USERNAME");
	if (user.isEmpty())
		user = QDir::home().dirName();
	return user;
}