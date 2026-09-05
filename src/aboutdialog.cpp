#include "aboutdialog.h"

#include "version.h"

#include <QApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

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

// MARK: - Layout constants

namespace
{
	/// The account's human display name, for the credits-roll easter egg —
	/// this dialog is its only consumer (folded in from userinfo.* 2026-08-31).
	QString userDisplayName()
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
	// The dialog is fixed-width, and the credits viewport is exactly the
	// space left between the side margins — derived here rather than
	// hand-computed, so widening the box can't leave the roll behind.
	constexpr int kDialogWidth = 420;
	constexpr int kSideMargin = 40;
	constexpr int kRollWidth = kDialogWidth - 2 * kSideMargin;
	constexpr int kRollHeight = 170;
	constexpr int kRollTickMs = 30; // 1 px per tick ≈ 33 px/s
	constexpr int kRollStartDelayMs = 1500;

	void tweakFont(QWidget *w, int pointDelta, bool bold = false)
	{
		QFont f = w->font();
		if (pointDelta != 0)
			f.setPointSize(f.pointSize() + pointDelta);
		if (bold)
			f.setBold(true);
		w->setFont(f);
	}
} // namespace

// MARK: - Construction

AboutDialog::AboutDialog(QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("About MediaMuster"));
	setAttribute(Qt::WA_DeleteOnClose);
	setModal(true);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(kSideMargin, 28, kSideMargin, 24);
	layout->setSpacing(8);

	auto *icon = new QLabel;
	const QPixmap appIcon = QApplication::windowIcon().pixmap(QSize(90, 90), devicePixelRatioF());
	if (!appIcon.isNull())
	{
		icon->setPixmap(appIcon);
		icon->setAlignment(Qt::AlignHCenter);
		layout->addWidget(icon, 0, Qt::AlignHCenter);
		layout->addSpacing(12);
	}

	auto *name = new QLabel(APP_NAME);
	tweakFont(name, 10, true);
	name->setAlignment(Qt::AlignHCenter);
	layout->addWidget(name);

	auto *version = new QLabel(tr("Version %1").arg(APP_VERSION));
	tweakFont(version, -1);
	version->setAlignment(Qt::AlignHCenter);
	layout->addWidget(version);

	layout->addSpacing(10);

	// MARK: Rolling credits

	auto *viewport = new QWidget;
	viewport->setFixedSize(kRollWidth, kRollHeight);
	layout->addWidget(viewport, 0, Qt::AlignHCenter);

	auto *content = new QWidget(viewport);
	auto *roll = new QVBoxLayout(content);
	roll->setContentsMargins(0, 0, 0, 0);
	roll->setSpacing(0);

	const auto addCredit = [roll](const QString &role, const QString &name)
	{
		auto *roleLabel = new QLabel(role);
		roleLabel->setAlignment(Qt::AlignHCenter);
		roll->addWidget(roleLabel);

		auto *nameLabel = new QLabel(name);
		tweakFont(nameLabel, 0, true);
		nameLabel->setAlignment(Qt::AlignHCenter);
		nameLabel->setOpenExternalLinks(true);
		roll->addWidget(nameLabel);
		roll->addSpacing(16);
	};

	addCredit(tr("Executive Producer"), QStringLiteral("Martin McLean"));
	addCredit(tr("Assistant Producer"), QStringLiteral("Cameron Gregg"));
	addCredit(tr("Icon Designer"), QStringLiteral("Matthew Skiles"));
	addCredit(tr("Production Designer"),
			  QStringLiteral("<a href=\"https://qt.io\">Qt %1</a>, GNU LGPL v3")
				  .arg(QStringLiteral(QT_VERSION_STR)));
	addCredit(tr("Continuity"),
			  QStringLiteral("<a href=\"https://xxhash.com\">xxHash3</a>, BSD 2-Clause"));

	// Easter egg: show the account display name of the user.
	const QString editor = userDisplayName();
	if (!editor.isEmpty())
		addCredit(tr("Editor"), editor);

	addCredit(tr("Assistant Editor"), QStringLiteral("Bella, the Kelpie"));

	// Beta testers, alphabetical by surname.
	const QStringList betaTesters = {
		QStringLiteral("Jack Brown"),
		QStringLiteral("Nathan Katsikaros"),
		QStringLiteral("John Lynn"),
		QStringLiteral("Harry B. Miller III"),
		QStringLiteral("Moritz Poth"),
		QStringLiteral("Jean-Denis Rouette"),
		QStringLiteral("Nacho Santana"),
		QStringLiteral("Lawson Tanner"),
	};
	if (!betaTesters.isEmpty())
		addCredit(tr("Test Audience"), betaTesters.join(QLatin1Char('\n')));

	addCredit(tr("Crafty"), QStringLiteral("Sette Café"));

	auto *provenance =
		new QLabel(tr("Made with published specifications and examination of files.\n"
					  "No Avid source code was used in the making of this app."));
	provenance->setWordWrap(true);
	provenance->setAlignment(Qt::AlignHCenter);
	tweakFont(provenance, -1);
	roll->addWidget(provenance);
	roll->addSpacing(10);

	auto *copyright = new QLabel(tr("Copyright © 2026 Martin McLean.\nAll rights reserved."));
	copyright->setAlignment(Qt::AlignHCenter);
	tweakFont(copyright, -1);
	roll->addWidget(copyright);

	// Hidden until the credits roll a second time.
	auto *postCredits =
		new QLabel(tr("You're still watching!? There's no post-credits scene..."));
	postCredits->setAlignment(Qt::AlignHCenter);
	postCredits->setContentsMargins(0, 28, 0, 0);
	tweakFont(postCredits, -1);
	postCredits->hide();
	roll->addWidget(postCredits);

	content->setFixedWidth(kRollWidth);
	content->adjustSize();
	content->move(0, 0);

	if (content->height() > viewport->height())
	{
		auto *rollTimer = new QTimer(this);
		rollTimer->setInterval(kRollTickMs);
		connect(rollTimer, &QTimer::timeout, viewport,
				[viewport, content, postCredits, wraps = 0]() mutable
				{
					int y = content->y() - 1;
					if (y + content->height() < 0)
					{
						y = viewport->height();
						if (++wraps == 1)
						{
							postCredits->show();
							content->adjustSize();
						}
					}
					content->move(0, y);
				});
		QTimer::singleShot(kRollStartDelayMs, rollTimer, [rollTimer] { rollTimer->start(); });
	}

	layout->addSpacing(12);

	auto *btnRow = new QHBoxLayout;
	auto *licensesBtn = new QPushButton(tr("Licenses"));
	licensesBtn->setAutoDefault(false);
	connect(licensesBtn, &QPushButton::clicked, this, [this]
	{
		auto *dialog = new QDialog(this);
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->setWindowTitle(tr("Third-party licenses"));
		dialog->setWindowModality(Qt::WindowModal);
		auto *licenseLayout = new QVBoxLayout(dialog);
		auto *text = new QPlainTextEdit(dialog);
		text->setReadOnly(true);
		QFile notices(QStringLiteral(":/licenses/third-party-notices.txt"));
		text->setPlainText(notices.open(QIODevice::ReadOnly)
			? QString::fromUtf8(notices.readAll())
			: tr("The license notices could not be loaded."));
		licenseLayout->addWidget(text);
		auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
		connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::close);
		licenseLayout->addWidget(close);
		dialog->resize(680, 500);
		dialog->show();
	});
	btnRow->addWidget(licensesBtn);
	btnRow->addStretch();
	auto *okBtn = new QPushButton(tr("Sweet as!"));
	okBtn->setDefault(true);
	connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
	btnRow->addWidget(okBtn);
	btnRow->addStretch();
	layout->addLayout(btnRow);

	setFixedWidth(kDialogWidth);
	adjustSize();
	setFixedSize(size());
}
