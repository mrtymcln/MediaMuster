#include "aboutdialog.h"

#include "userinfo.h"
#include "version.h"

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

// MARK: - Layout constants

namespace
{
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
	const QString editor = UserInfo::displayName();
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
