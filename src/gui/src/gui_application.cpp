//
// Created by egi on 5/12/19.
//

#include "gui_application.h"
#include "main_window.h"

#include <QSurfaceFormat>
#include <QApplication>
#include <QFile>

gui_application::gui_application(
    project_manager &pm_arg,
    int argc_arg,
    char **argv_arg)
  : pm (pm_arg)
  , argc (argc_arg)
  , argv (argv_arg)
{ }

int gui_application::run()
{
  Q_INIT_RESOURCE (resources);

  QApplication app (argc, argv);
  QFile style_file (":/styles/bright_theme.css");
  style_file.open (QFile::ReadOnly);
  QString style = style_file.readAll ();
  app.setStyleSheet (style);

  main_window window (pm);
  window.resize (QSize (1000, 800));
  window.show ();

  int ret = app.exec ();
  Q_CLEANUP_RESOURCE (resources);

  return ret;
}

