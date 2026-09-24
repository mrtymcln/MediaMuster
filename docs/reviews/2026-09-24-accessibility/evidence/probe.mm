#include "qtaccessibilityfix.h"
#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QSysInfo>
#include <cstdio>
#include <cstdlib>
#include <AppKit/AppKit.h>

static long nativeElementVisits = 0;
static long nativeRows = 0;
static long nativeHitTests = 0;
static long nativeHitResults = 0;
static long selections = 0;

static void walkNative(id object, int depth, NSMutableSet *seen)
{
    if (!object || depth > 7 || seen.count >= 400)
        return;
    NSValue *key = [NSValue valueWithPointer:(__bridge const void *)object];
    if ([seen containsObject:key])
        return;
    [seen addObject:key];
    ++nativeElementVisits;
    if ([object respondsToSelector:@selector(accessibilityRole)]) {
        NSString *role = [object accessibilityRole];
        if ([role isEqualToString:NSAccessibilityRowRole])
            ++nativeRows;
    }
    if ([object respondsToSelector:@selector(accessibilityFrame)])
        (void)[object accessibilityFrame];
    if ([object respondsToSelector:@selector(accessibilityParent)])
        (void)[object accessibilityParent];
    if ([object respondsToSelector:@selector(accessibilityRows)]) {
        for (id row in [object accessibilityRows])
            walkNative(row, depth + 1, seen);
    }
    if ([object respondsToSelector:@selector(accessibilityChildren)]) {
        for (id child in [object accessibilityChildren])
            walkNative(child, depth + 1, seen);
    }
}

static void queryNative(QWidget &window)
{
    @autoreleasepool {
        NSView *view = reinterpret_cast<NSView *>(window.winId());
        // This is Qt's actual Cocoa entry point. It activates the platform's
        // accessibility bridge before returning native elements (Qt 6.5.3).
        id children = [view accessibilityAttributeValue:NSAccessibilityChildrenAttribute];
        NSMutableSet *seen = [NSMutableSet set];
        for (id child in children)
            walkNative(child, 0, seen);
        (void)[view accessibilityFocusedUIElement];
    }
}

static void describe(const char *name, QWidget &widget)
{
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&widget);
    std::printf("interface %s children=%d table=%d role=%d\n", name,
                iface ? iface->childCount() : -1, iface && iface->tableInterface(),
                iface ? int(iface->role()) : -1);
}

static void hitTestNative(QWidget &widget, const QPoint &position)
{
    @autoreleasepool {
        QWidget *window = widget.window();
        NSView *view = reinterpret_cast<NSView *>(window->winId());
        const QPoint local = widget.mapTo(window, position);
        NSPoint point = NSMakePoint(local.x(), local.y());
        if (!view.isFlipped)
            point.y = NSHeight(view.bounds) - point.y;
        point = [view convertPoint:point toView:nil];
        point = [view.window convertPointToScreen:point];
        ++nativeHitTests;
        id hit = [view accessibilityHitTest:point];
        if (hit)
            ++nativeHitResults;
        walkNative(hit, 0, [NSMutableSet set]);
    }
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    NSSetUncaughtExceptionHandler([](NSException *exception) {
        std::fprintf(stderr, "NATIVE EXCEPTION %s: %s\n%s\n", exception.name.UTF8String,
                     exception.reason.UTF8String, exception.callStackSymbols.description.UTF8String);
    });
    QApplication app(argc, argv);
    app.setApplicationName("MediaMusterAccessibilityProbe");
    const bool guarded = app.arguments().contains("--guard");
    const int cycles = 30;
    std::printf("Qt=%s platform=%s arch=%s guard=%d cycles=%d\n", qVersion(),
                qPrintable(QApplication::platformName()), qPrintable(QSysInfo::currentCpuArchitecture()),
                guarded, cycles);
    if (QApplication::platformName() != "cocoa")
        return 2;
    if (guarded)
        QtAccessibilityFix::install();

    QWidget window;
    window.setWindowTitle("MediaMuster isolated accessibility test");
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    auto *layout = new QVBoxLayout(&window);
    auto *list = new QListWidget;
    auto *table = new QTableWidget(0, 2);
    auto *tree = new QTreeWidget;
    tree->setHeaderLabels({"Files"});
    auto *combo = new QComboBox;
    combo->addItems({"First", "Second", "Third"});
    auto *button = new QPushButton("Unaffected control");
    layout->addWidget(list);
    layout->addWidget(table);
    layout->addWidget(tree);
    layout->addWidget(combo);
    layout->addWidget(button);
    window.resize(500, 680);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000)) {
        std::puts("FAILED: native window not exposed");
        return 3;
    }
    queryNative(window);
    std::printf("accessibility active=%d\n", QAccessible::isActive());
    if (!QAccessible::isActive())
        return 4;

    // Qt's own reported trigger, with an in-process native accessibility
    // client instead of enabling the user's system-wide VoiceOver setting.
    std::puts("stage=QInputDialog::getItem");
    QTimer::singleShot(0, [&] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            std::exit(7);
        queryNative(*dialog);
        auto *picker = dialog->findChild<QComboBox *>();
        if (!picker)
            std::exit(8);
        picker->showPopup();
        QCoreApplication::processEvents();
        queryNative(*picker->view()->window());
        picker->hidePopup();
        dialog->accept();
    });
    bool accepted = false;
    (void)QInputDialog::getItem(&window, "Qt example", "Season:",
                              {"Spring", "Summer", "Fall", "Winter"}, 0, false, &accepted);
    if (!accepted)
        return 9;

    for (int cycle = 0; cycle < cycles; ++cycle) {
        std::printf("cycle=%d stage=list\n", cycle);
        list->clear();
        queryNative(window);
        for (int row = 0; row < 12; ++row) {
            list->addItem(QString("Media folder %1").arg(row));
            list->setCurrentRow(row);
            ++selections;
            list->scrollToItem(list->item(row));
            hitTestNative(*list->viewport(), list->visualItemRect(list->item(row)).center());
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              list->visualItemRect(list->item(row)).center());
            queryNative(window);
        }
        while (list->count() > 1) {
            delete list->takeItem(0);
            list->setCurrentRow(list->count() - 1);
            ++selections;
            hitTestNative(*list->viewport(), list->visualItemRect(list->currentItem()).center());
            queryNative(window);
        }
        std::printf("cycle=%d stage=table\n", cycle);
        table->setRowCount(0);
        queryNative(window);
        for (int row = 0; row < 8; ++row) {
            table->insertRow(row);
            for (int column = 0; column < 2; ++column)
                table->setItem(row, column, new QTableWidgetItem(QString::number(row)));
            table->setCurrentCell(row, 1);
            ++selections;
            table->scrollToItem(table->currentItem());
            hitTestNative(*table->viewport(), table->visualItemRect(table->currentItem()).center());
            queryNative(window);
        }
        table->removeRow(0);
        queryNative(window);
        std::printf("cycle=%d stage=tree\n", cycle);
        tree->clear();
        queryNative(window);
        for (int row = 0; row < 5; ++row) {
            auto *parent = new QTreeWidgetItem(tree, {QString("Project %1").arg(row)});
            auto *child = new QTreeWidgetItem(parent, {"Clip"});
            parent->setExpanded(true);
            tree->setCurrentItem(child);
            ++selections;
            tree->scrollToItem(child);
            hitTestNative(*tree->viewport(), tree->visualItemRect(child).center());
            queryNative(window);
        }
        tree->collapseAll();
        queryNative(window);
        tree->expandAll();
        queryNative(window);
        std::printf("cycle=%d stage=dropdown\n", cycle);
        combo->showPopup();
        QCoreApplication::processEvents();
        queryNative(*combo->view()->window());
        combo->view()->setCurrentIndex(combo->model()->index(cycle % 3, 0));
        ++selections;
        combo->hidePopup();

        // Non-modal form of the QInputDialog item picker named in QTBUG-119526.
        QInputDialog dialog(&window);
        dialog.setComboBoxItems({"One", "Two", "Three"});
        dialog.setComboBoxEditable(false);
        dialog.show();
        QCoreApplication::processEvents();
        queryNative(dialog);
        auto *picker = dialog.findChild<QComboBox *>();
        if (!picker)
            return 5;
        picker->showPopup();
        QCoreApplication::processEvents();
        queryNative(*picker->view()->window());
        picker->view()->setCurrentIndex(picker->model()->index(cycle % 3, 0));
        ++selections;
        picker->hidePopup();
        dialog.accept();
        QCoreApplication::processEvents();
    }
    describe("list", *list);
    describe("table", *table);
    describe("tree", *tree);
    describe("button", *button);
    std::printf("PASS cycles=%d selections=%ld native_element_visits=%ld native_rows=%ld "
                "native_hit_tests=%ld native_hit_results=%ld\n",
                cycles, selections, nativeElementVisits, nativeRows, nativeHitTests, nativeHitResults);
    return nativeElementVisits > 0 && nativeHitResults > 0 ? 0 : 6;
}
