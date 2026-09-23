#include "rebalancedialog.h"
#include <QApplication>
#include <QLabel>
#include <QDebug>
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 auto *dialog=RebalanceDialog::createDemo(RebalanceDialog::DemoScenario::Small);
 dialog->show();
 QCoreApplication::processEvents();
 dialog->grab().save("/tmp/mediamuster-review-20260922/operations/rebalance-before.png");
 QMetaObject::invokeMethod(dialog,"onRebalanceClicked",Qt::DirectConnection);
 QMetaObject::invokeMethod(dialog,"onCancelClicked",Qt::DirectConnection);
 QCoreApplication::processEvents();
 dialog->grab().save("/tmp/mediamuster-review-20260922/operations/rebalance-cancelled.png");
 for (const auto *label : dialog->findChildren<QLabel*>()) {
  if (label->text().contains("moved")) qInfo().noquote()<<label->text();
 }
 delete dialog;
}
