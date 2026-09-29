#include "aboutdialog.h"

#include "version.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QLabel>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

#if defined(Q_OS_UNIX)
#include <pwd.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#endif

namespace
{
	constexpr int kDialogWidth = 420;
	constexpr int kSideMargin = 40;
	constexpr int kRollWidth = kDialogWidth - 2 * kSideMargin;
	constexpr int kRollHeight = 170;
	constexpr int kRollTickMs = 30;
	constexpr int kRollStartDelayMs = 1500;

	QString userDisplayName();

	QLabel *addLabel(QVBoxLayout *layout, const QString &text, int pointDelta = 0, bool bold = false)
	{
		auto *label = new QLabel(text, layout->parentWidget());
		label->setAlignment(Qt::AlignHCenter);
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		QFont font = label->font();
		font.setPointSizeF(font.pointSizeF() + pointDelta);
		font.setBold(bold);
		label->setFont(font);
		layout->addWidget(label);
		return label;
	}

	void startCreditsRoll(QWidget *viewport, QWidget *content, QLabel *postCredits)
	{
		if (content->height() <= viewport->height())
			return;

		auto *rollTimer = new QTimer(viewport);
		rollTimer->setInterval(kRollTickMs);
		QObject::connect(rollTimer, &QTimer::timeout, content,
						 [viewport, content, postCredits]
						 {
							 int y = content->y() - 1;
							 if (y + content->height() < 0)
							 {
								 y = viewport->height();
								 if (postCredits->isHidden())
								 {
									 postCredits->show();
									 content->adjustSize();
								 }
							 }
							 content->move(0, y);
						 });
		QTimer::singleShot(kRollStartDelayMs, rollTimer, [rollTimer]
						   { rollTimer->start(); });
	}

	QWidget *createCredits(QWidget *parent)
	{
		auto *viewport = new QWidget(parent);
		viewport->setFixedSize(kRollWidth, kRollHeight);
		auto *content = new QWidget(viewport);
		content->setFixedWidth(kRollWidth);
		auto *roll = new QVBoxLayout(content);
		roll->setContentsMargins(0, 0, 0, 0);
		roll->setSpacing(0);

		const auto addCredit = [roll](const QString &role, const QString &names)
		{
			if (names.isEmpty())
				return;
			addLabel(roll, role);
			addLabel(roll, names, 0, true);
			roll->addSpacing(16);
		};

		addCredit(AboutDialog::tr("Writer and Director"), QStringLiteral("Marty McLean"));
		addCredit(AboutDialog::tr("Icon Designer"), QStringLiteral("Matthew Skiles"));
		addCredit(AboutDialog::tr("Editor"), userDisplayName());
		addCredit(AboutDialog::tr("Assistant Editor"), QStringLiteral("Bella, the Kelpie"));
		addCredit(AboutDialog::tr("Special Thanks"),
				  QStringLiteral("Jack Brown\n"
								 "Ben Gold\n"
								 "Nathan Katsikaros\n"
								 "John Lynn\n"
								 "Harry B. Miller III\n"
								 "Jean-Denis Rouette\n"
								 "Nacho Santana"));
		addLabel(roll, AboutDialog::tr("© 2026 Marty McLean. All rights reserved."), -1);

		auto *postCredits = addLabel(roll, AboutDialog::tr("You're still watching!? There's no post-credits scene..."), -1);
		postCredits->setContentsMargins(0, 28, 0, 0);
		postCredits->hide();

		content->adjustSize();
		startCreditsRoll(viewport, content, postCredits);
		return viewport;
	}
} // namespace

AboutDialog::AboutDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("About MediaMuster"));
	setAttribute(Qt::WA_DeleteOnClose);
	setModal(true);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(kSideMargin, 28, kSideMargin, 24);
	layout->setSpacing(8);

	const QPixmap appIcon = QApplication::windowIcon().pixmap(QSize(90, 90), devicePixelRatioF());
	if (!appIcon.isNull())
	{
		auto *icon = new QLabel(this);
		icon->setPixmap(appIcon);
		layout->addWidget(icon, 0, Qt::AlignHCenter);
		layout->addSpacing(12);
	}

	addLabel(layout, APP_NAME, 10, true);
	addLabel(layout, tr("Version %1").arg(APP_VERSION), -1);
	layout->addSpacing(10);
	layout->addWidget(createCredits(this), 0, Qt::AlignHCenter);

	setFixedWidth(kDialogWidth);
	adjustSize();
	setFixedSize(size());
}

namespace
{
	/// Get user's name for the Editor easter egg.
	QString userDisplayName()
	{
#if defined(Q_OS_UNIX)
		if (const struct passwd *pw = ::getpwuid(::getuid()))
		{
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
} // namespace
