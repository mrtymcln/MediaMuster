#include "mainwindow.h"
#include <QApplication>
#include <QAction>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include <QLabel>
#include <iostream>

class TestOperationUi {
public:
 static void probe() {
  MainWindow w(nullptr, MainWindow::StartupMode::UiOnly);
  VolumeInfo a; a.name="A"; a.path="/review-A"; a.hasAvidMedia=true;
  VolumeInfo b; b.name="B"; b.path="/review-B"; b.hasAvidMedia=true;
  w.rebuildVolumeList({a,b});
  std::cout << "cold-volume-count=" << w.m_volumeList->count() << " selected=" << w.m_volumeList->selectedItems().size() << " scan-selected-enabled=" << w.m_scanSelectedAct->isEnabled() << '\n';
  w.m_volumeList->item(0)->setSelected(true);
  w.m_volumeList->item(1)->setSelected(false);
  w.rebuildVolumeList({a,b});
  std::cout << "refresh-selected=" << w.m_volumeList->selectedItems().size() << '\n';
  QTemporaryDir dir;
  QDir().mkpath(dir.path()+"/Avid MediaFiles/MXF/1");
  w.addVolumePath(dir.path());
  std::cout << "manual-selected=" << w.m_volumeList->item(2)->isSelected() << '\n';
  MediaFile f; f.filePath="/review-A/Avid MediaFiles/MXF/1/a.mxf"; f.fileName="a.mxf"; f.project="Project A";
  w.onScanFinished({f});
  std::cout << "projects-before-removal=" << w.m_projectList->count() << '\n';
  w.m_operations->sourcesRemoved(QSet<QString>{f.filePath});
  std::cout << "rows-after-removal=" << w.m_model->rowCount() << " projects-after-removal=" << w.m_projectList->count() << '\n';
 }
};
int main(int argc,char** argv) { QApplication app(argc,argv); QStandardPaths::setTestModeEnabled(true); app.setApplicationName("MediaMusterReviewUiProbe"); TestOperationUi::probe(); }
